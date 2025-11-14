/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <array>
#include <stdio.h>
#include <string.h>

#include "gen_private.h"

#define WIDTH(width)   (1 << (width))

inline unsigned
ENCODE_VSTRIDE(unsigned value)
{
   if (value == BRW_VERTICAL_STRIDE_ONE_DIMENSIONAL)
      return value;
   else
      return cvt(value);
}

inline unsigned
DECODE_VSTRIDE(unsigned raw_value)
{
   if (raw_value == BRW_VERTICAL_STRIDE_ONE_DIMENSIONAL)
      return raw_value;
   else
      return STRIDE(raw_value);
}

#define FIELD(name, high, low) \
   static constexpr gen_range name = { .hi = high, .lo = low };

#define SUB_FIELD(name, high, low) \
   static constexpr gen_sub_range name = { .hi = high, .lo = low };

FIELD(HW_OPCODE,          6,  0);
FIELD(CONTROLS,          31,  8);
FIELD(OPERAND_CONTROLS,  63, 32);
FIELD(SOURCES,          127, 64);

FIELD(DEBUG_CONTROL,     30, 30);

/* EU_INSTRUCTION_BRANCH_* structures. */
FIELD(BRANCH_SRC1_FILE, 90, 89);
FIELD(BRANCH_SRC1_TYPE, 94, 91);
FIELD(BRANCH_UIP,       95, 64);
FIELD(BRANCH_JIP,      127, 96);

/* EU_INSTRUCTION_SEND and EU_INSTRUCTION_SENDS. */
FIELD(SEND_CONTROLS_A,     23,  8);
FIELD(SEND_SFID,           27, 24);
FIELD(SEND_CONTROLS_B,     31, 28);
FIELD(SEND_EX_DESC_IS_REG, 61, 61);
FIELD(SEND_DESC_IS_REG,    77, 77);
FIELD(SEND_MSG,           127, 96);

FIELD(SENDS_FLAG_SUBNR,         32, 32);
FIELD(SENDS_FLAG_NR,            33, 33);
FIELD(SENDS_NO_MASK,            34, 34);
FIELD(SENDS_DST_FILE,           35, 35);
FIELD(SENDS_SRC1_FILE,          36, 36);
FIELD(SENDS_DST_TYPE,           40, 37);
FIELD(SENDS_SRC1_NR,            51, 44);
FIELD(SENDS_DST_SUBNR,          52, 52);
FIELD(SENDS_DST_NR,             60, 53);
FIELD(SENDS_SRC0_SUBNR,         68, 68);
FIELD(SENDS_SRC0_NR,            76, 69);
FIELD(SENDS_SRC0_ADDR_MODE,     79, 79);

FIELD(SENDS_DST_ADDR_IMM,       56, 52);
FIELD(SENDS_DST_ADDR_SUBNR,     60, 57);
FIELD(SENDS_DST_ADDR_IMM_SIGN,  62, 62);
FIELD(SENDS_DST_ADDR_MODE,      63, 63);

FIELD(SENDS_SRC0_ADDR_IMM_SIGN, 78, 78);
FIELD(SENDS_SRC0_ADDR_SUBNR,    76, 73);
FIELD(SENDS_SRC0_ADDR_IMM,      72, 68);

/* EU_INSTRUCTION_BASIC_THREE_SRC. */
FIELD(THREE_FLAG_SUBNR,      32,  32);
FIELD(THREE_FLAG_NR,         33,  33);
FIELD(THREE_NO_MASK,         34,  34);
FIELD(THREE_SRC2_TYPE,       35,  35);
FIELD(THREE_SRC1_TYPE,       36,  36);
FIELD(THREE_SRC0_ABS,        37,  37);
FIELD(THREE_SRC0_NEGATE,     38,  38);
FIELD(THREE_SRC1_ABS,        39,  39);
FIELD(THREE_SRC1_NEGATE,     40,  40);
FIELD(THREE_SRC2_ABS,        41,  41);
FIELD(THREE_SRC2_NEGATE,     42,  42);
FIELD(THREE_SRC_TYPE,        45,  43);
FIELD(THREE_DST_TYPE,        48,  46);
FIELD(THREE_DST_WRITEMASK,   52,  49);
FIELD(THREE_DST_SUBNR,       55,  53);
FIELD(THREE_DST_NR,          63,  56);
FIELD(THREE_SRC0_OPERAND,    84,  64);
FIELD(THREE_SRC1_OPERAND,   105,  85);
FIELD(THREE_SRC2_OPERAND,   126, 106);

/* EU_INSTRUCTION_ALIGN1_THREE_SRC (Gfx11). */
FIELD(THREE_A1_EXECUTION_TYPE,  35,  35);
FIELD(THREE_A1_DST_FILE,        36,  36);
FIELD(THREE_A1_SRC0_FILE,       43,  43);
FIELD(THREE_A1_SRC1_FILE,       44,  44);
FIELD(THREE_A1_SRC2_FILE,       45,  45);
FIELD(THREE_A1_DST_HSTRIDE,     49,  49);
FIELD(THREE_A1_SRC0_TYPE,       66,  64);
FIELD(THREE_A1_SRC0_VSTRIDE,    68,  67);
FIELD(THREE_A1_SRC0_HSTRIDE,    70,  69);
FIELD(THREE_A1_SRC0_SUBNR,      75,  71);
FIELD(THREE_A1_SRC0_NR,         83,  76);
FIELD(THREE_A1_SRC1_TYPE,       87,  85);
FIELD(THREE_A1_SRC1_VSTRIDE,    89,  88);
FIELD(THREE_A1_SRC1_HSTRIDE,    91,  90);
FIELD(THREE_A1_SRC1_SUBNR,      96,  92);
FIELD(THREE_A1_SRC1_NR,        104,  97);
FIELD(THREE_A1_SRC2_TYPE,      108, 106);
FIELD(THREE_A1_SRC2_HSTRIDE,   112, 111);
FIELD(THREE_A1_SRC2_SUBNR,     117, 113);
FIELD(THREE_A1_SRC2_NR,        125, 118);

FIELD(THREE_A1_SRC0_IMM,        82,  67);
FIELD(THREE_A1_SRC2_IMM,       124, 109);

/* Relative to EU_INSTRUCTION_SOURCES_* sub-structures. */
SUB_FIELD(SRC0_OPERAND,       24,  0);
SUB_FIELD(SRC1_FILE,          26, 25);
SUB_FIELD(SRC1_TYPE,          30, 27);
SUB_FIELD(SRC1_OPERAND,       56, 32);

SUB_FIELD(SRC0_ADDR_IMM_SIGN, 31, 31);
SUB_FIELD(SRC1_ADDR_IMM_SIGN, 57, 57);

SUB_FIELD(IMM_32,             63, 32);
SUB_FIELD(IMM_64,             63,  0);

/* Relative to EU_INSTRUCTION_CONTROLS_A sub-structure. */
SUB_FIELD(ACCESS_MODE,     0,  0);
SUB_FIELD(NO_DD_CLEAR,     1,  1);
SUB_FIELD(NO_DD_CHECK,     2,  2);
SUB_FIELD(NIB_CONTROL,     3,  3);
SUB_FIELD(QTR_CONTROL,     5,  4);
SUB_FIELD(THREAD_CONTROL,  7,  6);
SUB_FIELD(PRED_CONTROL,   11,  8);
SUB_FIELD(PRED_INV,       12, 12);
SUB_FIELD(EXEC_SIZE,      15, 13);

/* Relative to EU_INSTRUCTION_CONTROLS_B sub-structure. */
SUB_FIELD(BRANCH_CONTROL, 0, 0);
SUB_FIELD(ACC_WR_CONTROL, 0, 0);
SUB_FIELD(SATURATE,       3, 3);

/* Relative to EU_INSTRUCTION_OPERAND_CONTROLS sub-structure. */
SUB_FIELD(FLAG_SUBNR,         0,  0);
SUB_FIELD(FLAG_NR,            1,  1);
SUB_FIELD(NO_MASK,            2,  2);
SUB_FIELD(DST_FILE,           4,  3);
SUB_FIELD(DST_TYPE,           8,  5);
SUB_FIELD(SRC0_FILE,         10,  9);
SUB_FIELD(SRC0_TYPE,         14, 11);
SUB_FIELD(DST_ADDR_IMM_SIGN, 15, 15);
SUB_FIELD(DST_OPERAND,       31, 16);

/* Relative to EU_INSTRUCTION_OPERAND_SEND_MSG sub-structure. */
SUB_FIELD(SEND_DESC_IMM, 30,  0);
SUB_FIELD(SEND_EOT,      31, 31);

/* Relative to EU_INSTRUCTION_OPERAND_SRC_REG_* sub-structures. */
SUB_FIELD(SRC_NR,               12,  5);
SUB_FIELD(SRC_ABS,              13, 13);
SUB_FIELD(SRC_NEGATE,           14, 14);
SUB_FIELD(SRC_ADDRESS_MODE,     15, 15);
SUB_FIELD(SRC_VSTRIDE,          24, 21);
SUB_FIELD(SRC_ADDR_SUBNR,       12,  9);

SUB_FIELD(SRC_A1_ADDR_IMM,       8,  0);
SUB_FIELD(SRC_A1_SUBNR,          4,  0);
SUB_FIELD(SRC_A1_HSTRIDE,       17, 16);
SUB_FIELD(SRC_A1_WIDTH,         20, 18);

SUB_FIELD(SRC_A16_SWIZZLE_LO,    3,  0);
SUB_FIELD(SRC_A16_SUBNR,         4,  4);
SUB_FIELD(SRC_A16_ADDR_IMM,      8,  4);
SUB_FIELD(SRC_A16_SWIZZLE_HI,   19, 16);

/* Relative to EU_INSTRUCTION_OPERAND_DST_* sub-structures. */
SUB_FIELD(DST_ADDR_MODE,       15, 15);
SUB_FIELD(DST_NR,              12,  5);
SUB_FIELD(DST_ADDR_SUBNR,      12,  9);

SUB_FIELD(DST_A1_SUBNR,         4,  0);
SUB_FIELD(DST_A1_HSTRIDE,      14, 13);

SUB_FIELD(DST_A1_ADDR_IMM,      8,  0);

SUB_FIELD(DST_A16_WRITEMASK,    3,  0);
SUB_FIELD(DST_A16_SUBNR,        4,  4);

SUB_FIELD(DST_A16_ADDR_IMM,     8,  4);

/* Relative to EU_INSTRUCTION_OPERAND_SRC_REG_THREE_SRC sub-structure. */
SUB_FIELD(THREE_SRC_REP_CTRL,     0,  0);
SUB_FIELD(THREE_SRC_SWIZZLE,      8,  1);
SUB_FIELD(THREE_SRC_SUBNR,       11,  9);
SUB_FIELD(THREE_SRC_NR,          19, 12);
SUB_FIELD(THREE_SRC_SUBNR_EXTRA, 20, 20);

/* Relative to EU_INSTRUCTION_CONTROLS sub-structure. */
SUB_FIELD(CONTROLS_A,    15,  0);
SUB_FIELD(COND_MODIFIER, 19, 16);
SUB_FIELD(CONTROLS_B,    23, 20);



// TODO: HERE: Not sure.not sure.
struct gen_inst_description {
   gen_opcode gen_op = GEN_OP_ILLEGAL;
   gen_format format = GEN_FORMAT_ILLEGAL;
   unsigned hw_opcode = 0;
   bool has_dst = false;

   constexpr gen_inst_description() = default;

   constexpr gen_inst_description(gen_opcode op, unsigned hw)
      : gen_op(op),
        format(gen_inst_format(op)),
        hw_opcode(hw),
        has_dst(gen_inst_has_dst(format, op))
   {}
};

static constexpr std::array<gen_inst_description, 128> gen_to_description = []() constexpr {
   std::array<gen_inst_description, 128> r;
   r[GEN_OP_ILLEGAL]  = gen_inst_description(GEN_OP_ILLEGAL, 0);
   r[GEN_OP_ADD]      = gen_inst_description(GEN_OP_ADD, 64);
   r[GEN_OP_ADDC]     = gen_inst_description(GEN_OP_ADDC, 78);
   r[GEN_OP_AND]      = gen_inst_description(GEN_OP_AND, 5);
   r[GEN_OP_ASR]      = gen_inst_description(GEN_OP_ASR, 12);
   r[GEN_OP_AVG]      = gen_inst_description(GEN_OP_AVG, 66);
   r[GEN_OP_BFE]      = gen_inst_description(GEN_OP_BFE, 24);
   r[GEN_OP_BFI1]     = gen_inst_description(GEN_OP_BFI1, 25);
   r[GEN_OP_BFI2]     = gen_inst_description(GEN_OP_BFI2, 26);
   r[GEN_OP_BFREV]    = gen_inst_description(GEN_OP_BFREV, 23);
   r[GEN_OP_BRC]      = gen_inst_description(GEN_OP_BRC, 35);
   r[GEN_OP_BRD]      = gen_inst_description(GEN_OP_BRD, 33);
   r[GEN_OP_BREAK]    = gen_inst_description(GEN_OP_BREAK, 40);
   r[GEN_OP_CALL]     = gen_inst_description(GEN_OP_CALL, 44);
   r[GEN_OP_CALLA]    = gen_inst_description(GEN_OP_CALLA, 43);
   r[GEN_OP_CBIT]     = gen_inst_description(GEN_OP_CBIT, 77);
   r[GEN_OP_CMP]      = gen_inst_description(GEN_OP_CMP, 16);
   r[GEN_OP_CMPN]     = gen_inst_description(GEN_OP_CMPN, 17);
   r[GEN_OP_CONTINUE] = gen_inst_description(GEN_OP_CONTINUE, 41);
   r[GEN_OP_CSEL]     = gen_inst_description(GEN_OP_CSEL, 18);
   r[GEN_OP_DP2]      = gen_inst_description(GEN_OP_DP2, 87);
   r[GEN_OP_DP3]      = gen_inst_description(GEN_OP_DP3, 86);
   r[GEN_OP_DP4]      = gen_inst_description(GEN_OP_DP4, 84);
   r[GEN_OP_DPH]      = gen_inst_description(GEN_OP_DPH, 85);
   r[GEN_OP_ELSE]     = gen_inst_description(GEN_OP_ELSE, 36);
   r[GEN_OP_ENDIF]    = gen_inst_description(GEN_OP_ENDIF, 37);
   r[GEN_OP_FBH]      = gen_inst_description(GEN_OP_FBH, 75);
   r[GEN_OP_FBL]      = gen_inst_description(GEN_OP_FBL, 76);
   r[GEN_OP_FRC]      = gen_inst_description(GEN_OP_FRC, 67);
   r[GEN_OP_GOTO]     = gen_inst_description(GEN_OP_GOTO, 46);
   r[GEN_OP_HALT]     = gen_inst_description(GEN_OP_HALT, 42);
   r[GEN_OP_IF]       = gen_inst_description(GEN_OP_IF, 34);
   r[GEN_OP_JMPI]     = gen_inst_description(GEN_OP_JMPI, 32);
   r[GEN_OP_JOIN]     = gen_inst_description(GEN_OP_JOIN, 47);
   r[GEN_OP_LINE]     = gen_inst_description(GEN_OP_LINE, 89);
   r[GEN_OP_LRP]      = gen_inst_description(GEN_OP_LRP, 92);
   r[GEN_OP_LZD]      = gen_inst_description(GEN_OP_LZD, 74);
   r[GEN_OP_MAC]      = gen_inst_description(GEN_OP_MAC, 72);
   r[GEN_OP_MACH]     = gen_inst_description(GEN_OP_MACH, 73);
   r[GEN_OP_MAD]      = gen_inst_description(GEN_OP_MAD, 91);
   r[GEN_OP_MADM]     = gen_inst_description(GEN_OP_MADM, 93);
   r[GEN_OP_MATH]     = gen_inst_description(GEN_OP_MATH, 56);
   r[GEN_OP_MOV]      = gen_inst_description(GEN_OP_MOV, 1);
   r[GEN_OP_MOVI]     = gen_inst_description(GEN_OP_MOVI, 3);
   r[GEN_OP_MUL]      = gen_inst_description(GEN_OP_MUL, 65);
   r[GEN_OP_NOP]      = gen_inst_description(GEN_OP_NOP, 126);
   r[GEN_OP_NOT]      = gen_inst_description(GEN_OP_NOT, 4);
   r[GEN_OP_OR]       = gen_inst_description(GEN_OP_OR, 6);
   r[GEN_OP_PLN]      = gen_inst_description(GEN_OP_PLN, 90);
   r[GEN_OP_RET]      = gen_inst_description(GEN_OP_RET, 45);
   r[GEN_OP_RNDD]     = gen_inst_description(GEN_OP_RNDD, 69);
   r[GEN_OP_RNDE]     = gen_inst_description(GEN_OP_RNDE, 70);
   r[GEN_OP_RNDU]     = gen_inst_description(GEN_OP_RNDU, 68);
   r[GEN_OP_RNDZ]     = gen_inst_description(GEN_OP_RNDZ, 71);
   r[GEN_OP_ROL]      = gen_inst_description(GEN_OP_ROL, 15);
   r[GEN_OP_ROR]      = gen_inst_description(GEN_OP_ROR, 14);
   r[GEN_OP_SEL]      = gen_inst_description(GEN_OP_SEL, 2);
   r[GEN_OP_SEND]     = gen_inst_description(GEN_OP_SEND, 49);
   r[GEN_OP_SENDC]    = gen_inst_description(GEN_OP_SENDC, 50);
   r[GEN_OP_SENDS]    = gen_inst_description(GEN_OP_SENDS, 51);
   r[GEN_OP_SENDSC]   = gen_inst_description(GEN_OP_SENDSC, 52);
   r[GEN_OP_SHL]      = gen_inst_description(GEN_OP_SHL, 9);
   r[GEN_OP_SHR]      = gen_inst_description(GEN_OP_SHR, 8);
   r[GEN_OP_SMOV]     = gen_inst_description(GEN_OP_SMOV, 10);
   r[GEN_OP_SUBB]     = gen_inst_description(GEN_OP_SUBB, 79);
   r[GEN_OP_WAIT]     = gen_inst_description(GEN_OP_WAIT, 48);
   r[GEN_OP_WHILE]    = gen_inst_description(GEN_OP_WHILE, 39);
   r[GEN_OP_XOR]      = gen_inst_description(GEN_OP_XOR, 7);
   return r;
}();

static constexpr std::array<gen_inst_description, 128> hw_to_description = []() constexpr {
   std::array<gen_inst_description, 128> r;
   for (const auto &d : gen_to_description)
      r[d.hw_opcode] = d;
   return r;
}();

struct gen_encoder_pre_xe {
   const intel_device_info *devinfo;

   const gen_inst *inst;
   gen_raw_inst *raw;
   const gen_inst_description *desc;

   gen_encoder_pre_xe(const intel_device_info *devinfo)
      : devinfo(devinfo)
   {}

   void
   encode(const gen_inst *inst, gen_raw_inst *raw)
   {
      this->inst = inst;
      this->raw = raw;
      this->desc = &gen_to_description[inst->opcode];

      memset(raw, 0, sizeof(gen_raw_inst));

      gen_range bits = { 127, 0 };

      set(HW_OPCODE,     desc->hw_opcode);
      set(DEBUG_CONTROL, inst->debug_control);

      switch (desc->format) {
      case GEN_FORMAT_BASIC_ONE_SRC:
      case GEN_FORMAT_BASIC_TWO_SRC: {
         encode_controls();
         encode_operand_controls();
         encode_sources();
         break;
      }

      case GEN_FORMAT_BASIC_THREE_SRC:
         encode_controls();

         set(THREE_FLAG_SUBNR, inst->flag_subnr);
         set(THREE_FLAG_NR,    inst->flag_nr);
         set(THREE_NO_MASK,    inst->no_mask);

         if (gen_inst_has_source_modifier(desc->format, inst)) {
            set(THREE_SRC0_ABS,    inst->src[0].abs);
            set(THREE_SRC1_ABS,    inst->src[1].abs);
            set(THREE_SRC2_ABS,    inst->src[2].abs);
            set(THREE_SRC0_NEGATE, inst->src[0].negate);
            set(THREE_SRC1_NEGATE, inst->src[1].negate);
            set(THREE_SRC2_NEGATE, inst->src[2].negate);
         }

         if (inst->align16) {
            /* All sources must have the same type in align16 mode */
            brw_reg_type src_type = inst->src[0].type;

            set(THREE_SRC2_TYPE,     inst->src[2].type == BRW_TYPE_HF);
            set(THREE_SRC1_TYPE,     inst->src[1].type == BRW_TYPE_HF);
            set(THREE_SRC_TYPE,      encode_type_3src(src_type));
            set(THREE_DST_TYPE,      encode_type_3src(inst->dst.type));
            set(THREE_DST_WRITEMASK, inst->dst.writemask);
            set(THREE_DST_SUBNR,     inst->dst.subnr >> 2); // TODO: Double check.
            set(THREE_DST_NR,        inst->dst.nr);

            encode_operand_src_reg_three_src(THREE_SRC0_OPERAND, 0);
            encode_operand_src_reg_three_src(THREE_SRC1_OPERAND, 1);
            encode_operand_src_reg_three_src(THREE_SRC2_OPERAND, 2);

         } else {
            assert(devinfo->ver == 11);

            set(THREE_A1_EXECUTION_TYPE, brw_type_is_float_or_bfloat(inst->dst.type));
            set(THREE_A1_DST_FILE,       inst->dst.file    == GEN_IMM ? 1 : 0);
            set(THREE_A1_SRC0_FILE,      inst->src[0].file == GEN_IMM ? 1 : 0);
            set(THREE_A1_SRC1_FILE,      inst->src[1].file == GEN_IMM ? 1 : 0);
            set(THREE_A1_SRC2_FILE,      inst->src[2].file == GEN_IMM ? 1 : 0);

            set(THREE_DST_TYPE,       encode_type_3src(inst->dst.type));
            set(THREE_A1_DST_HSTRIDE, inst->dst.region.hstride != 1);
            set(THREE_DST_SUBNR,      inst->dst.subnr);
            set(THREE_DST_NR,         inst->dst.nr);

            set(THREE_A1_SRC0_TYPE, encode_type_3src(inst->src[0].type));
            if (inst->src[0].file == GEN_GRF) {
               set(THREE_A1_SRC0_VSTRIDE, ENCODE_VSTRIDE_3SRC(inst->src[0].region.vstride));
               set(THREE_A1_SRC0_HSTRIDE, cvt(inst->src[0].region.hstride));
               set(THREE_A1_SRC0_SUBNR,   inst->src[0].subnr);
               set(THREE_A1_SRC0_NR,      inst->src[0].nr);
            } else {
               assert(inst->src[0].file == GEN_IMM);
               set(THREE_A1_SRC0_IMM, inst->src[0].imm & 0xFFFF);
            }

            set(THREE_A1_SRC1_TYPE,    encode_type_3src(inst->src[1].type));
            set(THREE_A1_SRC1_VSTRIDE, ENCODE_VSTRIDE_3SRC(inst->src[1].region.vstride));
            set(THREE_A1_SRC1_HSTRIDE, cvt(inst->src[1].region.hstride));
            set(THREE_A1_SRC1_SUBNR,   inst->src[1].subnr);
            set(THREE_A1_SRC1_NR,      inst->src[1].nr);

            set(THREE_A1_SRC2_TYPE, encode_type_3src(inst->src[2].type));
            if (inst->src[2].file == GEN_GRF) {
               set(THREE_A1_SRC2_HSTRIDE, cvt(inst->src[2].region.hstride));
               set(THREE_A1_SRC2_SUBNR,   inst->src[2].subnr);
               set(THREE_A1_SRC2_NR,      inst->src[2].nr);
            } else {
               assert(inst->src[2].file == GEN_IMM);
               set(THREE_A1_SRC2_IMM, inst->src[2].imm & 0xFFFF);
            }
         }
         break;

      case GEN_FORMAT_SEND: {
         encode_controls_a(SEND_CONTROLS_A);

         set(SEND_SFID, inst->send.sfid);

         encode_controls_b(SEND_CONTROLS_B);

         set(SEND_DESC_IS_REG,    inst->send.desc_is_reg);
         set(SEND_EX_DESC_IS_REG, inst->send.ex_desc_is_reg);

         if (gen_inst_is_split_send(devinfo, inst)) {
            set(SENDS_FLAG_SUBNR,    inst->flag_subnr);
            set(SENDS_FLAG_NR,       inst->flag_nr);
            set(SENDS_NO_MASK,       inst->no_mask);

            set(SENDS_DST_FILE,      encode_file(inst->dst.file));
            set(SENDS_DST_ADDR_MODE, inst->dst.indirect);
            set(SENDS_DST_TYPE,      encode_type(inst->dst.file, inst->dst.type));

            if (inst->dst.indirect) {
               set(SENDS_DST_ADDR_IMM,      (inst->dst.addr_imm >> 4) & 0x1F);
               set(SENDS_DST_ADDR_IMM_SIGN, (inst->dst.addr_imm >> 9) & 0x1);
               set(SENDS_DST_ADDR_SUBNR,    inst->dst.subnr);
            } else {
               set(SENDS_DST_NR,            inst->dst.nr);
               set(SENDS_DST_SUBNR,         inst->dst.subnr >> 4);
            }

            set(SENDS_SRC0_ADDR_MODE, inst->src[0].indirect);
            if (inst->src[0].indirect) {
               set(SENDS_SRC0_ADDR_IMM,      (inst->src[0].addr_imm >> 4) & 0x1F);
               set(SENDS_SRC0_ADDR_IMM_SIGN, (inst->src[0].addr_imm >> 9) & 0x1);
               set(SENDS_SRC0_ADDR_SUBNR,    inst->src[0].subnr);
            } else {
               set(SENDS_SRC0_SUBNR,  inst->src[0].subnr >> 4);
               set(SENDS_SRC0_NR,     inst->src[0].nr);
            }

            set(SENDS_SRC1_FILE,  encode_file(inst->src[1].file));
            set(SENDS_SRC1_NR,    inst->src[1].nr);

            if (inst->send.ex_desc_is_reg) {
               set(bits(82, 80), inst->send.ex_desc_subnr >> 2);
            } else {
               const unsigned imm = inst->send.ex_desc_imm;

               set(bits(95, 80), GET_BITS(imm, 31, 16));
               set(bits(67, 64), GET_BITS(imm, 9, 6));
               assert(GET_BITS(imm, 15, 10) == 0);
               assert(GET_BITS(imm, 5, 0) == 0);
            }

         } else {
            encode_operand_controls();
            encode_sources();

            if (inst->send.ex_desc_is_reg) {
               set(bits(82, 80), inst->send.ex_desc_subnr >> 2);
            } else {
               const unsigned imm = inst->send.ex_desc_imm;

               set(bits(94, 91), GET_BITS(imm, 31, 28));
               set(bits(88, 85), GET_BITS(imm, 27, 24));
               set(bits(83, 80), GET_BITS(imm, 23, 20));
               set(bits(67, 64), GET_BITS(imm, 19, 16));
               assert(GET_BITS(imm, 15, 0) == 0);
            }

            // TODO: HERE: Move to sources maybe?
            // TODO: Remove me, this is to fulfill older encoder.
            if (inst->send.desc_is_reg) {
               set(SOURCES(SRC1_FILE), encode_file(GEN_ARF));
               set(SOURCES(SRC1_OPERAND)(SRC_NR), BRW_ARF_ADDRESS);
            } else {
               set(SOURCES(SRC1_FILE), encode_file(GEN_IMM));
            }
         }

         encode_operand_send_msg();
         break;
      }

      case GEN_FORMAT_BRANCH: {
         encode_controls();
         encode_operand_controls();

         /* TODO: Support non-immediate branch sources. */

         set(BRANCH_JIP, (uint32_t)inst->branch.jip);

         if (gen_has_uip(inst->opcode)) {
            set(BRANCH_UIP, (uint32_t)inst->branch.uip);

         } else {
            /* When Src1 is not used, old versions required its file to be ARF and
             * its type to match Src0 type.  See "Non-present Operands" in PRM for
             * SKL, vol 7 "3D-Media-GPGPU".
             */
            set(BRANCH_SRC1_FILE, encode_file(GEN_ARF));
            set(BRANCH_SRC1_TYPE, encode_type(GEN_ARF, BRW_TYPE_D));
         }
         break;
      }

      case GEN_FORMAT_ILLEGAL:
      case GEN_FORMAT_NOP:
         break;

      case GEN_FORMAT_DPAS_THREE_SRC:
         UNREACHABLE("invalid format for pre-xe");
      }
   }

private:
   inline void
   set(const gen_range &bits, uint64_t value)
   {
      unsigned high = bits.hi;
      unsigned low = bits.lo;

      assume(high < 128);
      assume(high >= low);
      const unsigned word = high / 64;
      assert(word == low / 64);

      high %= 64;
      low %= 64;

      const uint64_t mask = (~0ull >> (64 - (high - low + 1))) << low;

      /* Make sure the supplied value actually fits in the given bitfield. */
      assert((value & (mask >> low)) == value);

      raw->data[word] = (raw->data[word] & ~mask) | (value << low);
   }

   inline void
   encode_controls()
   {
      const gen_range bits = CONTROLS;

      encode_controls_a(bits(CONTROLS_A));

      if (gen_inst_has_cond_modifier(desc->format, inst))
         set(bits(COND_MODIFIER), inst->cond_modifier);

      encode_controls_b(bits(CONTROLS_B));
   }

   inline void
   encode_controls_a(const gen_range &bits)
   {
      set(bits(ACCESS_MODE),    inst->align16);
      set(bits(NO_DD_CLEAR),    inst->no_dd_clear);
      set(bits(NO_DD_CHECK),    inst->no_dd_check);
      set(bits(QTR_CONTROL),    inst->chan_offset / 8);
      set(bits(NIB_CONTROL),    (inst->chan_offset / 4) % 2);
      set(bits(THREAD_CONTROL), inst->thread_control);
      set(bits(PRED_CONTROL),   inst->pred_control);
      set(bits(PRED_INV),       inst->pred_inv);
      set(bits(EXEC_SIZE),      cvt(inst->exec_size) - 1);
   }

   inline void
   encode_controls_b(const gen_range &bits)
   {
      if (desc->format == GEN_FORMAT_BRANCH)
         set(bits(BRANCH_CONTROL), inst->branch_control);
      else
         set(bits(ACC_WR_CONTROL), inst->acc_wr_control);

      if (gen_inst_has_saturate(desc->format, inst))
         set(bits(SATURATE), inst->saturate);
   }

   inline void
   encode_operand_src_reg_three_src(const gen_range &bits, unsigned i)
   {
      // TODO: Make into validation.
      assert((inst->src[i].subnr & ~0b11110) == 0);

      if (inst->src[i].rep_ctrl)
         set(bits(THREE_SRC_REP_CTRL), 1);
      else
         set(bits(THREE_SRC_SWIZZLE),  inst->src[i].swizzle);

      set(bits(THREE_SRC_SUBNR),       inst->src[i].subnr >> 2);
      set(bits(THREE_SRC_SUBNR_EXTRA), (inst->src[i].subnr >> 1) & 0x1);
      set(bits(THREE_SRC_NR),          inst->src[i].nr);
   }

   inline void
   encode_sources()
   {
      const gen_range bits = SOURCES;

      int imm_src = -1;

      if (inst->src[0].file == GEN_IMM)
         imm_src = 0;
      else
         encode_operand_src(bits(SRC0_OPERAND), 0);

      if (desc->format == GEN_FORMAT_BASIC_TWO_SRC) {
         // TODO: Check ONE_SRC doesn't have this.
         if (inst->src[0].indirect)
            set(bits(SRC0_ADDR_IMM_SIGN), (inst->src[0].addr_imm >> 9) & 0x1);

         set(bits(SRC1_FILE), encode_file(inst->src[1].file));
         set(bits(SRC1_TYPE), encode_type(inst->src[1].file, inst->src[1].type));

         if (inst->src[1].file == GEN_IMM) {
            assert(imm_src == -1);
            assert(brw_type_size_bytes(inst->src[1].type) < 8);
            imm_src = 1;

         } else {
            encode_operand_src(bits(SRC1_OPERAND), 1);

            if (inst->src[1].indirect)
               set(bits(SRC1_ADDR_IMM_SIGN), (inst->src[1].addr_imm >> 9) & 0x1);
         }
      }

      if (imm_src != -1) {
         if (brw_type_size_bytes(inst->src[imm_src].type) < 8) {
            set(bits(IMM_32), inst->src[imm_src].imm & 0xFFFFFFFF);

            // TODO: Add spec reference.  See commnet inside compact code,
            // we might just get rid of thsi entirely...?
            if (desc->format == GEN_FORMAT_BASIC_ONE_SRC) {
               set(bits(SRC1_FILE), encode_file(GEN_ARF));
               set(bits(SRC1_TYPE), encode_type(inst->src[0].file, inst->src[0].type));
            }

         } else {
            set(bits(IMM_64), inst->src[imm_src].imm);
         }
      }

      // TODO: Check if this actually needed or is just compatibility with old encoder.
      if (inst->opcode == GEN_OP_WAIT) {
         set(bits(SRC1_FILE), encode_file(GEN_ARF));
         set(bits(SRC1_TYPE), encode_type(GEN_ARF, BRW_TYPE_F));
         set(bits(SRC1_OPERAND)(SRC_VSTRIDE), ENCODE_VSTRIDE(8));
         set(bits(SRC1_OPERAND)(SRC_A1_WIDTH), cvt(8)-1);
         set(bits(SRC1_OPERAND)(SRC_A1_HSTRIDE), cvt(1));
      }
   }

   inline void
   encode_operand_src(const gen_range &bits, unsigned i)
   {
      if (inst->align16) {
         set(bits(SRC_A16_SWIZZLE_LO), (inst->src[i].swizzle >> 0) & 0xf);
         set(bits(SRC_A16_SWIZZLE_HI), (inst->src[i].swizzle >> 4) & 0xf);

         if (inst->src[i].indirect) {
            set(bits(SRC_A16_ADDR_IMM), (inst->src[i].addr_imm >> 4) & 0x1f);
            set(bits(SRC_ADDR_SUBNR),  inst->src[i].subnr);
         } else {
            set(bits(SRC_A16_SUBNR), inst->src[i].subnr >> 4);
            set(bits(SRC_NR),        inst->src[i].nr);
         }

      } else {
         if (inst->src[i].indirect) {
            set(bits(SRC_A1_ADDR_IMM), inst->src[i].addr_imm & 0x1ff);
            set(bits(SRC_A1_SUBNR),    inst->src[i].subnr);
         } else {
            set(bits(SRC_A1_SUBNR), inst->src[i].subnr);
            set(bits(SRC_NR),       inst->src[i].nr);
         }

         set(bits(SRC_A1_HSTRIDE), cvt(inst->src[i].region.hstride));
         set(bits(SRC_A1_WIDTH),   cvt(inst->src[i].region.width)-1);
      }

      set(bits(SRC_ABS),          inst->src[i].abs);
      set(bits(SRC_NEGATE),       inst->src[i].negate);
      set(bits(SRC_ADDRESS_MODE), inst->src[i].indirect);

      set(bits(SRC_VSTRIDE), ENCODE_VSTRIDE(inst->src[i].region.vstride));
   }

   inline void
   encode_operand_dst(const gen_range &bits)
   {
      if (inst->align16) {
         set(bits(DST_A16_WRITEMASK), inst->dst.writemask);

         if (inst->dst.indirect) {
            set(bits(DST_A16_ADDR_IMM), (inst->dst.addr_imm >> 4) & 0x1f);
            set(bits(DST_ADDR_SUBNR),   inst->dst.subnr);
         } else {
            set(bits(DST_A16_SUBNR), inst->dst.subnr >> 4);
            set(bits(DST_NR),        inst->dst.nr);
         }

         // TODO: Remove me. Here just to match existing code.
         set(bits(DST_A1_HSTRIDE), cvt(inst->dst.region.hstride));

      } else {
         if (inst->dst.indirect) {
            set(bits(DST_A1_ADDR_IMM), inst->dst.addr_imm & 0x1ff);
         } else {
            set(bits(DST_A1_SUBNR),    inst->dst.subnr);
            set(bits(DST_NR),          inst->dst.nr);
         }

         set(bits(DST_A1_HSTRIDE), cvt(inst->dst.region.hstride));
      }

      set(bits(DST_ADDR_MODE), inst->dst.indirect);
   }

   inline void
   encode_operand_controls()
   {
      const gen_range bits = OPERAND_CONTROLS;

      set(bits(FLAG_SUBNR), inst->flag_subnr);
      set(bits(FLAG_NR),    inst->flag_nr);
      set(bits(NO_MASK),    inst->no_mask);

      if (desc->has_dst) {
         set(bits(DST_FILE), encode_file(inst->dst.file));
         set(bits(DST_TYPE), encode_type(inst->dst.file, inst->dst.type));

         if (inst->dst.indirect)
            set(bits(DST_ADDR_IMM_SIGN), (inst->dst.addr_imm >> 9) & 0x1);

         encode_operand_dst(bits(DST_OPERAND));
      }

      if (desc->format == GEN_FORMAT_BRANCH) {
         /* TODO: Support non-immediate branch sources. */
         set(bits(SRC0_FILE), encode_file(GEN_IMM));
         set(bits(SRC0_TYPE), encode_type(GEN_IMM, BRW_TYPE_D));
      } else {
         set(bits(SRC0_FILE), encode_file(inst->src[0].file));
         set(bits(SRC0_TYPE), encode_type(inst->src[0].file, inst->src[0].type));
      }
   }

   inline void
   encode_operand_send_msg()
   {
      const gen_range bits = SEND_MSG;

      if (!inst->send.desc_is_reg)
         set(bits(SEND_DESC_IMM), inst->send.desc_imm);

      set(bits(SEND_EOT), inst->send.eot);
   }

   static inline unsigned
   encode_file(gen_file file)
   {
      switch (file) {
      case GEN_ARF: return 0x0;
      case GEN_GRF: return 0x1;
      case GEN_IMM: return 0x3;
      default:      UNREACHABLE("invalid reg file");
      }
   }

   unsigned
   encode_type(gen_file file, brw_reg_type type) const
   {
      assert(file != GEN_IMM ||
             brw_type_is_vector_imm(type) ||
             brw_type_size_bits(type) >= 16);

      if (type == BRW_TYPE_INVALID)
         return INVALID_HW_REG_TYPE;

      if (brw_type_size_bits(type) == 64 &&
          !(brw_type_is_int(type) ? devinfo->has_64bit_int
                                  : devinfo->has_64bit_float))
         return INVALID_HW_REG_TYPE;

      if (brw_type_is_bfloat(type))
         return INVALID_HW_REG_TYPE;

      if (devinfo->ver == 11) {
         if (brw_type_is_vector_imm(type)) {
            if (type == BRW_TYPE_VF)
               return 11;
            /* UV/V is the same encoding as UB/B */
            type = (brw_reg_type)((unsigned)type & ~(BRW_TYPE_VECTOR | BRW_TYPE_SIZE_MASK));
         }

         if (brw_type_is_float(type)) {
            /* HF: 8, F: 9 */
            return 8 + (type & BRW_TYPE_SIZE_MASK) - 1;
         }

         /* UB: 4, UW: 2, UD: 0
          *  B: 5,  W: 3,  D: 1
          */
         return 4 - 2 * (type & BRW_TYPE_SIZE_MASK) +
                (brw_type_is_sint(type) ? 1 : 0);
      } else {
         if (brw_type_is_vector_imm(type)) {
            return type == BRW_TYPE_UV ? 4 :
                   type == BRW_TYPE_VF ? 5 :
                        /* BRW_TYPE_V */ 6;
         } else if (brw_type_is_float(type)) {
            static const unsigned imm_tbl[] = {
               [0b00] = 5,  /* VF */
               [0b01] = 11, /* HF */
               [0b10] = 7,  /*  F */
               [0b11] = 10, /* DF */
            };
            static const unsigned reg_tbl[] = {
               [0b00] = 0,
               [0b01] = 10, /* HF */
               [0b10] = 7,  /*  F */
               [0b11] = 6,  /* DF */
            };
            const unsigned *tbl = file == GEN_IMM ? imm_tbl : reg_tbl;
            return tbl[type & BRW_TYPE_SIZE_MASK];
         } else {
            static const unsigned tbl[] = {
               [0b00] = 4, /* UB/UV */
               [0b01] = 2, /* UW */
               [0b10] = 0, /* UD */
               [0b11] = 8, /* UQ */
            };
            return tbl[type & BRW_TYPE_SIZE_MASK] |
                   (brw_type_is_sint(type) ? 1 : 0);
         }
      }
   }

   unsigned
   encode_type_3src(brw_reg_type type) const
   {
      if (brw_type_is_bfloat(type) && !devinfo->has_bfloat16)
         return INVALID_HW_REG_TYPE;

      if (devinfo->ver == 11) {
         if (brw_type_is_float(type)) {
            /* HF: 0b000 | F: 0b001 | DF: 0b010; subtract 1 from our size mask */
            return (type & BRW_TYPE_SIZE_MASK) - 1;
         }

         /* Bit 0 is the sign bit, bits 1-2 are our size mask reversed.
          * UD: 0b000 | D: 0b001
          * UW: 0b010 | W: 0b011
          * UB: 0b100 | B: 0b101
          */
         return ((2 - (type & BRW_TYPE_SIZE_MASK)) << 1) |
                (brw_type_is_sint(type) ? 1 : 0);
      } else {
         /* align16 encodings */
         switch (type) {
         case BRW_TYPE_F:  return 0;
         case BRW_TYPE_D:  return 1;
         case BRW_TYPE_UD: return 2;
         case BRW_TYPE_DF: return 3;
         case BRW_TYPE_HF: return 4;
         default:          return BRW_TYPE_INVALID;
         }
      }
   }
};

struct gen_decoder_pre_xe {
   const intel_device_info *devinfo;

   gen_inst *inst;
   const gen_raw_inst *raw;
   char **error;
   const gen_inst_description *desc;

   gen_decoder_pre_xe(const intel_device_info *devinfo)
      : devinfo(devinfo)
   {}

   void
   decode(gen_inst *inst, const gen_raw_inst *raw)
   {
      this->inst = inst;
      this->raw = raw;
      this->desc = &hw_to_description[get(HW_OPCODE)];

      memset(inst, 0, sizeof(*inst));

      gen_range bits = { 127, 0 };

      inst->opcode = desc->gen_op;
      inst->debug_control = get(DEBUG_CONTROL);

      switch (desc->format) {
      case GEN_FORMAT_BASIC_ONE_SRC:
      case GEN_FORMAT_BASIC_TWO_SRC: {
         decode_controls();
         decode_operand_controls();
         decode_sources();
         break;
      }

      case GEN_FORMAT_BASIC_THREE_SRC: {
         decode_controls();

         inst->flag_subnr = get(THREE_FLAG_SUBNR);
         inst->flag_nr    = get(THREE_FLAG_NR);
         inst->no_mask    = get(THREE_NO_MASK);

         if (gen_inst_has_source_modifier(desc->format, inst)) {
            inst->src[0].abs    = get(THREE_SRC0_ABS);
            inst->src[1].abs    = get(THREE_SRC1_ABS);
            inst->src[2].abs    = get(THREE_SRC2_ABS);
            inst->src[0].negate = get(THREE_SRC0_NEGATE);
            inst->src[1].negate = get(THREE_SRC1_NEGATE);
            inst->src[2].negate = get(THREE_SRC2_NEGATE);
         }

         if (inst->align16) {
            inst->dst.file    = GEN_GRF;
            inst->src[0].file = GEN_GRF;
            inst->src[1].file = GEN_GRF;
            inst->src[2].file = GEN_GRF;

            const brw_reg_type src_type = decode_type_3src(get(THREE_SRC_TYPE), 0);
            const brw_reg_type dst_type = decode_type_3src(get(THREE_DST_TYPE), 0);

            inst->src[0].type = src_type;
            inst->dst.type = dst_type;

            if (brw_type_is_float(src_type)) {
               inst->src[2].type = get(THREE_SRC2_TYPE) ? BRW_TYPE_HF : BRW_TYPE_F;
               inst->src[1].type = get(THREE_SRC1_TYPE) ? BRW_TYPE_HF : BRW_TYPE_F;
            } else {
               inst->src[2].type = src_type;
               inst->src[1].type = src_type;
            }

            inst->dst.writemask = get(THREE_DST_WRITEMASK);
            inst->dst.subnr     = get(THREE_DST_SUBNR) << 2; // TODO: Double check.
            inst->dst.nr        = get(THREE_DST_NR);

            decode_operand_src_reg_three_src(THREE_SRC0_OPERAND, 0);
            decode_operand_src_reg_three_src(THREE_SRC1_OPERAND, 1);
            decode_operand_src_reg_three_src(THREE_SRC2_OPERAND, 2);

         } else {
            assert(devinfo->ver == 11);

            const unsigned exec_type = get(THREE_A1_EXECUTION_TYPE);

            inst->dst.file    = get(THREE_A1_DST_FILE)  ? GEN_ARF : GEN_GRF;
            inst->src[0].file = get(THREE_A1_SRC0_FILE) ? GEN_IMM : GEN_GRF;
            inst->src[1].file = get(THREE_A1_SRC1_FILE) ? GEN_ARF : GEN_GRF;
            inst->src[2].file = get(THREE_A1_SRC2_FILE) ? GEN_IMM : GEN_GRF;

            inst->dst.type    = decode_type_3src(get(THREE_DST_TYPE), exec_type);
            inst->dst.subnr   = get(THREE_DST_SUBNR);
            inst->dst.nr      = get(THREE_DST_NR);

            inst->dst.region.hstride = DST_STRIDE_3SRC(get(THREE_A1_DST_HSTRIDE));

            inst->src[0].type = decode_type_3src(get(THREE_A1_SRC0_TYPE), exec_type);
            if (inst->src[0].file == GEN_GRF) {
               inst->src[0].region.vstride = DECODE_VSTRIDE_3SRC(get(THREE_A1_SRC0_VSTRIDE));
               inst->src[0].region.hstride = STRIDE(get(THREE_A1_SRC0_HSTRIDE));
               inst->src[0].subnr   = get(THREE_A1_SRC0_SUBNR);
               inst->src[0].nr      = get(THREE_A1_SRC0_NR);
            } else {
               assert(inst->src[0].file == GEN_IMM);
               inst->src[0].imm = get(THREE_A1_SRC0_IMM);
            }

            inst->src[1].type    = decode_type_3src(get(THREE_A1_SRC1_TYPE), exec_type);
            inst->src[1].region.vstride = DECODE_VSTRIDE_3SRC(get(THREE_A1_SRC1_VSTRIDE));
            inst->src[1].region.hstride = STRIDE(get(THREE_A1_SRC1_HSTRIDE));
            inst->src[1].subnr   = get(THREE_A1_SRC1_SUBNR);
            inst->src[1].nr      = get(THREE_A1_SRC1_NR);

            inst->src[2].type = decode_type_3src(get(THREE_A1_SRC2_TYPE), exec_type);
            if (inst->src[2].file == GEN_GRF) {
               inst->src[2].region.hstride = STRIDE(get(THREE_A1_SRC2_HSTRIDE));
               inst->src[2].subnr   = get(THREE_A1_SRC2_SUBNR);
               inst->src[2].nr      = get(THREE_A1_SRC2_NR);
            } else {
               assert(inst->src[2].file == GEN_IMM);
               inst->src[2].imm = get(THREE_A1_SRC2_IMM);
            }
         }
         break;
      }

      case GEN_FORMAT_SEND: {
         decode_controls_a(SEND_CONTROLS_A);

         inst->send.sfid = get(SEND_SFID);

         decode_controls_b(SEND_CONTROLS_B);

         /* Need to pull these early to decide proper decoding. */
         inst->send.desc_is_reg    = get(SEND_DESC_IS_REG);
         inst->send.ex_desc_is_reg = get(SEND_EX_DESC_IS_REG);

         fprintf(stderr, "decoded desc is reg = %d\n", inst->send.desc_is_reg);

         if (gen_inst_is_split_send(devinfo, inst)) {
            inst->flag_subnr = get(SENDS_FLAG_SUBNR);
            inst->flag_nr    = get(SENDS_FLAG_NR);
            inst->no_mask    = get(SENDS_NO_MASK);

            inst->dst.indirect = get(SENDS_DST_ADDR_MODE);
            inst->dst.file     = decode_file(get(SENDS_DST_FILE));
            inst->dst.type     = decode_type(inst->dst.file, get(SENDS_DST_TYPE));

            if (inst->dst.indirect) {
               inst->dst.addr_imm =
                  (-1 * get(SENDS_DST_ADDR_IMM_SIGN)) * get(SENDS_DST_ADDR_IMM);
               inst->dst.subnr = get(SENDS_DST_ADDR_SUBNR);
            } else {
               inst->dst.nr    = get(SENDS_DST_NR);
               inst->dst.subnr = get(SENDS_DST_SUBNR) << 4;
            }

            inst->src[0].file     = GEN_GRF;
            inst->src[0].indirect = get(SENDS_SRC0_ADDR_MODE);

            if (inst->src[0].indirect) {
               inst->src[0].addr_imm =
                  (-1 * get(SENDS_SRC0_ADDR_IMM_SIGN)) * get(SENDS_SRC0_ADDR_IMM);
               inst->src[0].subnr = get(SENDS_SRC0_ADDR_SUBNR);
            } else {
               inst->src[0].nr    = get(SENDS_SRC0_NR);
               inst->src[0].subnr = get(SENDS_SRC0_SUBNR) << 4;
            }

            inst->src[1].file = decode_file(get(SENDS_SRC1_FILE));
            inst->src[1].nr   = get(SENDS_SRC1_NR);

            if (inst->send.ex_desc_is_reg) {
               inst->send.ex_desc_subnr = get(bits(82, 80)) << 2;
            } else {
               inst->send.ex_desc_imm = get(bits(95, 80)) << 16 |
                                   get(bits(67, 64)) << 6;
            }

         } else {
            decode_operand_controls();
            decode_sources();

            if (inst->send.ex_desc_is_reg) {
               inst->send.ex_desc_subnr = get(bits(82, 80)) << 2;
            } else {
               inst->send.ex_desc_imm = get(bits(94, 91)) << 28 |
                                        get(bits(88, 85)) << 24 |
                                        get(bits(83, 80)) << 20 |
                                        get(bits(67, 64)) << 16;
            }
         }

         decode_operand_send_msg();
         break;
      }

      case GEN_FORMAT_BRANCH: {
         decode_controls();
         decode_operand_controls();

         /* TODO: Support non-immediate branch sources. */

         inst->branch.jip = (int32_t) get(BRANCH_JIP);
         if (gen_has_uip(inst->opcode))
            inst->branch.uip = (int32_t) get(BRANCH_UIP);
         break;
      }

      case GEN_FORMAT_ILLEGAL:
      case GEN_FORMAT_NOP:
         break;

      case GEN_FORMAT_DPAS_THREE_SRC:
         UNREACHABLE("invalid format for pre-xe");
      }
   }

private:
   inline uint64_t
   get(const gen_range &bits) const
   {
      unsigned high = bits.hi;
      unsigned low = bits.lo;

      assume(high < 128);
      assume(high >= low);
      /* We assume the field doesn't cross 64-bit boundaries. */
      const unsigned word = high / 64;
      assert(word == low / 64);

      high %= 64;
      low %= 64;

      const uint64_t mask = (~0ull >> (64 - (high - low + 1)));

      return (raw->data[word] >> low) & mask;
   }

   inline void
   decode_controls()
   {
      const gen_range bits = CONTROLS;

      decode_controls_a(bits(CONTROLS_A));

      if (gen_inst_has_cond_modifier(desc->format, inst))
         inst->cond_modifier = (brw_conditional_mod) get(bits(COND_MODIFIER));

      decode_controls_b(bits(CONTROLS_B));
   }

   inline void
   decode_controls_a(const gen_range &bits)
   {
      inst->align16        = get(bits(ACCESS_MODE));
      inst->no_dd_clear    = get(bits(NO_DD_CLEAR));
      inst->no_dd_check    = get(bits(NO_DD_CHECK));
      inst->chan_offset    = get(bits(QTR_CONTROL)) * 8 +
                             get(bits(NIB_CONTROL)) * 4;
      inst->thread_control = get(bits(THREAD_CONTROL));
      inst->pred_control   = (brw_predicate) get(bits(PRED_CONTROL));
      inst->pred_inv       = get(bits(PRED_INV));
      inst->exec_size      = 1 << get(bits(EXEC_SIZE));
   }

   inline void
   decode_controls_b(const gen_range &bits)
   {
      if (desc->format == GEN_FORMAT_BRANCH)
         inst->branch_control = get(bits(BRANCH_CONTROL));
      else
         inst->acc_wr_control = get(bits(ACC_WR_CONTROL));

      if (gen_inst_has_saturate(desc->format, inst))
         inst->saturate = get(bits(SATURATE));
   }

   inline void
   decode_operand_controls()
   {
      const gen_range bits = OPERAND_CONTROLS;

      inst->flag_subnr = get(bits(FLAG_SUBNR));
      inst->flag_nr    = get(bits(FLAG_NR));
      inst->no_mask    = get(bits(NO_MASK));

      if (desc->has_dst) {
         inst->dst.file = decode_file(get(bits(DST_FILE)));
         inst->dst.type = decode_type(inst->dst.file, get(bits(DST_TYPE)));
      }

      inst->src[0].file = decode_file(get(bits(SRC0_FILE)));
      inst->src[0].type = decode_type(inst->src[0].file, get(bits(SRC0_TYPE)));

      if (desc->has_dst) {
         decode_operand_dst(bits(DST_OPERAND));

         /* Apply address immediate sign bit. */
         if (inst->dst.indirect)
            inst->dst.addr_imm *= -1 * get(bits(DST_ADDR_IMM_SIGN));
      }
   }

   inline void
   decode_operand_dst(const gen_range &bits)
   {
      inst->dst.indirect = get(bits(DST_ADDR_MODE));

      if (inst->align16) {
         inst->dst.writemask = get(bits(DST_A16_WRITEMASK));

         if (inst->dst.indirect) {
            inst->dst.addr_imm = get(bits(DST_A16_ADDR_IMM)) << 4;
            inst->dst.subnr    = get(bits(DST_ADDR_SUBNR));
         } else {
            inst->dst.subnr = get(bits(DST_A16_SUBNR)) << 4;
            inst->dst.nr    = get(bits(DST_NR));
         }

         // TODO: Remove me!
         inst->dst.region.hstride = STRIDE(get(bits(DST_A1_HSTRIDE)));
      } else {
         if (inst->dst.indirect) {
            inst->dst.addr_imm = get(bits(DST_A1_ADDR_IMM));
         } else {
            inst->dst.subnr = get(bits(DST_A1_SUBNR));
            inst->dst.nr    = get(bits(DST_NR));
         }

         inst->dst.region.hstride = STRIDE(get(bits(DST_A1_HSTRIDE)));
      }
   }

   inline void
   decode_sources()
   {
      const gen_range bits = SOURCES;

      int imm_src = -1;

      if (inst->src[0].file == GEN_IMM)
         imm_src = 0;
      else
         decode_operand_src(bits(24, 0), 0);

      if (desc->format == GEN_FORMAT_BASIC_TWO_SRC) {
         // TODO: Check ONE_SRC doesn't have this.
         if (inst->src[0].indirect)
            inst->src[0].addr_imm *= -1 * get(bits(31));

         inst->src[1].file = decode_file(get(bits(26, 25)));
         inst->src[1].type = decode_type(inst->src[1].file, get(bits(30, 27)));

         if (inst->src[1].file == GEN_IMM) {
            assert(imm_src == -1);
            imm_src = 1;
         } else {
            decode_operand_src(bits(56, 32), 1);

            if (inst->src[1].indirect)
               inst->src[1].addr_imm *= -1 * get(bits(57));
         }
      }

      if (imm_src != -1) {
         if (brw_type_size_bytes(inst->src[imm_src].type) < 8)
            inst->src[imm_src].imm = get(bits(IMM_32));
         else
            inst->src[imm_src].imm = get(bits(IMM_64));
      }
   }

   inline void
   decode_operand_src(const gen_range &bits, unsigned i)
   {
      inst->src[i].indirect = get(bits(SRC_ADDRESS_MODE));

      if (inst->align16) {
         inst->src[i].swizzle = (get(bits(SRC_A16_SWIZZLE_LO)) << 0) |
                                (get(bits(SRC_A16_SWIZZLE_HI)) << 4);

         if (inst->src[i].indirect) {
            inst->src[i].addr_imm = get(bits(SRC_A16_ADDR_IMM)) << 4;
            inst->src[i].subnr    = get(bits(SRC_ADDR_SUBNR));
         } else {
            inst->src[i].subnr = get(bits(SRC_A16_SUBNR)) << 4;
            inst->src[i].nr    = get(bits(SRC_NR));
         }

      } else {
         if (inst->src[i].indirect) {
            inst->src[i].addr_imm = get(bits(SRC_A1_ADDR_IMM));
            inst->src[i].subnr    = get(bits(SRC_A1_SUBNR));
         } else {
            inst->src[i].subnr = get(bits(SRC_A1_SUBNR));
            inst->src[i].nr    = get(bits(SRC_NR));
         }

         inst->src[i].region.hstride = STRIDE(get(bits(SRC_A1_HSTRIDE)));
         inst->src[i].region.width   = WIDTH(get(bits(SRC_A1_WIDTH)));
      }

      inst->src[i].abs    = get(bits(SRC_ABS));
      inst->src[i].negate = get(bits(SRC_NEGATE));

      inst->src[i].region.vstride = DECODE_VSTRIDE(get(bits(SRC_VSTRIDE)));
   }

   inline void
   decode_operand_src_reg_three_src(const gen_range &bits, unsigned i)
   {
      if (get(bits(THREE_SRC_REP_CTRL)))
         inst->src[i].rep_ctrl = true;
      else
         inst->src[i].swizzle  = get(bits(THREE_SRC_SWIZZLE));

      inst->src[i].subnr    = get(bits(THREE_SRC_SUBNR)) << 2 |
                              get(bits(THREE_SRC_SUBNR_EXTRA)) << 1;
      inst->src[i].nr       = get(bits(THREE_SRC_NR));
   }

   inline void
   decode_operand_send_msg()
   {
      const gen_range bits = SEND_MSG;

      if (!inst->send.desc_is_reg)
         inst->send.desc_imm = get(bits(SEND_DESC_IMM));

      inst->send.eot = get(bits(SEND_EOT));
   }

   static inline gen_file
   decode_file(unsigned hw_file)
   {
      switch (hw_file) {
      case 0:  return GEN_ARF;
      case 1:  return GEN_GRF;
      default: return GEN_IMM;
      }
   }

   inline brw_reg_type
   decode_type(gen_file file, unsigned hw_type)
   {
      if (hw_type >= (1 << 4))
         return BRW_TYPE_INVALID;

      if (devinfo->ver == 11) {
         static const enum brw_reg_type tbl[] = {
            [0] = BRW_TYPE_UD,
            [1] = BRW_TYPE_D,
            [2] = BRW_TYPE_UW,
            [3] = BRW_TYPE_W,
            [4] = BRW_TYPE_UB, /* or UV */
            [5] = BRW_TYPE_B,  /* or V */
            [6] = BRW_TYPE_UQ,
            [7] = BRW_TYPE_Q,
            [8] = BRW_TYPE_HF,
            [9] = BRW_TYPE_F,
            [10] = BRW_TYPE_INVALID, /* no DF */
            [11] = BRW_TYPE_VF,
            [12] = BRW_TYPE_INVALID,
            [13] = BRW_TYPE_INVALID,
            [14] = BRW_TYPE_INVALID,
            [15] = BRW_TYPE_INVALID,
         };
         enum brw_reg_type t = tbl[hw_type];
         if (file == GEN_IMM && brw_type_size_bits(t) == 8)
            return (t & BRW_TYPE_BASE_SINT) ? BRW_TYPE_V : BRW_TYPE_UV;
         if (file != GEN_IMM && brw_type_is_vector_imm(t))
            return BRW_TYPE_INVALID;
         return t;
      } else {
         static const enum brw_reg_type imm_tbl[] = {
            [0] = BRW_TYPE_UD,
            [1] = BRW_TYPE_D,
            [2] = BRW_TYPE_UW,
            [3] = BRW_TYPE_W,
            [4] = BRW_TYPE_UV,
            [5] = BRW_TYPE_VF,
            [6] = BRW_TYPE_V,
            [7] = BRW_TYPE_F,
            [8] = BRW_TYPE_UQ,
            [9] = BRW_TYPE_Q,
            [10] = BRW_TYPE_DF,
            [11] = BRW_TYPE_HF,
            [12] = BRW_TYPE_INVALID,
            [13] = BRW_TYPE_INVALID,
            [14] = BRW_TYPE_INVALID,
            [15] = BRW_TYPE_INVALID,
         };
         static const enum brw_reg_type reg_tbl[] = {
            [0] = BRW_TYPE_UD,
            [1] = BRW_TYPE_D,
            [2] = BRW_TYPE_UW,
            [3] = BRW_TYPE_W,
            [4] = BRW_TYPE_UB,
            [5] = BRW_TYPE_B,
            [6] = BRW_TYPE_DF,
            [7] = BRW_TYPE_F,
            [8] = BRW_TYPE_UQ,
            [9] = BRW_TYPE_Q,
            [10] = BRW_TYPE_HF,
            [11] = BRW_TYPE_INVALID,
            [12] = BRW_TYPE_INVALID,
            [13] = BRW_TYPE_INVALID,
            [14] = BRW_TYPE_INVALID,
            [15] = BRW_TYPE_INVALID,
         };
         const enum brw_reg_type *tbl = file == GEN_IMM ? imm_tbl : reg_tbl;
         return tbl[hw_type];
      }

      return BRW_TYPE_INVALID;
   }

   inline brw_reg_type
   decode_type_3src(unsigned hw_type, unsigned exec_type)
   {
      STATIC_ASSERT(BRW_ALIGN1_3SRC_EXEC_TYPE_INT == 0);
      STATIC_ASSERT(BRW_ALIGN1_3SRC_EXEC_TYPE_FLOAT == 1);
      assert(exec_type == 0 || exec_type == 1);

      if (devinfo->ver == 11) {
         if (exec_type == BRW_ALIGN1_3SRC_EXEC_TYPE_FLOAT) {
            return hw_type > 1 ? BRW_TYPE_INVALID :
                   hw_type ? BRW_TYPE_F : BRW_TYPE_HF;
         }

         unsigned size_field = 2 >> (hw_type >> 1);
         unsigned base_field = (hw_type & 1) << 2;
         return (enum brw_reg_type) (base_field | size_field);
      } else {
         /* align16 encodings */
         static const enum brw_reg_type tbl[] = {
            [0] = BRW_TYPE_F,
            [1] = BRW_TYPE_D,
            [2] = BRW_TYPE_UD,
            [3] = BRW_TYPE_DF,
            [4] = BRW_TYPE_HF,
         };
         return hw_type < ARRAY_SIZE(tbl) ? tbl[hw_type] : BRW_TYPE_INVALID;
      }
   }
};

bool
gen_encode_pre_xe(gen_encode_params *params)
{
   assert(params->devinfo);
   assert(params->mem_ctx);
   assert(params->insts);
   assert(params->errors == NULL);

   /* Already allocated by the main gen_encode() function (if needed). */
   assert(params->raw_bytes != NULL);
   assert(params->raw_bytes_size >= params->num_insts * (int)sizeof(gen_raw_inst));
   assert(params->num_insts > 0);

   gen_raw_inst *raw = (gen_raw_inst *)params->raw_bytes;
   int written = 0;

   auto e = gen_encoder_pre_xe(params->devinfo);

   for (int i = 0; i < params->num_insts; i++) {
      // TODO: Handle encode failure.
      e.encode(params->insts[i], raw + i);
      written++;
   }

   params->raw_bytes_size = written * sizeof(gen_raw_inst);
   return params->errors == NULL;
}

bool
gen_decode_pre_xe(gen_decode_params *params)
{
   assert(params->devinfo);
   assert(params->mem_ctx);
   assert(params->raw_bytes);
   assert(params->errors == NULL);

   /* Already allocated by the main gen_decode() function. */
   assert(params->insts);
   assert(params->num_insts > 0);

   int decoded = 0;

   uint64_t *raw = (uint64_t *)params->raw_bytes;
   const uint64_t *raw_end = raw + (params->raw_bytes_size / 8);

   auto d = gen_decoder_pre_xe(params->devinfo);

   while (raw < raw_end) {
      // TODO: Compact.  Maybe teach decode to mark whether "was compact"?
      // Or just do it from here and add a bitset was compact to the
      // params.
      assert(gen_raw_is_compact((void *)raw) == false);

      // TODO: Error handling.
      d.decode(params->insts[decoded], (gen_raw_inst *)raw);
      decoded++;

      raw += 2;
   }

   params->num_insts = decoded;
   return params->errors == NULL;
}
