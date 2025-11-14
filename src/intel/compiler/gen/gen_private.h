/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "gen.h"

#include "dev/intel_device_info.h"

// TODO: Merge operand types and get rid of this templates.

/* This is almost always called with a numeric constant argument, so
 * make things easy to evaluate at compile time:
 */
static inline unsigned cvt(unsigned val)
{
   switch (val) {
   case 0: return 0;
   case 1: return 1;
   case 2: return 2;
   case 4: return 3;
   case 8: return 4;
   case 16: return 5;
   case 32: return 6;
   }
   return 0;
}

static inline bool
is_null(const gen_operand &o)
{
   return !o.indirect &&
          o.file == GEN_ARF &&
          o.nr == BRW_ARF_NULL;
}

static inline bool
is_accumulator(const gen_operand &o)
{
   return !o.indirect &&
          o.file == GEN_ARF &&
          (o.nr & 0xF0) == BRW_ARF_ACCUMULATOR;
}

enum gen_format {
   GEN_FORMAT_BASIC_ONE_SRC,
   GEN_FORMAT_BASIC_TWO_SRC,
   GEN_FORMAT_BASIC_THREE_SRC,
   GEN_FORMAT_DPAS_THREE_SRC,
   GEN_FORMAT_SEND,
   GEN_FORMAT_BRANCH,
   GEN_FORMAT_ILLEGAL,
   GEN_FORMAT_NOP,
};

constexpr gen_format
gen_inst_format(const gen_opcode op)
{
   switch (op) {
   case GEN_OP_CBIT:
   case GEN_OP_MOV:
   case GEN_OP_NOT:
   case GEN_OP_SYNC:
   case GEN_OP_BFREV:
   case GEN_OP_FRC:
   case GEN_OP_FBH:
   case GEN_OP_FBL:
   case GEN_OP_RNDU:
   case GEN_OP_RNDD:
   case GEN_OP_RNDE:
   case GEN_OP_RNDZ:
   case GEN_OP_LZD:
   case GEN_OP_WAIT:
      return GEN_FORMAT_BASIC_ONE_SRC;

   case GEN_OP_ADD:
   case GEN_OP_ADDC:
   case GEN_OP_AND:
   case GEN_OP_ASR:
   case GEN_OP_BFI1:
   case GEN_OP_CMP:
   case GEN_OP_MAC:
   case GEN_OP_MACH:
   case GEN_OP_MATH:
   case GEN_OP_MOVI:
   case GEN_OP_MUL:
   case GEN_OP_OR:
   case GEN_OP_SEL:
   case GEN_OP_SHL:
   case GEN_OP_SHR:
   case GEN_OP_SUBB:
   case GEN_OP_XOR:
   case GEN_OP_CMPN:
   case GEN_OP_AVG:
   case GEN_OP_SMOV:
   case GEN_OP_DP4:
   case GEN_OP_DPH:
   case GEN_OP_DP3:
   case GEN_OP_DP2:
   case GEN_OP_LINE:
   case GEN_OP_SRND:
      return GEN_FORMAT_BASIC_TWO_SRC;

   case GEN_OP_BFI2:
   case GEN_OP_BFE:
   case GEN_OP_BFN:
   case GEN_OP_MAD:
   case GEN_OP_CSEL:
   case GEN_OP_MADM:
   case GEN_OP_LRP:
      return GEN_FORMAT_BASIC_THREE_SRC;

   case GEN_OP_ROR:
   case GEN_OP_ROL:
      return GEN_FORMAT_BASIC_TWO_SRC;

   case GEN_OP_ADD3:
   case GEN_OP_DP4A:
      return GEN_FORMAT_BASIC_THREE_SRC;

   case GEN_OP_PLN:
      return GEN_FORMAT_BASIC_TWO_SRC;

   case GEN_OP_SEND:
   case GEN_OP_SENDC:
   case GEN_OP_SENDS:
   case GEN_OP_SENDSC:
      return GEN_FORMAT_SEND;

   case GEN_OP_BRC:
   case GEN_OP_BRD:
   case GEN_OP_BREAK:
   case GEN_OP_CALL:
   case GEN_OP_CALLA:
   case GEN_OP_CONTINUE:
   case GEN_OP_ELSE:
   case GEN_OP_ENDIF:
   case GEN_OP_GOTO:
   case GEN_OP_HALT:
   case GEN_OP_IF:
   case GEN_OP_JMPI:
   case GEN_OP_JOIN:
   case GEN_OP_RET:
   case GEN_OP_WHILE:
      return GEN_FORMAT_BRANCH;

   case GEN_OP_DPAS:
      return GEN_FORMAT_DPAS_THREE_SRC;

   case GEN_OP_NOP:
      return GEN_FORMAT_NOP;

   default:
      return GEN_FORMAT_ILLEGAL;
   }
}

/* This needs to be applied to a gen_range to be used.
 * Avoids making the mistake of using fields directly.
 */
struct gen_sub_range {
   unsigned hi;
   unsigned lo;
};

struct gen_range {
   unsigned hi;
   unsigned lo;

   gen_range
   operator()(unsigned rel) const
   {
      assert(lo <= hi);
      assert(rel <= hi - lo);
      return { lo + rel, lo + rel };
   }

   gen_range
   operator()(unsigned rel_hi, unsigned rel_lo) const
   {
      // TODO: Move some of these assertions to user code (set/get).
      assert(lo <= hi);
      assert(rel_lo <= rel_hi);
      assert(rel_lo <= hi - lo);
      assert(rel_hi <= hi - lo);
      return { lo + rel_hi, lo + rel_lo };
   }

   gen_range
   operator()(gen_sub_range rel) const
   {
      assert(lo <= hi);
      assert(rel.lo <= rel.hi);
      assert(rel.lo <= hi - lo);
      assert(rel.hi <= hi - lo);
      return { lo + rel.hi, lo + rel.lo };
   }

   constexpr bool
   operator==(const gen_range &other) const
   {
      return hi == other.hi && lo == other.lo;
   }
};

inline bool
gen_inst_is_send(const gen_inst *inst)
{
   switch (inst->opcode) {
   case GEN_OP_SEND:
   case GEN_OP_SENDC:
   case GEN_OP_SENDS:
   case GEN_OP_SENDSC:
      return true;
   default:
      return false;
   }
}

inline bool
gen_inst_is_split_send(const intel_device_info *devinfo, const gen_inst *inst)
{
   switch (inst->opcode) {
   case GEN_OP_SEND:
   case GEN_OP_SENDC:
      return devinfo->ver >= 12;

   case GEN_OP_SENDS:
   case GEN_OP_SENDSC:
      return true;

   default:
      return false;
   }
}

inline unsigned
gen_inst_num_sources(const intel_device_info *devinfo, const gen_inst *inst)
{
   switch (gen_inst_format(inst->opcode)) {
   case GEN_FORMAT_BASIC_ONE_SRC:
      return 1;

   case GEN_FORMAT_BASIC_TWO_SRC:
      if (inst->opcode == GEN_OP_MATH) {
         // TODO: Create proper math function field!
         switch ((unsigned)inst->cond_modifier) {
         case BRW_MATH_FUNCTION_INV:
         case BRW_MATH_FUNCTION_LOG:
         case BRW_MATH_FUNCTION_EXP:
         case BRW_MATH_FUNCTION_SQRT:
         case BRW_MATH_FUNCTION_RSQ:
         case BRW_MATH_FUNCTION_SIN:
         case BRW_MATH_FUNCTION_COS:
         case GFX8_MATH_FUNCTION_INVM:
         case GFX8_MATH_FUNCTION_RSQRTM:
            return 1;
         case BRW_MATH_FUNCTION_FDIV:
         case BRW_MATH_FUNCTION_POW:
         case BRW_MATH_FUNCTION_INT_DIV_QUOTIENT_AND_REMAINDER:
         case BRW_MATH_FUNCTION_INT_DIV_QUOTIENT:
         case BRW_MATH_FUNCTION_INT_DIV_REMAINDER:
            return 2;
         default:
            UNREACHABLE("not reached");
         }
      }
      return 2;

   case GEN_FORMAT_BASIC_THREE_SRC:
   case GEN_FORMAT_DPAS_THREE_SRC:
      return 3;

   case GEN_FORMAT_SEND:
      return gen_inst_is_split_send(devinfo, inst) ? 2 : 1;

   case GEN_FORMAT_BRANCH:
   case GEN_FORMAT_NOP:
   case GEN_FORMAT_ILLEGAL:
      return 0;
   }
   UNREACHABLE("invalid gen inst");
}

constexpr inline bool
gen_inst_has_dst(const gen_format format, const gen_opcode op)
{
   return format != GEN_FORMAT_BRANCH &&
          format != GEN_FORMAT_ILLEGAL &&
          format != GEN_FORMAT_NOP &&
          op != GEN_OP_SYNC &&
          op != GEN_OP_SMOV;
}

inline bool
gen_inst_has_saturate(const gen_format format, const gen_inst *inst)
{
   return format == GEN_FORMAT_BASIC_ONE_SRC ||
          format == GEN_FORMAT_BASIC_TWO_SRC ||
          (format == GEN_FORMAT_BASIC_THREE_SRC && inst->opcode != GEN_OP_BFN) ||
          format == GEN_FORMAT_DPAS_THREE_SRC;
}

inline bool
gen_inst_has_cond_modifier(const gen_format format, const gen_inst *inst)
{
   // TODO: Check if we need devinfo <= 12 for this case....
   return (format == GEN_FORMAT_BASIC_ONE_SRC &&
           (inst->src[0].file != GEN_IMM || brw_type_size_bytes(inst->src[0].type) < 8)) ||
          format == GEN_FORMAT_BASIC_TWO_SRC ||
          format == GEN_FORMAT_BASIC_THREE_SRC ||
          format == GEN_FORMAT_DPAS_THREE_SRC;
}

inline bool
gen_inst_has_source_modifier(const gen_format format, const gen_inst *inst)
{
   // TODO: Complete this.
   switch (format) {
   case GEN_FORMAT_BASIC_ONE_SRC:
   case GEN_FORMAT_BASIC_TWO_SRC:
   case GEN_FORMAT_BASIC_THREE_SRC:
      return true;

   default:
      return false;
   }
}

#define STRIDE(stride) (stride != 0 ? 1 << ((stride) - 1) : 0)

inline unsigned
DST_STRIDE_3SRC(unsigned hstride)
{
   switch (hstride) {
   case BRW_ALIGN1_3SRC_DST_HORIZONTAL_STRIDE_1: return 1;
   case BRW_ALIGN1_3SRC_DST_HORIZONTAL_STRIDE_2: return 2;
   }
   UNREACHABLE("invalid hstride");
}

inline unsigned
DECODE_VSTRIDE_3SRC(unsigned vstride)
{
   switch (vstride) {
   case BRW_ALIGN1_3SRC_VERTICAL_STRIDE_0: return 0;
   case BRW_ALIGN1_3SRC_VERTICAL_STRIDE_1: return 1;
   case BRW_ALIGN1_3SRC_VERTICAL_STRIDE_4: return 4;
   case BRW_ALIGN1_3SRC_VERTICAL_STRIDE_8: return 8;
   }
   UNREACHABLE("invalid vstride");
}

inline unsigned
ENCODE_VSTRIDE_3SRC(unsigned vstride)
{
   switch (vstride) {
   case 0:  return BRW_ALIGN1_3SRC_VERTICAL_STRIDE_0;
   case 1:  return BRW_ALIGN1_3SRC_VERTICAL_STRIDE_1;
   case 4:  return BRW_ALIGN1_3SRC_VERTICAL_STRIDE_4;
   case 8:  return BRW_ALIGN1_3SRC_VERTICAL_STRIDE_8;
   case 16: return BRW_ALIGN1_3SRC_VERTICAL_STRIDE_8;
   }
   UNREACHABLE("invalid vstride");
}

inline bool
gen_region_is_scalar(const gen_region rgn)
{
   return rgn.vstride == 0 &&
          rgn.width == 1 &&
          rgn.hstride == 0;
}

/**
 * Returns whether a region is packed
 *
 * A region is packed if its elements are adjacent in memory, with no
 * intervening space, no overlap, and no replicated values.
 */
inline bool
gen_region_is_packed(const gen_region rgn)
{
   if (rgn.vstride == rgn.width) {
      if (rgn.vstride == 1)
         return rgn.hstride == 0;
      else
         return rgn.hstride == 1;
   }

   return false;
}

/**
 * Returns whether a region is linear
 *
 * A region is linear if its elements do not overlap and are not replicated.
 * Unlike a packed region, intervening space (i.e. strided values) is allowed.
 */
inline bool
gen_region_is_linear(const gen_region rgn)
{
   return (rgn.vstride == rgn.width * rgn.hstride) ||
          (rgn.hstride == 0 && rgn.width == 1);
}

// TODO: Use format branch_1src 2src?
inline bool
gen_has_uip(gen_opcode op)
{
   return op == GEN_OP_IF ||
          op == GEN_OP_ELSE ||
          op == GEN_OP_BREAK ||
          op == GEN_OP_CONTINUE ||
          op == GEN_OP_HALT ||
          op == GEN_OP_GOTO;
}

const char *gen_opcode_to_string(gen_opcode op);

static inline int
gen_raw_is_compact(const void *raw_bytes)
{
   /* Bit 29 always contains whether instruction is compacted or not. */
   const uint64_t *raw = (uint64_t *)raw_bytes;
   return *raw & (1 << 29u);
}

bool gen_encode_pre_xe(gen_encode_params *params);
bool gen_decode_pre_xe(gen_decode_params *params);

void gen_decode_inst_pre_xe(const intel_device_info *devinfo,
                            gen_inst *inst,
                            const gen_raw_inst *raw, char **error);



