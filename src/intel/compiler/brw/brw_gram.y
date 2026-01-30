%{
/*
 * Copyright © 2018 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "brw_asm_internal.h"

#undef ALIGN16

#ifndef WRITEMASK_X
#define WRITEMASK_X 0x1
#define WRITEMASK_Y 0x2
#define WRITEMASK_Z 0x4
#define WRITEMASK_W 0x8
#define WRITEMASK_XY 0x3
#define WRITEMASK_XYZ 0x7
#define WRITEMASK_YZW 0xE
#define WRITEMASK_XYZW 0xF
#endif

#ifndef BRW_SWIZZLE4
#define BRW_SWIZZLE_X 0
#define BRW_SWIZZLE_Y 1
#define BRW_SWIZZLE_Z 2
#define BRW_SWIZZLE_W 3
#define BRW_SWIZZLE4(a,b,c,d) (((a)<<0) | ((b)<<2) | ((c)<<4) | ((d)<<6))
#define BRW_SWIZZLE_NOOP      BRW_SWIZZLE4(0,1,2,3)
#define BRW_SWIZZLE_XYZW      BRW_SWIZZLE4(0,1,2,3)
#endif

#define YYLTYPE YYLTYPE
typedef struct YYLTYPE
{
   int first_line;
   int first_column;
   int last_line;
   int last_column;
} YYLTYPE;

void yyerror (YYLTYPE *, struct brw_asm_parser *parser, const char *);

enum message_level {
   WARN,
   ERROR,
};

static void
message(struct brw_asm_parser *parser, enum message_level level,
        YYLTYPE *location, const char *fmt, ...)
{
   static const char *level_str[] = { "warning", "error" };
   va_list args;

   if (location)
      fprintf(stderr, "%s:%d:%d: %s: ", parser->input_filename,
              location->first_line,
              location->first_column, level_str[level]);
   else
      fprintf(stderr, "%s:%s: ", parser->input_filename, level_str[level]);

   va_start(args, fmt);
   vfprintf(stderr, fmt, args);
   va_end(args);
}

#define error(l, fmt, ...)                              \
   do {                                                 \
      message(parser, ERROR, l, fmt, ## __VA_ARGS__);   \
   } while (0)

static bool
isPowerofTwo(unsigned int x)
{
   return x && (!(x & (x - 1)));
}

static gen_operand
asm_make_base_reg(enum gen_file file, unsigned nr, unsigned subnr)
{
   gen_operand o = {};
   o.file = file;
   o.nr = nr;
   o.subnr = subnr;
   o.type = BRW_TYPE_INVALID;
   return o;
}

static gen_region
asm_make_region(unsigned vstride, unsigned width, unsigned hstride)
{
   return (gen_region){ vstride, width, hstride };
}

static gen_operand
asm_finalize_operand(gen_operand o)
{
   if (!o.indirect && o.file != GEN_IMM)
      o.subnr *= brw_type_size_bytes(o.type);
   return o;
}

static gen_operand
asm_make_direct_operand(gen_operand base, enum brw_reg_type type,
                        gen_region region, unsigned swizzle,
                        unsigned writemask, bool negate, bool abs)
{
   base.type = type;
   base.negate = negate;
   base.abs = abs;
   base.region = region;
   base.swizzle = swizzle;
   base.writemask = writemask;
   return asm_finalize_operand(base);
}

static gen_operand
asm_make_indirect_operand(gen_operand base, enum brw_reg_type type,
                          gen_region region, unsigned swizzle,
                          unsigned writemask, bool negate, bool abs)
{
   base.indirect = true;
   base.type = type;
   base.negate = negate;
   base.abs = abs;
   base.region = region;
   base.swizzle = swizzle;
   base.writemask = writemask;
   return base;
}

static gen_operand
asm_make_imm_operand(enum brw_reg_type type, uint64_t value)
{
   gen_operand o = {};
   o.file = GEN_IMM;
   o.type = type;
   o.imm = value;
   return o;
}

static unsigned
brw_swizzle_for_mask(unsigned mask)
{
   unsigned last = (mask ? ffs(mask) - 1 : 0);
   unsigned swz[4];

   for (unsigned i = 0; i < 4; i++)
      last = swz[i] = (mask & (1 << i) ? i : last);

   return BRW_SWIZZLE4(swz[0], swz[1], swz[2], swz[3]);
}

static gen_operand
brw_null_reg(void)
{
   gen_operand o = asm_make_base_reg(GEN_ARF, BRW_ARF_NULL, 0);
   o.type = BRW_TYPE_F;
   o.region = asm_make_region(8, 8, 1);
   o.swizzle = BRW_SWIZZLE_XYZW;
   o.writemask = WRITEMASK_XYZW;
   return o;
}

static gen_operand
brw_ip_reg(void)
{
   gen_operand o = asm_make_base_reg(GEN_ARF, BRW_ARF_IP, 0);
   o.type = BRW_TYPE_UD;
   o.region = asm_make_region(4, 1, 0);
   o.swizzle = BRW_SWIZZLE_XYZW;
   o.writemask = WRITEMASK_XYZW;
   return o;
}

static gen_operand
brw_ud8_reg(enum gen_file file, unsigned nr, unsigned subnr)
{
   gen_operand o = asm_make_base_reg(file, nr, subnr);
   o.type = BRW_TYPE_UD;
   o.region = asm_make_region(8, 8, 1);
   o.swizzle = BRW_SWIZZLE_XYZW;
   o.writemask = WRITEMASK_XYZW;
   return o;
}

static inline enum gfx12_systolic_depth
translate_systolic_depth(unsigned d)
{
   switch (d) {
   case 2:  return BRW_SYSTOLIC_DEPTH_2;
   case 4:  return BRW_SYSTOLIC_DEPTH_4;
   case 8:  return BRW_SYSTOLIC_DEPTH_8;
   case 16: return BRW_SYSTOLIC_DEPTH_16;
   default: UNREACHABLE("Invalid systolic depth.");
   }
}

static gen_inst *
i965_asm_unary_instruction(struct brw_asm_parser *parser, gen_opcode opcode,
                      gen_operand dest, gen_operand src0)
{
   gen_inst *inst = gen_asm_next_inst(parser, opcode);
   inst->dst = dest;
   inst->src[0] = src0;
   return inst;
}

static gen_inst *
i965_asm_binary_instruction(struct brw_asm_parser *parser, gen_opcode opcode,
                       gen_operand dest, gen_operand src0,
                       gen_operand src1)
{
   gen_inst *inst = gen_asm_next_inst(parser, opcode);
   inst->dst = dest;
   inst->src[0] = src0;
   inst->src[1] = src1;
   return inst;
}

static gen_inst *
i965_asm_ternary_instruction(struct brw_asm_parser *parser, gen_opcode opcode,
                        gen_operand dest, gen_operand src0,
                        gen_operand src1, gen_operand src2)
{
   gen_inst *inst = gen_asm_next_inst(parser, opcode);
   inst->dst = dest;
   inst->src[0] = src0;
   inst->src[1] = src1;
   inst->src[2] = src2;
   return inst;
}

static void
apply_common_fields(struct brw_asm_parser *parser, gen_inst *inst,
                    const struct predicate *pred,
                    const struct condition *cond,
                    const struct options *options,
                    unsigned exec_size, bool saturate)
{
   i965_asm_set_instruction_options(parser, inst, pred, cond, options);
   inst->exec_size = exec_size;
   inst->saturate = saturate;

   if (inst->align16) {
      for (unsigned i = 0; i < ARRAY_SIZE(inst->src); i++)
         inst->src[i].rep_ctrl = false;

      switch (inst->opcode) {
      case GEN_OP_CSEL:
      case GEN_OP_BFE:
      case GEN_OP_BFI2:
      case GEN_OP_LRP:
      case GEN_OP_MAD:
      case GEN_OP_DP4A:
      case GEN_OP_ADD3:
         inst->src[0].rep_ctrl = inst->src[0].region.vstride == 0;
         inst->src[1].rep_ctrl = inst->src[1].region.vstride == 0;
         inst->src[2].rep_ctrl = inst->src[2].region.vstride == 0;
         break;
      default:
         break;
      }
   } else {
      inst->dst.swizzle = 0;
      inst->dst.writemask = 0;

      for (unsigned i = 0; i < ARRAY_SIZE(inst->src); i++) {
         inst->src[i].swizzle = 0;
         inst->src[i].writemask = 0;
         inst->src[i].rep_ctrl = false;
      }
   }

   if (!inst->align16 && inst->exec_size == 1) {
      if (inst->src[0].file != GEN_IMM && inst->src[0].region.width == 1) {
         inst->src[0].region.vstride = 0;
         inst->src[0].region.hstride = 0;
      }
      if (inst->src[1].file != GEN_IMM && inst->src[1].region.width == 1) {
         inst->src[1].region.vstride = 0;
         inst->src[1].region.hstride = 0;
      }
   }
}
%}

%pure-parser
%lex-param { struct brw_asm_parser *parser }
%parse-param { struct brw_asm_parser *parser }

%locations

%start ROOT

%union {
   char *string;
   int integer;
   unsigned long long int llint;
   gen_operand reg;
   gen_region region;
   enum brw_reg_type reg_type;
   struct predicate predicate;
   struct condition condition;
   struct options options;
   struct instoption instoption;
   struct msgdesc msgdesc;
   struct tgl_swsb depinfo;
   struct { int sdepth; int rcount; } dpas_params;
}

%code {
int brw_asm_lex(YYSTYPE *yylval_param, YYLTYPE *yylloc_param,
                yyscan_t yyscanner);

static int
yylex(YYSTYPE *yylval_param, YYLTYPE *yylloc_param,
      struct brw_asm_parser *parser)
{
   return brw_asm_lex(yylval_param, yylloc_param, parser->scanner);
}
}

%token ABS
%token COLON
%token COMMA
%token DOT
%token LANGLE RANGLE
%token LCURLY RCURLY
%token LPAREN RPAREN
%token LSQUARE RSQUARE
%token PLUS MINUS
%token SEMICOLON
%token ASSIGN

/* datatypes */
%token <integer> TYPE_B TYPE_UB
%token <integer> TYPE_W TYPE_UW
%token <integer> TYPE_D TYPE_UD
%token <integer> TYPE_Q TYPE_UQ
%token <integer> TYPE_V TYPE_UV
%token <integer> TYPE_F TYPE_HF TYPE_HF8
%token <integer> TYPE_BF TYPE_BF8
%token <integer> TYPE_DF
%token <integer> TYPE_VF

/* label */
%token <string> JUMP_LABEL
%token <string> JUMP_LABEL_TARGET
%token JIP UIP

/* opcodes */
%token <integer> ADD ADD3 ADDC AND ASR AVG
%token <integer> BFE BFI1 BFI2 BFB BFREV BRC BRD BREAK
%token <integer> CALL CALLA CASE CBIT CMP CMPN CONT CSEL
%token <integer> DIM DO DPAS DP2 DP3 DP4 DP4A DPH
%token <integer> ELSE ENDIF FBH FBL FORK FRC
%token <integer> GOTO
%token <integer> HALT
%token <integer> IF ILLEGAL
%token <integer> JMPI JOIN
%token <integer> LINE LRP LZD
%token <integer> MAC MACH MAD MADM MATH MOV MOVI MUL MREST MSAVE
%token <integer> NENOP NOP NOT
%token <integer> OR
%token <integer> PLN POP PUSH
%token <integer> RET RNDD RNDE RNDU RNDZ ROL ROR
%token <integer> SEL SENDS SENDSC SHL SHR SMOV SRND SUBB SYNC
%token <integer> SEND_GFX4 SENDC_GFX4 SEND_GFX12 SENDC_GFX12
%token <integer> WAIT WHILE
%token <integer> XOR

/* extended math functions */
%token <integer> COS EXP FDIV INV INVM INTDIV INTDIVMOD INTMOD LOG POW RSQ
%token <integer> RSQRTM SIN SINCOS SQRT

/* sync instruction */
%token <integer> ALLRD ALLWR FENCE BAR HOST
%type <integer> sync_function
%type <reg> sync_arg

/* shared functions for send */
%token HDC0 HDC1 HDC2 HDC_RO GATEWAY PIXEL_INTERP RENDER SAMPLER
%token TS_BTD URB RT_ACCEL SLM TGM UGM

/* message details for send */
%token MSGDESC_BEGIN SRC1_LEN EX_BSO MSGDESC_END
%type <msgdesc> msgdesc msgdesc_parts;

/* Conditional modifiers */
%token <integer> EQUAL GREATER GREATER_EQUAL LESS LESS_EQUAL NOT_EQUAL
%token <integer> NOT_ZERO OVERFLOW UNORDERED ZERO

/* register Access Modes */
%token ALIGN1 ALIGN16

/* accumulator write control */
%token ACCWREN

/* compaction control */
%token CMPTCTRL

/* mask control (WeCtrl) */
%token WECTRL

/* debug control */
%token BREAKPOINT

/* dependency control */
%token NODDCLR NODDCHK

/* end of thread */
%token EOT

/* mask control */
%token MASK_DISABLE;

/* predicate control */
%token <integer> ANYV ALLV ANY2H ALL2H ANY4H ALL4H ANY8H ALL8H ANY16H ALL16H
%token <integer> ANY32H ALL32H

/* round instructions */
%token <integer> ROUND_INCREMENT

/* staturation */
%token SATURATE

/* thread control */
%token ATOMIC SWITCH

/* branch control */
%token BRANCH_CTRL

/* quater control */
%token QTR_2Q QTR_3Q QTR_4Q QTR_2H QTR_2N QTR_3N QTR_4N QTR_5N
%token QTR_6N QTR_7N QTR_8N

/* channels */
%token <integer> X Y Z W

/* reg files */
%token GENREGFILE

/* vertical stride in register region */
%token VxH

/* register type */
%token <integer> GENREG ADDRREG ACCREG FLAGREG NOTIFYREG STATEREG
%token <integer> CONTROLREG IPREG PERFORMANCEREG THREADREG CHANNELENABLEREG
%token <integer> MASKREG SCALARREG

%token <integer> INTEGER
%token <llint> LONG
%token NULL_TOKEN

%nonassoc SUBREGNUM
%left PLUS MINUS
%nonassoc DOT
%nonassoc EMPTYEXECSIZE
%nonassoc LPAREN

%type <integer> execsize exp
%type <llint> exp2

/* predicate control */
%type <integer> predctrl predstate
%type <predicate> predicate

/* conditional modifier */
%type <condition> cond_mod
%type <integer> condModifiers

/* instruction options  */
%type <options> instoptions instoption_list
%type <instoption> instoption

/* writemask */
%type <integer> writemask_x writemask_y writemask_z writemask_w
%type <integer> writemask

/* dst operand */
%type <reg> dst dstoperand dstoperandex dstoperandex_typed dstreg
%type <integer> dstregion

%type <integer> saturate
%type <reg> relativelocation2

/* src operand */
%type <reg> directsrcoperand directsrcaccoperand indirectsrcoperand srcacc
%type <reg> srcarcoperandex srcaccimm srcarcoperandex_typed srcimm
%type <reg> indirectgenreg
%type <region> indirectregion region region_wh
%type <reg> immreg src reg32 payload directgenreg_list addrparam
%type <reg> directgenreg
%type <reg> desc ex_desc reg32a
%type <integer> swizzle

/* registers */
%type <reg> accreg addrreg channelenablereg controlreg flagreg ipreg scalarreg
%type <reg> notifyreg nullreg performancereg threadcontrolreg statereg maskreg
%type <integer> subregnum

/* register types */
%type <reg_type> reg_type imm_type

/* immediate values */
%type <llint> immval

/* instruction opcodes */
%type <integer> unaryopcodes binaryopcodes binaryaccopcodes ternaryopcodes
%type <integer> sendop sendsop sendopcode sendsopcode

%type <integer> negate abs chansel math_function sharedfunction

%type <string> jumplabeltarget

/* SWSB */
%token <integer> REG_DIST_CURRENT
%token <integer> REG_DIST_FLOAT
%token <integer> REG_DIST_INT
%token <integer> REG_DIST_LONG
%token <integer> REG_DIST_ALL
%token <integer> REG_DIST_MATH
%token <integer> REG_DIST_SCALAR
%token <integer> SBID_ALLOC
%token <integer> SBID_WAIT_SRC
%token <integer> SBID_WAIT_DST

%type <depinfo> depinfo

/* DPAS */
%token <dpas_params> DPAS_PARAMS

%code {

static void
add_instruction_option(struct brw_asm_parser *parser,
                       struct options *options, struct instoption opt)
{
   if (opt.type == INSTOPTION_DEP_INFO) {
      if (opt.depinfo_value.regdist) {
         options->depinfo.regdist = opt.depinfo_value.regdist;
         options->depinfo.pipe = opt.depinfo_value.pipe;
      } else {
         options->depinfo.sbid = opt.depinfo_value.sbid;
         options->depinfo.mode = opt.depinfo_value.mode;
      }
      return;
   }
   if (opt.type == INSTOPTION_CHAN_OFFSET) {
      options->chan_offset = opt.uint_value;
      return;
   }
   switch (opt.uint_value) {
   case ALIGN1:
      options->access_mode = BRW_ALIGN_1;
      break;
   case ALIGN16:
      options->access_mode = BRW_ALIGN_16;
      break;
   case SWITCH:
      options->thread_control |= BRW_THREAD_SWITCH;
      break;
   case ATOMIC:
      options->thread_control |= BRW_THREAD_ATOMIC;
      break;
   case BRANCH_CTRL:
      options->branch_control = true;
      break;
   case NODDCHK:
      options->no_dd_check = true;
      break;
   case NODDCLR:
      options->no_dd_clear = true;
      break;
   case MASK_DISABLE:
      options->mask_control |= BRW_MASK_DISABLE;
      break;
   case BREAKPOINT:
      options->debug_control = BRW_DEBUG_BREAKPOINT;
      break;
   case WECTRL:
      options->mask_control |= BRW_WE_ALL;
      break;
   case CMPTCTRL:
      /* Don't set the compaction flag to true, we're just reading
       * text assembly, not instruction bits. The code that will
       * assemble things later will set the flag if it decides to
       * compact instructions.
       */
      if (!parser->compaction_warning_given) {
         parser->compaction_warning_given = true;
         fprintf(stderr, "%s: ignoring 'compacted' "
                 "annotations for text assembly "
                 "instructions\n", parser->input_filename);
      }
      break;
   case ACCWREN:
      options->acc_wr_control = true;
      break;
   case EOT:
      options->end_of_thread = true;
      break;
   }
}
}
%%

ROOT:
   instrseq
   ;

instrseq:
   instrseq instruction SEMICOLON
   | instrseq relocatableinstruction SEMICOLON
   | instruction SEMICOLON
   | relocatableinstruction SEMICOLON
   | instrseq jumplabeltarget
   | jumplabeltarget
   ;

/* Instruction Group */
instruction:
   unaryinstruction
   | binaryinstruction
   | binaryaccinstruction
   | mathinstruction
   | nopinstruction
   | waitinstruction
   | ternaryinstruction
   | sendinstruction
   | illegalinstruction
   | syncinstruction
   ;

relocatableinstruction:
   jumpinstruction
   | branchinstruction
   | breakinstruction
   | loopinstruction
   | joininstruction
   ;

illegalinstruction:
   ILLEGAL execsize instoptions
   {
      struct predicate pred = { .pred_control = BRW_PREDICATE_NONE };
      gen_inst *inst = gen_asm_next_inst(parser, $1);
      apply_common_fields(parser, inst, &pred, NULL, &$3, $2, false);
      (void) yynerrs;
   }
   ;

/* Unary instruction */
unaryinstruction:
   predicate unaryopcodes saturate cond_mod execsize dst srcaccimm   instoptions
   {
      gen_inst *inst = i965_asm_unary_instruction(parser, $2, $6, $7);
      apply_common_fields(parser, inst, &$1, &$4, &$8, $5,
                          $3 == BRW_INSTRUCTION_SATURATE);
   }
   ;

unaryopcodes:
   BFREV
   | CBIT
   | DIM
   | FBH
   | FBL
   | FRC
   | LZD
   | MOV
   | NOT
   | RNDD
   | RNDE
   | RNDU
   | RNDZ
   ;

/* Binary instruction */
binaryinstruction:
   predicate binaryopcodes saturate cond_mod execsize dst srcimm srcimm instoptions
   {
      gen_inst *inst = i965_asm_binary_instruction(parser, $2, $6, $7, $8);
      apply_common_fields(parser, inst, &$1, &$4, &$9, $5,
                          $3 == BRW_INSTRUCTION_SATURATE);
   }
   ;

binaryopcodes:
   ADDC
   | BFI1
   | DP2
   | DP3
   | DP4
   | DPH
   | LINE
   | MAC
   | MACH
   | MUL
   | PLN
   | ROL
   | ROR
   | SUBB
   | SRND
   ;

/* Binary acc instruction */
binaryaccinstruction:
   predicate binaryaccopcodes saturate cond_mod execsize dst srcacc srcimm instoptions
   {
      gen_inst *inst = i965_asm_binary_instruction(parser, $2, $6, $7, $8);
      apply_common_fields(parser, inst, &$1, &$4, &$9, $5,
                          $3 == BRW_INSTRUCTION_SATURATE);
   }
   ;

binaryaccopcodes:
   ADD
   | AND
   | ASR
   | AVG
   | CMP
   | CMPN
   | OR
   | SEL
   | SHL
   | SHR
   | XOR
   ;

/* Math instruction */
mathinstruction:
   predicate MATH saturate math_function execsize dst src srcimm instoptions
   {
      gen_inst *inst = gen_asm_next_inst(parser, $2);
      inst->dst = $6;
      inst->src[0] = $7;
      inst->src[1] = $8;
      inst->math.func = $4;
      apply_common_fields(parser, inst, &$1, NULL, &$9, $5,
                          $3 == BRW_INSTRUCTION_SATURATE);
   }
   ;

math_function:
   COS
   | EXP
   | FDIV
   | INV
   | INVM
   | INTDIV
   | INTDIVMOD
   | INTMOD
   | LOG
   | POW
   | RSQ
   | RSQRTM
   | SIN
   | SQRT
   | SINCOS
   ;

/* NOP instruction */
nopinstruction:
   NOP
   {
      struct predicate pred = { .pred_control = BRW_PREDICATE_NONE };
      gen_inst *inst = gen_asm_next_inst(parser, $1);
      apply_common_fields(parser, inst, &pred, NULL, &(struct options){0}, 1, false);
   }
   ;

/* Ternary operand instruction */
ternaryinstruction:
   predicate ternaryopcodes saturate cond_mod execsize dst srcimm src srcimm instoptions
   {
      gen_inst *inst = i965_asm_ternary_instruction(parser, $2, $6, $7, $8, $9);
      apply_common_fields(parser, inst, &$1, &$4, &$10, $5,
                          $3 == BRW_INSTRUCTION_SATURATE);
   }
   |
   predicate DPAS DPAS_PARAMS saturate cond_mod execsize dst src src src instoptions
   {
      assert(parser->devinfo->verx10 >= 125);
      gen_inst *inst = gen_asm_next_inst(parser, $2);
      inst->dst = $7;
      inst->src[0] = $8;
      inst->src[1] = $9;
      inst->src[2] = $10;
      inst->dpas.sdepth = translate_systolic_depth($3.sdepth);
      inst->dpas.rcount = $3.rcount;
      inst->dpas.src1_subbyte = BRW_SUB_BYTE_PRECISION_NONE;
      inst->dpas.src2_subbyte = BRW_SUB_BYTE_PRECISION_NONE;
      apply_common_fields(parser, inst, &$1, &$5, &$11, $6,
                          $4 == BRW_INSTRUCTION_SATURATE);
   }
   ;

ternaryopcodes:
   CSEL
   | BFE
   | BFI2
   | LRP
   | MAD
   | DP4A
   | ADD3
   ;

/* Wait instruction */
waitinstruction:
   WAIT execsize dst instoptions
   {
      struct predicate pred = { .pred_control = BRW_PREDICATE_NONE };
      gen_operand dest = $3;
      dest.swizzle = brw_swizzle_for_mask(dest.writemask);
      if (dest.file != GEN_ARF || dest.nr != BRW_ARF_NOTIFICATION_COUNT)
         error(&@1, "WAIT must use the notification register\n");
      gen_inst *inst = gen_asm_next_inst(parser, $1);
      inst->dst = dest;
      inst->src[0] = dest;
      inst->src[1] = brw_null_reg();
      apply_common_fields(parser, inst, &pred, NULL, &$4, $2, false);
   }
   ;

/* Send instruction */
sendinstruction:
   predicate sendopcode execsize dst payload exp2 sharedfunction msgdesc instoptions
   {
      assert(parser->devinfo->ver < 12);
      gen_inst *inst = gen_asm_next_inst(parser, $2);
      inst->dst = $4;
      inst->src[0] = $5;
      inst->src[1] = brw_null_reg();

      inst->send.desc_imm = ((uint32_t)$6) & 0x7fffffff;
      inst->send.desc_is_reg = false;
      inst->send.ex_desc_is_reg = false;
      inst->send.sfid = $7;
      inst->send.eot = $9.end_of_thread;
      apply_common_fields(parser, inst, &$1, NULL, &$9, $3, false);
   }
   | predicate sendopcode execsize dst payload payload exp2 sharedfunction msgdesc instoptions
   {
      assert(parser->devinfo->ver < 12);
      gen_inst *inst = gen_asm_next_inst(parser, $2);
      inst->dst = $4;
      inst->src[0] = $5;
      inst->src[1] = brw_null_reg();

      if ($6.file != GEN_ARF ||
          $6.nr != BRW_ARF_ADDRESS ||
          $6.subnr != 0)
         error(&@2, "SEND with indirect desc must use a0.0\n");
      inst->send.desc_is_reg = true;
      inst->send.ex_desc_is_reg = false;
      inst->send.sfid = $8;
      inst->send.eot = $10.end_of_thread;
      apply_common_fields(parser, inst, &$1, NULL, &$10, $3, false);
   }
   | predicate sendsopcode execsize dst payload payload desc ex_desc sharedfunction msgdesc instoptions
   {
      gen_inst *inst = gen_asm_next_inst(parser, $2);
      inst->dst = $4;
      inst->src[0] = $5;
      inst->src[1] = $6;

      if ($7.file == GEN_IMM) {
         inst->send.desc_is_reg = false;
         inst->send.desc_imm = $7.imm & 0x7fffffff;
      } else {
         inst->send.desc_is_reg = true;
      }

      if ($8.file == GEN_IMM) {
         inst->send.ex_desc_is_reg = false;
         inst->send.ex_desc_imm = $8.imm;
      } else {
         inst->send.ex_desc_is_reg = true;
         inst->send.ex_desc_subnr = $8.subnr;
      }

      inst->send.sfid = $9;
      inst->send.eot = $11.end_of_thread;

      if (parser->devinfo->verx10 >= 125 && $10.ex_bso) {
         inst->send.ex_bso = true;
         inst->send.src1_len = $10.src1_len;
      }

      apply_common_fields(parser, inst, &$1, NULL, &$11, $3, false);
   }
   | predicate sendsopcode execsize dst GENREGFILE LSQUARE scalarreg RSQUARE desc ex_desc sharedfunction msgdesc instoptions
   {
      assert(parser->devinfo->ver >= 30);
      gen_inst *inst = gen_asm_next_inst(parser, $2);
      inst->dst = $4;
      inst->src[0] = $7;
      inst->src[1] = brw_null_reg();

      if ($9.file == GEN_IMM) {
         inst->send.desc_is_reg = false;
         inst->send.desc_imm = $9.imm & 0x7fffffff;
      } else {
         inst->send.desc_is_reg = true;
      }

      if ($10.file == GEN_IMM) {
         inst->send.ex_desc_is_reg = false;
         inst->send.ex_desc_imm = $10.imm;
      } else {
         inst->send.ex_desc_is_reg = true;
         inst->send.ex_desc_subnr = $10.subnr;
      }

      inst->send.sfid = $11;
      inst->send.eot = $13.end_of_thread;

      if ($12.ex_bso)
         inst->send.ex_bso = true;

      apply_common_fields(parser, inst, &$1, NULL, &$13, $3, false);
   }
   ;

sendop:
   SEND_GFX4
   | SENDC_GFX4
   ;

sendsop:
   SEND_GFX12
   | SENDC_GFX12
   | SENDS
   | SENDSC
   ;

sendopcode:
   sendop   { $$ = $1; }
   ;

sendsopcode:
   sendsop  { $$ = $1; }
   ;

sharedfunction:
   NULL_TOKEN          { $$ = BRW_SFID_NULL; }
   | GATEWAY           { $$ = BRW_SFID_MESSAGE_GATEWAY; }
   | URB               { $$ = BRW_SFID_URB; }
   | TS_BTD            { $$ = BRW_SFID_BINDLESS_THREAD_DISPATCH; }
   | RENDER            { $$ = BRW_SFID_RENDER_CACHE; }
   | HDC_RO            { $$ = BRW_SFID_HDC_READ_ONLY; }
   | HDC0              { $$ = BRW_SFID_HDC0; }
   | PIXEL_INTERP      { $$ = BRW_SFID_PIXEL_INTERPOLATOR; }
   | HDC1              { $$ = BRW_SFID_HDC1; }
   | SAMPLER           { $$ = BRW_SFID_SAMPLER; }
   | HDC2              { $$ = BRW_SFID_HDC2; }
   | RT_ACCEL          { $$ = BRW_SFID_RAY_TRACE_ACCELERATOR; }
   | SLM               { $$ = BRW_SFID_SLM; }
   | TGM               { $$ = BRW_SFID_TGM; }
   | UGM               { $$ = BRW_SFID_UGM; }
   ;

exp2:
   LONG       { $$ = $1; }
   | MINUS LONG    { $$ = -$2; }
   ;

desc:
   reg32a
   | exp2
   {
      $$ = asm_make_imm_operand(BRW_TYPE_UD, $1);
   }
   ;

ex_desc:
   reg32a
   | exp2
   {
      $$ = asm_make_imm_operand(BRW_TYPE_UD, $1);
   }
   ;

reg32a:
   addrreg region reg_type
   {
      $$ = asm_make_direct_operand($1, $3, $2, BRW_SWIZZLE_NOOP,
                                   WRITEMASK_XYZW, false, false);
   }
   ;


/* Jump instruction */
jumpinstruction:
   predicate JMPI execsize relativelocation2 instoptions
   {
      gen_inst *inst = gen_asm_next_inst(parser, $2);
      inst->src[0] = $4;
      apply_common_fields(parser, inst, &$1, NULL, &$5, $3, false);
   }
   ;

/* branch instruction */
branchinstruction:
   predicate ENDIF execsize JIP JUMP_LABEL instoptions
   {
      gen_inst *inst = gen_asm_next_inst(parser, $2);
      brw_asm_label_use_jip(parser, $5);
      apply_common_fields(parser, inst, &$1, NULL, &$6, $3, false);
   }
   | ELSE execsize JIP JUMP_LABEL UIP JUMP_LABEL instoptions
   {
      struct predicate pred = { .pred_control = BRW_PREDICATE_NONE };
      gen_inst *inst = gen_asm_next_inst(parser, $1);
      brw_asm_label_use_jip(parser, $4);
      brw_asm_label_use_uip(parser, $6);
      apply_common_fields(parser, inst, &pred, NULL, &$7, $2, false);
   }
   | predicate IF execsize JIP JUMP_LABEL UIP JUMP_LABEL instoptions
   {
      gen_inst *inst = gen_asm_next_inst(parser, $2);
      brw_asm_label_use_jip(parser, $5);
      brw_asm_label_use_uip(parser, $7);
      apply_common_fields(parser, inst, &$1, NULL, &$8, $3, false);
   }
   | predicate GOTO execsize JIP JUMP_LABEL UIP JUMP_LABEL instoptions
   {
      gen_inst *inst = gen_asm_next_inst(parser, $2);
      brw_asm_label_use_jip(parser, $5);
      brw_asm_label_use_uip(parser, $7);
      apply_common_fields(parser, inst, &$1, NULL, &$8, $3, false);
   }
   ;

joininstruction:
   predicate JOIN execsize JIP JUMP_LABEL instoptions
   {
      gen_inst *inst = gen_asm_next_inst(parser, $2);
      brw_asm_label_use_jip(parser, $5);
      apply_common_fields(parser, inst, &$1, NULL, &$6, $3, false);
   }
   ;

breakinstruction:
   predicate BREAK execsize JIP JUMP_LABEL UIP JUMP_LABEL instoptions
   {
      gen_inst *inst = gen_asm_next_inst(parser, $2);
      brw_asm_label_use_jip(parser, $5);
      brw_asm_label_use_uip(parser, $7);
      apply_common_fields(parser, inst, &$1, NULL, &$8, $3, false);
   }
   | predicate HALT execsize JIP JUMP_LABEL UIP JUMP_LABEL instoptions
   {
      gen_inst *inst = gen_asm_next_inst(parser, $2);
      brw_asm_label_use_jip(parser, $5);
      brw_asm_label_use_uip(parser, $7);
      apply_common_fields(parser, inst, &$1, NULL, &$8, $3, false);
   }
   | predicate CONT execsize JIP JUMP_LABEL UIP JUMP_LABEL instoptions
   {
      gen_inst *inst = gen_asm_next_inst(parser, $2);
      brw_asm_label_use_jip(parser, $5);
      brw_asm_label_use_uip(parser, $7);
      apply_common_fields(parser, inst, &$1, NULL, &$8, $3, false);
   }
   ;

loopinstruction:
   predicate WHILE execsize JIP JUMP_LABEL instoptions
   {
      gen_inst *inst = gen_asm_next_inst(parser, $2);
      brw_asm_label_use_jip(parser, $5);
      apply_common_fields(parser, inst, &$1, NULL, &$6, $3, false);
   }
   | DO execsize instoptions
   {
      struct predicate pred = { .pred_control = BRW_PREDICATE_NONE };
      gen_inst *inst = gen_asm_next_inst(parser, $1);
      apply_common_fields(parser, inst, &pred, NULL, &$3, $2, false);
   }
   ;

syncinstruction:
   predicate SYNC sync_function execsize sync_arg instoptions
   {
      if (parser->devinfo->ver < 12)
         error(&@2, "sync instruction is supported only on gfx12+\n");

      if ($5.file == GEN_IMM &&
          $3 != TGL_SYNC_ALLRD &&
          $3 != TGL_SYNC_ALLWR) {
         error(&@2, "Only allrd and allwr support immediate argument\n");
      }

      gen_inst *inst = gen_asm_next_inst(parser, $2);
      inst->sync.func = $3;
      inst->src[0] = $5;
      apply_common_fields(parser, inst, &$1, NULL, &$6, $4, false);
   }
   ;

sync_function:
   NOP      { $$ = TGL_SYNC_NOP; }
   | ALLRD
   | ALLWR
   | FENCE
   | BAR
   | HOST
   ;

sync_arg:
   nullreg region reg_type
   {
      $$ = asm_make_direct_operand($1, $3, $2, BRW_SWIZZLE_NOOP,
                                   WRITEMASK_XYZW, false, false);
   }
   | immreg
   ;

/* Relative location */
relativelocation2:
   immreg
   | reg32
   ;

jumplabeltarget:
   JUMP_LABEL_TARGET
   {
      brw_asm_label_set(parser, $1);
   }
   ;

/* Destination register */
dst:
   dstoperand
   | dstoperandex
   ;

dstoperand:
   dstreg dstregion writemask reg_type
   {
      gen_operand o = $1;
      o.region = asm_make_region(1, 1, $2);
      o.type = $4;
      o.writemask = $3;
      o.swizzle = BRW_SWIZZLE_NOOP;
      $$ = asm_finalize_operand(o);
   }
   ;

dstoperandex:
   dstoperandex_typed dstregion writemask reg_type
   {
      gen_operand o = $1;
      o.region = asm_make_region(1, 1, $2);
      o.type = $4;
      o.writemask = $3;
      $$ = asm_finalize_operand(o);
   }
   /* BSpec says "When the conditional modifier is present, updates
    * to the selected flag register also occur. In this case, the
    * register region fields of the ‘null’ operand are valid."
    */
   | nullreg dstregion writemask reg_type
   {
      gen_operand o = $1;
      o.region = asm_make_region(1, 1, $2);
      o.type = $4;
      o.writemask = $3;
      $$ = asm_finalize_operand(o);
   }
   | threadcontrolreg
   {
      gen_operand o = $1;
      o.region = asm_make_region(1, 1, 1);
      o.type = BRW_TYPE_UW;
      $$ = asm_finalize_operand(o);
   }
   ;

dstoperandex_typed:
   accreg
   | addrreg
   | channelenablereg
   | controlreg
   | flagreg
   | ipreg
   | maskreg
   | notifyreg
   | performancereg
   | statereg
   | scalarreg
   ;

dstreg:
   directgenreg
   {
      $$ = $1;
   }
   | indirectgenreg
   {
      $$ = $1;
      $$.indirect = true;
   }
   ;

/* Source register */
srcaccimm:
   srcacc
   | immreg
   ;

immreg:
   immval imm_type
   {
      switch ($2) {
      case BRW_TYPE_UD:
         $$ = asm_make_imm_operand(BRW_TYPE_UD, (uint32_t)$1);
         break;
      case BRW_TYPE_D:
         $$ = asm_make_imm_operand(BRW_TYPE_D, (uint64_t)(int32_t)$1);
         break;
      case BRW_TYPE_UW:
         $$ = asm_make_imm_operand(BRW_TYPE_UW,
                                   (uint32_t)$1 | ((uint32_t)$1 << 16));
         break;
      case BRW_TYPE_W: {
         uint32_t packed = (uint16_t)$1 | ((uint32_t)(uint16_t)$1 << 16);
         $$ = asm_make_imm_operand(BRW_TYPE_W, packed);
         break;
      }
      case BRW_TYPE_F:
         $$ = asm_make_imm_operand(BRW_TYPE_F, (uint64_t)$1);
         break;
      case BRW_TYPE_V:
         $$ = asm_make_imm_operand(BRW_TYPE_V, (uint32_t)$1);
         break;
      case BRW_TYPE_UV:
         $$ = asm_make_imm_operand(BRW_TYPE_UV, (uint32_t)$1);
         break;
      case BRW_TYPE_VF:
         $$ = asm_make_imm_operand(BRW_TYPE_VF, (uint32_t)$1);
         break;
      case BRW_TYPE_Q:
         $$ = asm_make_imm_operand(BRW_TYPE_Q, (uint64_t)(int64_t)$1);
         break;
      case BRW_TYPE_UQ:
         $$ = asm_make_imm_operand(BRW_TYPE_UQ, (uint64_t)$1);
         break;
      case BRW_TYPE_DF:
         $$ = asm_make_imm_operand(BRW_TYPE_DF, (uint64_t)$1);
         break;
      case BRW_TYPE_HF:
         $$ = asm_make_imm_operand(BRW_TYPE_HF,
                                   (uint32_t)$1 | ((uint32_t)$1 << 16));
         break;
      default:
         error(&@2, "Unknown immediate type %s\n",
               brw_reg_type_to_letters($2));
      }
   }
   ;

reg32:
   directgenreg region reg_type
   {
      $$ = asm_make_direct_operand($1, $3, $2, BRW_SWIZZLE_NOOP,
                                   WRITEMASK_XYZW, false, false);
   }
   ;

payload:
   directsrcoperand
   ;

src:
   directsrcoperand
   | indirectsrcoperand
   ;

srcacc:
   directsrcaccoperand
   | indirectsrcoperand
   ;

srcimm:
   directsrcoperand
   | indirectsrcoperand
   | immreg
   ;

directsrcaccoperand:
   directsrcoperand
   | negate abs accreg region reg_type
   {
      $$ = asm_make_direct_operand($3, $5, $4, BRW_SWIZZLE_NOOP,
                                   WRITEMASK_X, $1, $2);
   }
   ;

srcarcoperandex:
   srcarcoperandex_typed region reg_type
   {
      $$ = asm_make_direct_operand($1, $3, $2, BRW_SWIZZLE_NOOP,
                                   WRITEMASK_XYZW, false, false);
   }
   | nullreg region reg_type
   {
      $$ = asm_make_direct_operand($1, $3, $2, BRW_SWIZZLE_NOOP,
                                   WRITEMASK_XYZW, false, false);
   }
   | threadcontrolreg
   {
      $$ = asm_make_direct_operand($1, BRW_TYPE_UW, asm_make_region(0, 1, 0),
                                   BRW_SWIZZLE_NOOP, WRITEMASK_XYZW,
                                   false, false);
   }
   ;

srcarcoperandex_typed:
   channelenablereg
   | controlreg
   | flagreg
   | ipreg
   | maskreg
   | statereg
   | scalarreg
   ;

indirectsrcoperand:
   negate abs indirectgenreg indirectregion swizzle reg_type
   {
      $$ = asm_make_indirect_operand($3, $6, $4, $5, WRITEMASK_X, $1, $2);
   }
   ;

directgenreg_list:
   directgenreg
   | notifyreg
   | addrreg
   | performancereg
   ;

directsrcoperand:
   negate abs directgenreg_list region swizzle reg_type
   {
      $$ = asm_make_direct_operand($3, $6, $4, $5, WRITEMASK_X, $1, $2);
   }
   | srcarcoperandex
   ;

/* Address register */
addrparam:
   addrreg exp
   {
      $$ = $1;
      $$.addr_imm = $2;
   }
   | addrreg
   ;

/* Register files and register numbers */
exp:
   INTEGER    { $$ = $1; }
   | LONG     { $$ = $1; }
   ;

subregnum:
   DOT exp                          { $$ = $2; }
   | /* empty */ %prec SUBREGNUM    { $$ = 0; }
   ;

directgenreg:
   GENREG subregnum
   {
      $$ = asm_make_base_reg(GEN_GRF, $1, $2);
   }
   ;

indirectgenreg:
   GENREGFILE LSQUARE addrparam RSQUARE
   {
      $$ = asm_make_base_reg(GEN_GRF, 0, $3.subnr);
      $$.addr_imm = $3.addr_imm;
   }
   ;

addrreg:
   ADDRREG subregnum
   {
      int subnr = 16;

      if ($2 > subnr)
         error(&@2, "Address sub register number %d"
               "out of range\n", $2);

      $$ = asm_make_base_reg(GEN_ARF, BRW_ARF_ADDRESS, $2);
   }
   ;

accreg:
   ACCREG subregnum
   {
      int nr_reg = 10;

      if ($1 > nr_reg)
         error(&@1, "Accumulator register number %d"
               " out of range\n", $1);

      $$ = asm_make_base_reg(GEN_ARF, BRW_ARF_ACCUMULATOR + $1, $2);
   }
   ;

flagreg:
   FLAGREG subregnum
   {
      // 2 flag reg
      int nr_reg = 2;
      int subnr = nr_reg;

      if ($1 > nr_reg)
         error(&@1, "Flag register number %d"
               " out of range \n", $1);
      if ($2 > subnr)
         error(&@2, "Flag subregister number %d"
               " out of range\n", $2);

      $$ = asm_make_base_reg(GEN_ARF, BRW_ARF_FLAG | $1, $2);
   }
   ;

maskreg:
   MASKREG subregnum
   {
      if ($1 > 0)
         error(&@1, "Mask register number %d"
               " out of range\n", $1);

      $$ = asm_make_base_reg(GEN_ARF, BRW_ARF_MASK, $2);
   }
   ;

notifyreg:
   NOTIFYREG subregnum
   {
      int subnr = (parser->devinfo->ver >= 11) ? 2 : 3;
      if ($2 > subnr)
         error(&@2, "Notification sub register number %d"
               " out of range\n", $2);

      $$ = asm_make_base_reg(GEN_ARF, BRW_ARF_NOTIFICATION_COUNT, $2);
   }
   ;

scalarreg:
   SCALARREG subregnum
   {
      if ($2 > 31)
         error(&@2, "Scalar sub register number %d"
                    " out of range\n", $2);

      $$ = asm_make_base_reg(GEN_ARF, BRW_ARF_SCALAR, $2);
   }
   ;

statereg:
   STATEREG subregnum
   {
      if ($1 > 2)
         error(&@1, "State register number %d"
               " out of range\n", $1);

      if ($2 > 4)
         error(&@2, "State sub register number %d"
               " out of range\n", $2);

      $$ = asm_make_base_reg(GEN_ARF, BRW_ARF_STATE, $2);
   }
   ;

controlreg:
   CONTROLREG subregnum
   {
      if ($2 > 3)
         error(&@2, "control sub register number %d"
               " out of range\n", $2);

      $$ = asm_make_base_reg(GEN_ARF, BRW_ARF_CONTROL, $2);
   }
   ;

ipreg:
   IPREG      { $$ = asm_make_base_reg(GEN_ARF, BRW_ARF_IP, 0); }
   ;

nullreg:
   NULL_TOKEN    { $$ = asm_make_base_reg(GEN_ARF, BRW_ARF_NULL, 0); }
   ;

threadcontrolreg:
   THREADREG subregnum
   {
      if ($2 > 7)
         error(&@2, "Thread control sub register number %d"
               " out of range\n", $2);

      $$ = asm_make_base_reg(GEN_ARF, BRW_ARF_TDR, $2);
   }
   ;

performancereg:
   PERFORMANCEREG subregnum
   {
      int subnr;
      if (parser->devinfo->ver >= 10)
         subnr = 5;
      else
         subnr = 4;

      if ($2 > subnr)
         error(&@2, "Performance sub register number %d"
               " out of range\n", $2);

      $$ = asm_make_base_reg(GEN_ARF, BRW_ARF_TIMESTAMP, $2);
   }
   ;

channelenablereg:
   CHANNELENABLEREG subregnum
   {
      if ($1 > 0)
         error(&@1, "Channel enable register number %d"
               " out of range\n", $1);

      $$ = asm_make_base_reg(GEN_ARF, BRW_ARF_MASK, $2);
   }
   ;

/* Immediate values */
immval:
   exp2
   {
      $$ = $1;
   }
   | LSQUARE exp2 COMMA exp2 COMMA exp2 COMMA exp2 RSQUARE
   {
      $$ = ($2 << 0) | ($4 << 8) | ($6 << 16) | ($8 << 24);
   }
   ;

/* Regions */
dstregion:
   /* empty */
   {
      $$ = 1;
   }
   | LANGLE exp RANGLE
   {
      if ($2 != 0 && ($2 > 4 || !isPowerofTwo($2)))
         error(&@2, "Invalid Horizontal stride %d\n", $2);

      $$ = $2;
   }
   ;

indirectregion:
   region
   | region_wh
   ;

region:
   /* empty */
   {
      $$ = asm_make_region(0, 1, 0);
   }
   | LANGLE exp RANGLE
   {
      if ($2 != 0 && ($2 > 32 || !isPowerofTwo($2)))
         error(&@2, "Invalid VertStride %d\n", $2);

      $$ = asm_make_region($2, 1, 0);
   }
   | LANGLE exp COMMA exp COMMA exp RANGLE
   {

      if ($2 != 0 && ($2 > 32 || !isPowerofTwo($2)))
         error(&@2, "Invalid VertStride %d\n", $2);

      if ($4 > 16 || !isPowerofTwo($4))
         error(&@4, "Invalid width %d\n", $4);

      if ($6 != 0 && ($6 > 4 || !isPowerofTwo($6)))
         error(&@6, "Invalid Horizontal stride in"
               "  region_wh %d\n", $6);

      $$ = asm_make_region($2, $4, $6);
   }
   | LANGLE exp SEMICOLON exp COMMA exp RANGLE
   {
      if ($2 != 0 && ($2 > 32 || !isPowerofTwo($2)))
         error(&@2, "Invalid VertStride %d\n", $2);

      if ($4 > 16 || !isPowerofTwo($4))
         error(&@4, "Invalid width %d\n", $4);

      if ($6 != 0 && ($6 > 4 || !isPowerofTwo($6)))
         error(&@6, "Invalid Horizontal stride in"
               " region_wh %d\n", $6);

      $$ = asm_make_region($2, $4, $6);
   }
   | LANGLE VxH COMMA exp COMMA exp RANGLE
   {
      if ($4 > 16 || !isPowerofTwo($4))
         error(&@4, "Invalid width %d\n", $4);

      if ($6 != 0 && ($6 > 4 || !isPowerofTwo($6)))
         error(&@6, "Invalid Horizontal stride in"
               " region_wh %d\n", $6);

      $$ = asm_make_region(BRW_VERTICAL_STRIDE_ONE_DIMENSIONAL, $4, $6);
   }
   ;

region_wh:
   LANGLE exp COMMA exp RANGLE
   {
      if ($2 > 16 || !isPowerofTwo($2))
         error(&@2, "Invalid width %d\n", $2);

      if ($4 != 0 && ($4 > 4 || !isPowerofTwo($4)))
         error(&@4, "Invalid Horizontal stride in"
               " region_wh %d\n", $4);

      $$ = asm_make_region(BRW_VERTICAL_STRIDE_ONE_DIMENSIONAL, $2, $4);
   }
   ;

reg_type:
     TYPE_F    { $$ = BRW_TYPE_F;  }
   | TYPE_UD   { $$ = BRW_TYPE_UD; }
   | TYPE_D    { $$ = BRW_TYPE_D;  }
   | TYPE_UW   { $$ = BRW_TYPE_UW; }
   | TYPE_W    { $$ = BRW_TYPE_W;  }
   | TYPE_UB   { $$ = BRW_TYPE_UB; }
   | TYPE_B    { $$ = BRW_TYPE_B;  }
   | TYPE_DF   { $$ = BRW_TYPE_DF; }
   | TYPE_UQ   { $$ = BRW_TYPE_UQ; }
   | TYPE_Q    { $$ = BRW_TYPE_Q;  }
   | TYPE_HF   { $$ = BRW_TYPE_HF; }
   | TYPE_BF   { $$ = BRW_TYPE_BF; }
   | TYPE_HF8  { $$ = BRW_TYPE_HF8; }
   | TYPE_BF8  { $$ = BRW_TYPE_BF8; }
   ;

imm_type:
   reg_type    { $$ = $1; }
   | TYPE_V    { $$ = BRW_TYPE_V;  }
   | TYPE_VF   { $$ = BRW_TYPE_VF; }
   | TYPE_UV   { $$ = BRW_TYPE_UV; }
   ;

writemask:
   /* empty */
   {
      $$ = WRITEMASK_XYZW;
   }
   | DOT writemask_x writemask_y writemask_z writemask_w
   {
      $$ = $2 | $3 | $4 | $5;
   }
   ;

writemask_x:
   /* empty */    { $$ = 0; }
   | X            { $$ = 1 << BRW_CHANNEL_X; }
   ;

writemask_y:
   /* empty */    { $$ = 0; }
   | Y            { $$ = 1 << BRW_CHANNEL_Y; }
   ;

writemask_z:
   /* empty */    { $$ = 0; }
   | Z            { $$ = 1 << BRW_CHANNEL_Z; }
   ;

writemask_w:
   /* empty */    { $$ = 0; }
   | W            { $$ = 1 << BRW_CHANNEL_W; }
   ;

swizzle:
   /* empty */
   {
      $$ = BRW_SWIZZLE_NOOP;
   }
   | DOT chansel
   {
      $$ = BRW_SWIZZLE4($2, $2, $2, $2);
   }
   | DOT chansel chansel chansel chansel
   {
      $$ = BRW_SWIZZLE4($2, $3, $4, $5);
   }
   ;

chansel:
   X
   | Y
   | Z
   | W
   ;

/* Instruction prediction and modifiers */
predicate:
   /* empty */
   {
      $$.pred_control = BRW_PREDICATE_NONE;
      $$.pred_inv = 0;
      $$.flag_reg_nr = 0;
      $$.flag_subreg_nr = 0;
   }
   | LPAREN predstate flagreg predctrl RPAREN
   {
      $$.pred_inv = $2;
      $$.flag_reg_nr = $3.nr;
      $$.flag_subreg_nr = $3.subnr;
      $$.pred_control = $4;
   }
   ;

predstate:
   /* empty */     { $$ = 0; }
   | PLUS          { $$ = 0; }
   | MINUS         { $$ = 1; }
   ;

predctrl:
   /* empty */    { $$ = BRW_PREDICATE_NORMAL; }
   | DOT X        { $$ = BRW_PREDICATE_ALIGN16_REPLICATE_X; }
   | DOT Y        { $$ = BRW_PREDICATE_ALIGN16_REPLICATE_Y; }
   | DOT Z        { $$ = BRW_PREDICATE_ALIGN16_REPLICATE_Z; }
   | DOT W        { $$ = BRW_PREDICATE_ALIGN16_REPLICATE_W; }
   | ANYV
   | ALLV
   | ANY2H
   | ALL2H
   | ANY4H
   | ALL4H
   | ANY8H
   | ALL8H
   | ANY16H
   | ALL16H
   | ANY32H
   | ALL32H
   ;

/* Source Modification */
negate:
   /* empty */   { $$ = 0; }
   | MINUS       { $$ = 1; }
   ;

abs:
   /* empty */   { $$ = 0; }
   | ABS         { $$ = 1; }
   ;

/* Flag (Conditional) Modifier */
cond_mod:
   condModifiers
   {
      $$.cond_modifier = $1;
      $$.flag_reg_nr = 0;
      $$.flag_subreg_nr = 0;
   }
   | condModifiers DOT flagreg
   {
      $$.cond_modifier = $1;
      $$.flag_reg_nr = $3.nr;
      $$.flag_subreg_nr = $3.subnr;
   }
   ;

condModifiers:
   /* empty */    { $$ = BRW_CONDITIONAL_NONE; }
   | ZERO
   | EQUAL
   | NOT_ZERO
   | NOT_EQUAL
   | GREATER
   | GREATER_EQUAL
   | LESS
   | LESS_EQUAL
   | OVERFLOW
   | ROUND_INCREMENT
   | UNORDERED
   ;

/* message details for send */
msgdesc:
   MSGDESC_BEGIN msgdesc_parts MSGDESC_END { $$ = $2; }
   ;

msgdesc_parts:
   SRC1_LEN ASSIGN INTEGER msgdesc_parts
   {
      $$ = $4;
      $$.src1_len = $3;
   }
   | EX_BSO msgdesc_parts
   {
      $$ = $2;
      $$.ex_bso = 1;
   }
   | INTEGER msgdesc_parts { $$ = $2; }
   | ASSIGN msgdesc_parts { $$ = $2; }
   | /* empty */
   {
      memset(&$$, 0, sizeof($$));
   }
   ;

saturate:
   /* empty */   { $$ = BRW_INSTRUCTION_NORMAL; }
   | SATURATE    { $$ = BRW_INSTRUCTION_SATURATE; }
   ;

/* Execution size */
execsize:
   /* empty */ %prec EMPTYEXECSIZE
   {
      $$ = 1;
   }
   | LPAREN exp2 RPAREN
   {
      if ($2 > 32 || !isPowerofTwo($2))
         error(&@2, "Invalid execution size %llu\n", $2);

      $$ = $2;
   }
   ;

/* Instruction options */
instoptions:
   /* empty */
   {
      memset(&$$, 0, sizeof($$));
   }
   | LCURLY instoption_list RCURLY
   {
      memset(&$$, 0, sizeof($$));
      $$ = $2;
   }
   ;

instoption_list:
   instoption_list COMMA instoption
   {
      memset(&$$, 0, sizeof($$));
      $$ = $1;
      add_instruction_option(parser, &$$, $3);
   }
   | instoption_list instoption
   {
      memset(&$$, 0, sizeof($$));
      $$ = $1;
      add_instruction_option(parser, &$$, $2);
   }
   | /* empty */
   {
      memset(&$$, 0, sizeof($$));
   }
   ;

depinfo:
   REG_DIST_CURRENT
   {
      memset(&$$, 0, sizeof($$));
      $$.regdist = $1;
      $$.pipe = TGL_PIPE_NONE;
   }
   | REG_DIST_FLOAT
   {
      memset(&$$, 0, sizeof($$));
      $$.regdist = $1;
      $$.pipe = TGL_PIPE_FLOAT;
   }
   | REG_DIST_INT
   {
      memset(&$$, 0, sizeof($$));
      $$.regdist = $1;
      $$.pipe = TGL_PIPE_INT;
   }
   | REG_DIST_LONG
   {
      memset(&$$, 0, sizeof($$));
      $$.regdist = $1;
      $$.pipe = TGL_PIPE_LONG;
   }
   | REG_DIST_ALL
   {
      memset(&$$, 0, sizeof($$));
      $$.regdist = $1;
      $$.pipe = TGL_PIPE_ALL;
   }
   | REG_DIST_MATH
   {
      memset(&$$, 0, sizeof($$));
      $$.regdist = $1;
      $$.pipe = TGL_PIPE_MATH;
   }
   | REG_DIST_SCALAR
   {
      memset(&$$, 0, sizeof($$));
      $$.regdist = $1;
      $$.pipe = TGL_PIPE_SCALAR;
   }
   | SBID_ALLOC
   {
      memset(&$$, 0, sizeof($$));
      $$.sbid = $1;
      $$.mode = TGL_SBID_SET;
   }
   | SBID_WAIT_SRC
   {
      memset(&$$, 0, sizeof($$));
      $$.sbid = $1;
      $$.mode = TGL_SBID_SRC;
   }
   | SBID_WAIT_DST
   {
      memset(&$$, 0, sizeof($$));
      $$.sbid = $1;
      $$.mode = TGL_SBID_DST;
   }

instoption:
   ALIGN1          { $$.type = INSTOPTION_FLAG; $$.uint_value = ALIGN1;}
   | ALIGN16       { $$.type = INSTOPTION_FLAG; $$.uint_value = ALIGN16; }
   | ACCWREN
   {
      if (parser->devinfo->ver >= 20)
         error(&@1, "AccWrEnable not supported in Xe2+\n");
      $$.type = INSTOPTION_FLAG;
      $$.uint_value = ACCWREN;
   }
   | BREAKPOINT    { $$.type = INSTOPTION_FLAG; $$.uint_value = BREAKPOINT; }
   | NODDCLR       { $$.type = INSTOPTION_FLAG; $$.uint_value = NODDCLR; }
   | NODDCHK       { $$.type = INSTOPTION_FLAG; $$.uint_value = NODDCHK; }
   | MASK_DISABLE  { $$.type = INSTOPTION_FLAG; $$.uint_value = MASK_DISABLE; }
   | EOT           { $$.type = INSTOPTION_FLAG; $$.uint_value = EOT; }
   | SWITCH        { $$.type = INSTOPTION_FLAG; $$.uint_value = SWITCH; }
   | ATOMIC        { $$.type = INSTOPTION_FLAG; $$.uint_value = ATOMIC; }
   | BRANCH_CTRL   { $$.type = INSTOPTION_FLAG; $$.uint_value = BRANCH_CTRL; }
   | CMPTCTRL      { $$.type = INSTOPTION_FLAG; $$.uint_value = CMPTCTRL; }
   | WECTRL        { $$.type = INSTOPTION_FLAG; $$.uint_value = WECTRL; }
   | QTR_2Q        { $$.type = INSTOPTION_CHAN_OFFSET; $$.uint_value = 8; }
   | QTR_3Q        { $$.type = INSTOPTION_CHAN_OFFSET; $$.uint_value = 16; }
   | QTR_4Q        { $$.type = INSTOPTION_CHAN_OFFSET; $$.uint_value = 24; }
   | QTR_2H        { $$.type = INSTOPTION_CHAN_OFFSET; $$.uint_value = 16; }
   | QTR_2N
   {
      if (parser->devinfo->ver >= 20)
         error(&@1, "Channel offset must be multiple of 8 in Xe2+\n");
      $$.type = INSTOPTION_CHAN_OFFSET;
      $$.uint_value = 4;
   }
   | QTR_3N        { $$.type = INSTOPTION_CHAN_OFFSET; $$.uint_value = 8; }
   | QTR_4N
   {
      if (parser->devinfo->ver >= 20)
         error(&@1, "Channel offset must be multiple of 8 in Xe2+\n");
      $$.type = INSTOPTION_CHAN_OFFSET; $$.uint_value = 12;
   }
   | QTR_5N        { $$.type = INSTOPTION_CHAN_OFFSET; $$.uint_value = 16; }
   | QTR_6N
   {
      if (parser->devinfo->ver >= 20)
         error(&@1, "Channel offset must be multiple of 8 in Xe2+\n");
      $$.type = INSTOPTION_CHAN_OFFSET; $$.uint_value = 20;
   }
   | QTR_7N        { $$.type = INSTOPTION_CHAN_OFFSET; $$.uint_value = 24; }
   | QTR_8N
   {
      if (parser->devinfo->ver >= 20)
         error(&@1, "Channel offset must be multiple of 8 in Xe2+\n");
      $$.type = INSTOPTION_CHAN_OFFSET; $$.uint_value = 28;
   }
   | depinfo       { $$.type = INSTOPTION_DEP_INFO; $$.depinfo_value = $1; }
   ;

%%

void
yyerror(YYLTYPE *loc, struct brw_asm_parser *parser, const char *msg)
{
   fprintf(stderr, "%s: %d: %s at \"%s\"\n",
           parser->input_filename, loc->first_line, msg,
           brw_asm_get_text(parser->scanner));
   ++parser->errors;
}
