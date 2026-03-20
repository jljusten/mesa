/*
 * Copyright © 2008 Keith Packard
 * Copyright © 2014 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "intel/compiler/gen/gen.h"

#include "dev/intel_debug.h"
#include "brw_disasm.h"
#include "brw_disasm_info.h"
#include "brw_eu_defines.h"
#include "brw_eu.h"
#include "brw_eu_inst.h"
#include "brw_isa_info.h"
#include "brw_reg.h"
#include "util/half_float.h"

bool
brw_has_jip(const struct intel_device_info *devinfo, enum opcode opcode)
{
   return opcode == BRW_OPCODE_IF ||
          opcode == BRW_OPCODE_ELSE ||
          opcode == BRW_OPCODE_ENDIF ||
          opcode == BRW_OPCODE_WHILE ||
          opcode == BRW_OPCODE_BREAK ||
          opcode == BRW_OPCODE_CONTINUE ||
          opcode == BRW_OPCODE_HALT ||
          opcode == BRW_OPCODE_GOTO ||
          opcode == BRW_OPCODE_JOIN;
}

bool
brw_has_uip(const struct intel_device_info *devinfo, enum opcode opcode)
{
   return opcode == BRW_OPCODE_IF ||
          opcode == BRW_OPCODE_ELSE ||
          opcode == BRW_OPCODE_BREAK ||
          opcode == BRW_OPCODE_CONTINUE ||
          opcode == BRW_OPCODE_HALT ||
          opcode == BRW_OPCODE_GOTO;
}

bool
brw_has_branch_ctrl(const struct intel_device_info *devinfo, enum opcode opcode)
{
   switch (opcode) {
   case BRW_OPCODE_IF:
   case BRW_OPCODE_ELSE:
   case BRW_OPCODE_GOTO:
   case BRW_OPCODE_BREAK:
   case BRW_OPCODE_CALL:
   case BRW_OPCODE_CALLA:
   case BRW_OPCODE_CONTINUE:
   case BRW_OPCODE_ENDIF:
   case BRW_OPCODE_HALT:
   case BRW_OPCODE_JMPI:
   case BRW_OPCODE_RET:
   case BRW_OPCODE_WHILE:
   case BRW_OPCODE_BRC:
   case BRW_OPCODE_BRD:
      /* TODO: "join" should also be here if added */
      return true;
   default:
      return false;
   }
}

static bool
is_logic_instruction(gen_opcode opcode)
{
   return opcode == GEN_OP_AND ||
          opcode == GEN_OP_NOT ||
          opcode == GEN_OP_OR ||
          opcode == GEN_OP_XOR;
}

static bool
is_send(gen_opcode opcode)
{
   return opcode == GEN_OP_SEND ||
          opcode == GEN_OP_SENDC ||
          opcode == GEN_OP_SENDS ||
          opcode == GEN_OP_SENDSC;
}

static bool
is_split_send(UNUSED const struct intel_device_info *devinfo, gen_opcode opcode)
{
   if (devinfo->ver >= 12)
      return is_send(opcode);
   else
      return opcode == GEN_OP_SENDS ||
             opcode == GEN_OP_SENDSC;
}

static bool gen_inst_has_dst_local(const gen_inst *inst);
static const char *gen_type_letters(gen_reg_type type);

static bool
is_send_gather(const struct brw_isa_info *isa, const gen_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;

   return devinfo->ver >= 30 &&
          is_split_send(devinfo, inst->opcode) &&
          inst->src[0].file == GEN_ARF &&
          inst->src[0].nr == GEN_ARF_SCALAR;
}

const char *const conditional_modifier[16] = {
   [BRW_CONDITIONAL_NONE] = "",
   [BRW_CONDITIONAL_Z]    = ".z",
   [BRW_CONDITIONAL_NZ]   = ".nz",
   [BRW_CONDITIONAL_G]    = ".g",
   [BRW_CONDITIONAL_GE]   = ".ge",
   [BRW_CONDITIONAL_L]    = ".l",
   [BRW_CONDITIONAL_LE]   = ".le",
   [BRW_CONDITIONAL_R]    = ".r",
   [BRW_CONDITIONAL_O]    = ".o",
   [BRW_CONDITIONAL_U]    = ".u",
};

static const char *const m_negate[2] = {
   [0] = "",
   [1] = "-",
};

static const char *const _abs[2] = {
   [0] = "",
   [1] = "(abs)",
};

static const char *const m_bitnot[2] = { "", "~" };

static const char *const chan_sel[4] = {
   [0] = "x",
   [1] = "y",
   [2] = "z",
   [3] = "w",
};

static const char *const debug_ctrl[2] = {
   [0] = "",
   [1] = ".breakpoint"
};

static const char *const saturate[2] = {
   [0] = "",
   [1] = ".sat"
};

static const char *const cmpt_ctrl[2] = {
   [0] = "",
   [1] = "compacted"
};

static const char *const accwr[2] = {
   [0] = "",
   [1] = "AccWrEnable"
};

static const char *const branch_ctrl[2] = {
   [0] = "",
   [1] = "BranchCtrl"
};

static const char *const fusion_ctrl[2] = {
   [0] = "",
   [1] = "FusionCtrl"
};

static const char *const wectrl[2] = {
   [0] = "",
   [1] = "WE_all"
};

static const char *const pred_inv[2] = {
   [0] = "+",
   [1] = "-"
};

const char *const pred_ctrl_align16[16] = {
   [1] = "",
   [2] = ".x",
   [3] = ".y",
   [4] = ".z",
   [5] = ".w",
   [6] = ".any4h",
   [7] = ".all4h",
};

static const char *const pred_ctrl_align1[16] = {
   [BRW_PREDICATE_NORMAL]        = "",
   [BRW_PREDICATE_ALIGN1_ANYV]   = ".anyv",
   [BRW_PREDICATE_ALIGN1_ALLV]   = ".allv",
   [BRW_PREDICATE_ALIGN1_ANY2H]  = ".any2h",
   [BRW_PREDICATE_ALIGN1_ALL2H]  = ".all2h",
   [BRW_PREDICATE_ALIGN1_ANY4H]  = ".any4h",
   [BRW_PREDICATE_ALIGN1_ALL4H]  = ".all4h",
   [BRW_PREDICATE_ALIGN1_ANY8H]  = ".any8h",
   [BRW_PREDICATE_ALIGN1_ALL8H]  = ".all8h",
   [BRW_PREDICATE_ALIGN1_ANY16H] = ".any16h",
   [BRW_PREDICATE_ALIGN1_ALL16H] = ".all16h",
   [BRW_PREDICATE_ALIGN1_ANY32H] = ".any32h",
   [BRW_PREDICATE_ALIGN1_ALL32H] = ".all32h",
};

static const char *const xe2_pred_ctrl[4] = {
   [BRW_PREDICATE_NORMAL]        = "",
   [XE2_PREDICATE_ANY]           = ".any",
   [XE2_PREDICATE_ALL]           = ".all",
};

static const char *const thread_ctrl[4] = {
   [BRW_THREAD_NORMAL] = "",
   [BRW_THREAD_ATOMIC] = "atomic",
   [BRW_THREAD_SWITCH] = "switch",
};

static const char *const dep_ctrl[4] = {
   [0] = "",
   [1] = "NoDDClr",
   [2] = "NoDDChk",
   [3] = "NoDDClr,NoDDChk",
};

static const char *const access_mode[2] = {
   [0] = "align1",
   [1] = "align16",
};

static const char *const writemask[16] = {
   [0x0] = ".",
   [0x1] = ".x",
   [0x2] = ".y",
   [0x3] = ".xy",
   [0x4] = ".z",
   [0x5] = ".xz",
   [0x6] = ".yz",
   [0x7] = ".xyz",
   [0x8] = ".w",
   [0x9] = ".xw",
   [0xa] = ".yw",
   [0xb] = ".xyw",
   [0xc] = ".zw",
   [0xd] = ".xzw",
   [0xe] = ".yzw",
   [0xf] = "",
};

static const char *const end_of_thread[2] = {
   [0] = "",
   [1] = "EOT"
};

static const char *const brw_sfid[16] = {
   [BRW_SFID_NULL]                  = "null",
   [BRW_SFID_SAMPLER]               = "sampler",
   [BRW_SFID_MESSAGE_GATEWAY]       = "gateway",
   [BRW_SFID_HDC2]                  = "hdc2",
   [BRW_SFID_RENDER_CACHE]          = "render",
   [BRW_SFID_URB]                   = "urb",
   [BRW_SFID_THREAD_SPAWNER]        = "ts/btd",
   [BRW_SFID_RAY_TRACE_ACCELERATOR] = "rt accel",
   [BRW_SFID_HDC_READ_ONLY]         = "hdc:ro",
   [BRW_SFID_HDC0]                  = "hdc0",
   [BRW_SFID_PIXEL_INTERPOLATOR]    = "pi",
   [BRW_SFID_HDC1]                  = "hdc1",
   [BRW_SFID_SLM]                   = "slm",
   [BRW_SFID_TGM]                   = "tgm",
   [BRW_SFID_UGM]                   = "ugm",
};

static const char *const gfx7_gateway_subfuncid[8] = {
   [BRW_MESSAGE_GATEWAY_SFID_OPEN_GATEWAY] = "open",
   [BRW_MESSAGE_GATEWAY_SFID_CLOSE_GATEWAY] = "close",
   [BRW_MESSAGE_GATEWAY_SFID_FORWARD_MSG] = "forward msg",
   [BRW_MESSAGE_GATEWAY_SFID_GET_TIMESTAMP] = "get timestamp",
   [BRW_MESSAGE_GATEWAY_SFID_BARRIER_MSG] = "barrier msg",
   [BRW_MESSAGE_GATEWAY_SFID_UPDATE_GATEWAY_STATE] = "update state",
   [BRW_MESSAGE_GATEWAY_SFID_MMIO_READ_WRITE] = "mmio read/write",
};

static const char *const dp_rc_msg_type_gfx9[16] = {
   [GFX9_DATAPORT_RC_RENDER_TARGET_WRITE] = "RT write",
   [GFX9_DATAPORT_RC_RENDER_TARGET_READ] = "RT read"
};

static const char *const *
dp_rc_msg_type(const struct intel_device_info *devinfo)
{
   return dp_rc_msg_type_gfx9;
}

static const char *const m_rt_write_subtype[] = {
   [0b000] = "SIMD16",
   [0b001] = "SIMD16/RepData",
   [0b010] = "SIMD8/DualSrcLow",
   [0b011] = "SIMD8/DualSrcHigh",
   [0b100] = "SIMD8",
   [0b101] = "SIMD8/ImageWrite",   /* Gfx6+ */
   [0b111] = "SIMD16/RepData-111", /* no idea how this is different than 1 */
};

static const char *const m_rt_write_subtype_xe2[] = {
   [0b000] = "SIMD16",
   [0b001] = "SIMD32",
   [0b010] = "SIMD16/DualSrc",
   [0b011] = "invalid",
   [0b100] = "invalid",
   [0b101] = "invalid",
   [0b111] = "invalid",
};

static const char *const dp_dc0_msg_type_gfx7[16] = {
   [GFX7_DATAPORT_DC_OWORD_BLOCK_READ] = "DC OWORD block read",
   [GFX7_DATAPORT_DC_UNALIGNED_OWORD_BLOCK_READ] =
      "DC unaligned OWORD block read",
   [GFX7_DATAPORT_DC_OWORD_DUAL_BLOCK_READ] = "DC OWORD dual block read",
   [GFX7_DATAPORT_DC_DWORD_SCATTERED_READ] = "DC DWORD scattered read",
   [GFX7_DATAPORT_DC_BYTE_SCATTERED_READ] = "DC byte scattered read",
   [GFX7_DATAPORT_DC_UNTYPED_SURFACE_READ] = "DC untyped surface read",
   [GFX7_DATAPORT_DC_UNTYPED_ATOMIC_OP] = "DC untyped atomic",
   [GFX7_DATAPORT_DC_MEMORY_FENCE] = "DC mfence",
   [GFX7_DATAPORT_DC_OWORD_BLOCK_WRITE] = "DC OWORD block write",
   [GFX7_DATAPORT_DC_OWORD_DUAL_BLOCK_WRITE] = "DC OWORD dual block write",
   [GFX7_DATAPORT_DC_DWORD_SCATTERED_WRITE] = "DC DWORD scatterd write",
   [GFX7_DATAPORT_DC_BYTE_SCATTERED_WRITE] = "DC byte scattered write",
   [GFX7_DATAPORT_DC_UNTYPED_SURFACE_WRITE] = "DC untyped surface write",
};

static const char *const dp_oword_block_rw[8] = {
      [BRW_DATAPORT_OWORD_BLOCK_1_OWORDLOW]  = "1-low",
      [BRW_DATAPORT_OWORD_BLOCK_1_OWORDHIGH] = "1-high",
      [BRW_DATAPORT_OWORD_BLOCK_2_OWORDS]    = "2",
      [BRW_DATAPORT_OWORD_BLOCK_4_OWORDS]    = "4",
      [BRW_DATAPORT_OWORD_BLOCK_8_OWORDS]    = "8",
};

static const char *const dp_dc1_msg_type_hsw[32] = {
   [HSW_DATAPORT_DC_PORT1_UNTYPED_SURFACE_READ] = "untyped surface read",
   [HSW_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_OP] = "DC untyped atomic op",
   [HSW_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_OP_SIMD4X2] =
      "DC untyped 4x2 atomic op",
   [HSW_DATAPORT_DC_PORT1_MEDIA_BLOCK_READ] = "DC media block read",
   [HSW_DATAPORT_DC_PORT1_TYPED_SURFACE_READ] = "DC typed surface read",
   [HSW_DATAPORT_DC_PORT1_TYPED_ATOMIC_OP] = "DC typed atomic",
   [HSW_DATAPORT_DC_PORT1_TYPED_ATOMIC_OP_SIMD4X2] = "DC typed 4x2 atomic op",
   [HSW_DATAPORT_DC_PORT1_UNTYPED_SURFACE_WRITE] = "DC untyped surface write",
   [HSW_DATAPORT_DC_PORT1_MEDIA_BLOCK_WRITE] = "DC media block write",
   [HSW_DATAPORT_DC_PORT1_ATOMIC_COUNTER_OP] = "DC atomic counter op",
   [HSW_DATAPORT_DC_PORT1_ATOMIC_COUNTER_OP_SIMD4X2] =
      "DC 4x2 atomic counter op",
   [HSW_DATAPORT_DC_PORT1_TYPED_SURFACE_WRITE] = "DC typed surface write",
   [GFX9_DATAPORT_DC_PORT1_A64_SCATTERED_READ] = "DC A64 scattered read",
   [GFX8_DATAPORT_DC_PORT1_A64_UNTYPED_SURFACE_READ] = "DC A64 untyped surface read",
   [GFX8_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_OP] = "DC A64 untyped atomic op",
   [GFX9_DATAPORT_DC_PORT1_A64_OWORD_BLOCK_READ] = "DC A64 oword block read",
   [GFX9_DATAPORT_DC_PORT1_A64_OWORD_BLOCK_WRITE] = "DC A64 oword block write",
   [GFX8_DATAPORT_DC_PORT1_A64_UNTYPED_SURFACE_WRITE] = "DC A64 untyped surface write",
   [GFX8_DATAPORT_DC_PORT1_A64_SCATTERED_WRITE] = "DC A64 scattered write",
   [GFX9_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_FLOAT_OP] =
      "DC untyped atomic float op",
   [GFX9_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_FLOAT_OP] =
      "DC A64 untyped atomic float op",
   [GFX12_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_HALF_INT_OP] =
      "DC A64 untyped atomic half-integer op",
   [GFX12_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_HALF_FLOAT_OP] =
      "DC A64 untyped atomic half-float op",
};

static const char *const aop[16] = {
   [BRW_AOP_AND]    = "and",
   [BRW_AOP_OR]     = "or",
   [BRW_AOP_XOR]    = "xor",
   [BRW_AOP_MOV]    = "mov",
   [BRW_AOP_INC]    = "inc",
   [BRW_AOP_DEC]    = "dec",
   [BRW_AOP_ADD]    = "add",
   [BRW_AOP_SUB]    = "sub",
   [BRW_AOP_REVSUB] = "revsub",
   [BRW_AOP_IMAX]   = "imax",
   [BRW_AOP_IMIN]   = "imin",
   [BRW_AOP_UMAX]   = "umax",
   [BRW_AOP_UMIN]   = "umin",
   [BRW_AOP_CMPWR]  = "cmpwr",
   [BRW_AOP_PREDEC] = "predec",
};

static const char *const aop_float[5] = {
   [BRW_AOP_FMAX]   = "fmax",
   [BRW_AOP_FMIN]   = "fmin",
   [BRW_AOP_FCMPWR] = "fcmpwr",
   [BRW_AOP_FADD]   = "fadd",
};

static const char * const pixel_interpolator_msg_types[4] = {
    [GFX7_PIXEL_INTERPOLATOR_LOC_SHARED_OFFSET] = "per_message_offset",
    [GFX7_PIXEL_INTERPOLATOR_LOC_SAMPLE] = "sample_position",
    [GFX7_PIXEL_INTERPOLATOR_LOC_CENTROID] = "centroid",
    [GFX7_PIXEL_INTERPOLATOR_LOC_PER_SLOT_OFFSET] = "per_slot_offset",
};

static const char *const math_function[16] = {
   [BRW_MATH_FUNCTION_INV]    = "inv",
   [BRW_MATH_FUNCTION_LOG]    = "log",
   [BRW_MATH_FUNCTION_EXP]    = "exp",
   [BRW_MATH_FUNCTION_SQRT]   = "sqrt",
   [BRW_MATH_FUNCTION_RSQ]    = "rsq",
   [BRW_MATH_FUNCTION_SIN]    = "sin",
   [BRW_MATH_FUNCTION_COS]    = "cos",
   [BRW_MATH_FUNCTION_FDIV]   = "fdiv",
   [BRW_MATH_FUNCTION_POW]    = "pow",
   [BRW_MATH_FUNCTION_INT_DIV_QUOTIENT_AND_REMAINDER] = "intdivmod",
   [BRW_MATH_FUNCTION_INT_DIV_QUOTIENT]  = "intdiv",
   [BRW_MATH_FUNCTION_INT_DIV_REMAINDER] = "intmod",
   [GFX8_MATH_FUNCTION_INVM]  = "invm",
   [GFX8_MATH_FUNCTION_RSQRTM] = "rsqrtm",
};

static const char *const sync_function[16] = {
   [TGL_SYNC_NOP] = "nop",
   [TGL_SYNC_ALLRD] = "allrd",
   [TGL_SYNC_ALLWR] = "allwr",
   [TGL_SYNC_FENCE] = "fence",
   [TGL_SYNC_BAR] = "bar",
   [TGL_SYNC_HOST] = "host",
};

static const char *const gfx7_urb_opcode[] = {
   [GFX7_URB_OPCODE_ATOMIC_MOV] = "atomic mov",  /* Gfx7+ */
   [GFX7_URB_OPCODE_ATOMIC_INC] = "atomic inc",  /* Gfx7+ */
   [GFX8_URB_OPCODE_ATOMIC_ADD] = "atomic add",  /* Gfx8+ */
   [GFX8_URB_OPCODE_SIMD8_WRITE] = "SIMD8 write", /* Gfx8+ */
   [GFX8_URB_OPCODE_SIMD8_READ] = "SIMD8 read",  /* Gfx8+ */
   [GFX125_URB_OPCODE_FENCE] = "fence",  /* Gfx12.5+ */
   /* [10-15] - reserved */
};

static const char *const urb_swizzle[4] = {
   [BRW_URB_SWIZZLE_NONE]       = "",
   [BRW_URB_SWIZZLE_INTERLEAVE] = "interleave",
   [BRW_URB_SWIZZLE_TRANSPOSE]  = "transpose",
};

static const char *const gfx5_sampler_msg_type[] = {
   [GFX5_SAMPLER_MESSAGE_SAMPLE]              = "sample",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_BIAS]         = "sample_b",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_LOD]          = "sample_l",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_COMPARE]      = "sample_c",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_DERIVS]       = "sample_d",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_BIAS_COMPARE] = "sample_b_c",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_LOD_COMPARE]  = "sample_l_c",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_LD]           = "ld",
   [GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4]      = "gather4",
   [GFX5_SAMPLER_MESSAGE_LOD]                 = "lod",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_RESINFO]      = "resinfo",
   [GFX6_SAMPLER_MESSAGE_SAMPLE_SAMPLEINFO]   = "sampleinfo",
   [GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4_C]    = "gather4_c",
   [GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO]   = "gather4_po",
   [GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_C] = "gather4_po_c",
   [HSW_SAMPLER_MESSAGE_SAMPLE_DERIV_COMPARE] = "sample_d_c",
   [GFX9_SAMPLER_MESSAGE_SAMPLE_LZ]           = "sample_lz",
   [GFX9_SAMPLER_MESSAGE_SAMPLE_C_LZ]         = "sample_c_lz",
   [GFX9_SAMPLER_MESSAGE_SAMPLE_LD_LZ]        = "ld_lz",
   [GFX9_SAMPLER_MESSAGE_SAMPLE_LD2DMS_W]     = "ld2dms_w",
   [GFX7_SAMPLER_MESSAGE_SAMPLE_LD_MCS]       = "ld_mcs",
   [GFX7_SAMPLER_MESSAGE_SAMPLE_LD2DMS]       = "ld2dms",
   [GFX7_SAMPLER_MESSAGE_SAMPLE_LD2DSS]       = "ld2dss",
};

static const char *const xe2_sampler_msg_type[] = {
   [GFX5_SAMPLER_MESSAGE_SAMPLE]              = "sample",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_BIAS]         = "sample_b",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_LOD]          = "sample_l",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_COMPARE]      = "sample_c",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_DERIVS]       = "sample_d",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_BIAS_COMPARE] = "sample_b_c",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_LOD_COMPARE]  = "sample_l_c",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_LD]           = "ld",
   [GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4]      = "gather4",
   [GFX5_SAMPLER_MESSAGE_LOD]                 = "lod",
   [GFX5_SAMPLER_MESSAGE_SAMPLE_RESINFO]      = "resinfo",
   [GFX6_SAMPLER_MESSAGE_SAMPLE_SAMPLEINFO]   = "sampleinfo",
   [GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4_C]    = "gather4_c",
   [GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO]   = "gather4_po",
   [XE2_SAMPLER_MESSAGE_SAMPLE_MLOD]          = "sample_mlod",
   [XE2_SAMPLER_MESSAGE_SAMPLE_COMPARE_MLOD]  = "sample_c_mlod",
   [XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I]    = "gather4_i",
   [XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L]    = "gather4_l",
   [XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_B]    = "gather4_b",
   [XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I_C]  = "gather4_i_c",
   [XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L_C]  = "gather4_l_c",
   [XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_C] = "gather4_po_c",
   [XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_I] = "gather4_po_i",
   [XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_L] = "gather4_po_l",
   [XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_I_C] = "gather4_po_i_c",
   [XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_L_C] = "gather4_po_l_c",
   [XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_B]  = "gather4_po_b",
   [HSW_SAMPLER_MESSAGE_SAMPLE_DERIV_COMPARE] = "sample_d_c",
   [GFX9_SAMPLER_MESSAGE_SAMPLE_LZ]           = "sample_lz",
   [GFX9_SAMPLER_MESSAGE_SAMPLE_C_LZ]         = "sample_c_lz",
   [GFX9_SAMPLER_MESSAGE_SAMPLE_LD_LZ]        = "ld_lz",
   [GFX9_SAMPLER_MESSAGE_SAMPLE_LD2DMS_W]     = "ld2dms_w",
   [GFX7_SAMPLER_MESSAGE_SAMPLE_LD_MCS]       = "ld_mcs",
   [GFX7_SAMPLER_MESSAGE_SAMPLE_LD2DMS]       = "ld2dms",
   [GFX7_SAMPLER_MESSAGE_SAMPLE_LD2DSS]       = "ld2dss",
   [XE2_SAMPLER_MESSAGE_SAMPLE_PO]                 = "sample_po",
   [XE2_SAMPLER_MESSAGE_SAMPLE_PO_BIAS]            = "sample_po_b",
   [XE2_SAMPLER_MESSAGE_SAMPLE_PO_LOD]             = "sample_po_l",
   [XE2_SAMPLER_MESSAGE_SAMPLE_PO_COMPARE]         = "sample_po_c",
   [XE2_SAMPLER_MESSAGE_SAMPLE_PO_DERIVS]          = "sample_po_d",
   [XE3_SAMPLER_MESSAGE_SAMPLE_PO_BIAS_COMPARE]    = "sample_po_b_c",
   [XE2_SAMPLER_MESSAGE_SAMPLE_PO_LOD_COMPARE]     = "sample_po_l_c",
   [XE2_SAMPLER_MESSAGE_SAMPLE_PO_D_C]             = "sample_po_d_c",
   [XE2_SAMPLER_MESSAGE_SAMPLE_PO_LZ]              = "sample_po_lz",
   [XE2_SAMPLER_MESSAGE_SAMPLE_PO_C_LZ]            = "sample_po_c_lz",
};

static const char *const gfx5_sampler_simd_mode[7] = {
   [BRW_SAMPLER_SIMD_MODE_SIMD4X2]   = "SIMD4x2",
   [BRW_SAMPLER_SIMD_MODE_SIMD8]     = "SIMD8",
   [BRW_SAMPLER_SIMD_MODE_SIMD16]    = "SIMD16",
   [BRW_SAMPLER_SIMD_MODE_SIMD32_64] = "SIMD32/64",
   [GFX10_SAMPLER_SIMD_MODE_SIMD8H]  = "SIMD8H",
   [GFX10_SAMPLER_SIMD_MODE_SIMD16H] = "SIMD16H",
};

static const char *const xe2_sampler_simd_mode[7] = {
   [XE2_SAMPLER_SIMD_MODE_SIMD16]  = "SIMD16",
   [XE2_SAMPLER_SIMD_MODE_SIMD32]  = "SIMD32",
   [XE2_SAMPLER_SIMD_MODE_SIMD16H] = "SIMD16H",
   [XE2_SAMPLER_SIMD_MODE_SIMD32H] = "SIMD32H",
};

static const char *const lsc_operation[] = {
   [LSC_OP_LOAD]            = "load",
   [LSC_OP_LOAD_CMASK]      = "load_cmask",
   [LSC_OP_STORE]           = "store",
   [LSC_OP_STORE_CMASK]     = "store_cmask",
   [LSC_OP_FENCE]           = "fence",
   [LSC_OP_ATOMIC_INC]      = "atomic_inc",
   [LSC_OP_ATOMIC_DEC]      = "atomic_dec",
   [LSC_OP_ATOMIC_LOAD]     = "atomic_load",
   [LSC_OP_ATOMIC_STORE]    = "atomic_store",
   [LSC_OP_ATOMIC_ADD]      = "atomic_add",
   [LSC_OP_ATOMIC_SUB]      = "atomic_sub",
   [LSC_OP_ATOMIC_MIN]      = "atomic_min",
   [LSC_OP_ATOMIC_MAX]      = "atomic_max",
   [LSC_OP_ATOMIC_UMIN]     = "atomic_umin",
   [LSC_OP_ATOMIC_UMAX]     = "atomic_umax",
   [LSC_OP_ATOMIC_CMPXCHG]  = "atomic_cmpxchg",
   [LSC_OP_ATOMIC_FADD]     = "atomic_fadd",
   [LSC_OP_ATOMIC_FSUB]     = "atomic_fsub",
   [LSC_OP_ATOMIC_FMIN]     = "atomic_fmin",
   [LSC_OP_ATOMIC_FMAX]     = "atomic_fmax",
   [LSC_OP_ATOMIC_FCMPXCHG] = "atomic_fcmpxchg",
   [LSC_OP_ATOMIC_AND]      = "atomic_and",
   [LSC_OP_ATOMIC_OR]       = "atomic_or",
   [LSC_OP_ATOMIC_XOR]      = "atomic_xor",
   [LSC_OP_LOAD_CMASK_MSRT] = "load_cmask_msrt",
   [LSC_OP_STORE_CMASK_MSRT] = "store_cmask_msrt",
};

const char *
brw_lsc_op_to_string(unsigned op)
{
   assert(op < ARRAY_SIZE(lsc_operation));
   return lsc_operation[op];
}

static const char *const lsc_addr_surface_type[] = {
   [LSC_ADDR_SURFTYPE_FLAT] = "flat",
   [LSC_ADDR_SURFTYPE_BSS]  = "bss",
   [LSC_ADDR_SURFTYPE_SS]   = "ss",
   [LSC_ADDR_SURFTYPE_BTI]  = "bti",
};

const char *
brw_lsc_addr_surftype_to_string(unsigned t)
{
   assert(t < ARRAY_SIZE(lsc_addr_surface_type));
   return lsc_addr_surface_type[t];
}

static const char* const lsc_fence_scope[] = {
   [LSC_FENCE_THREADGROUP]     = "threadgroup",
   [LSC_FENCE_LOCAL]           = "local",
   [LSC_FENCE_TILE]            = "tile",
   [LSC_FENCE_GPU]             = "gpu",
   [LSC_FENCE_ALL_GPU]         = "all_gpu",
   [LSC_FENCE_SYSTEM_RELEASE]  = "system_release",
   [LSC_FENCE_SYSTEM_ACQUIRE]  = "system_acquire",
};

static const char* const lsc_flush_type[] = {
   [LSC_FLUSH_TYPE_NONE]       = "none",
   [LSC_FLUSH_TYPE_EVICT]      = "evict",
   [LSC_FLUSH_TYPE_INVALIDATE] = "invalidate",
   [LSC_FLUSH_TYPE_DISCARD]    = "discard",
   [LSC_FLUSH_TYPE_CLEAN]      = "clean",
   [LSC_FLUSH_TYPE_L3ONLY]     = "l3only",
   [LSC_FLUSH_TYPE_NONE_6]     = "none_6",
};

static const char* const lsc_addr_size[] = {
   [LSC_ADDR_SIZE_A16] = "a16",
   [LSC_ADDR_SIZE_A32] = "a32",
   [LSC_ADDR_SIZE_A64] = "a64",
};

static const char* const lsc_backup_fence_routing[] = {
   [LSC_NORMAL_ROUTING]  = "normal_routing",
   [LSC_ROUTE_TO_LSC]    = "route_to_lsc",
};

static const char* const lsc_data_size[] = {
   [LSC_DATA_SIZE_D8]      = "d8",
   [LSC_DATA_SIZE_D16]     = "d16",
   [LSC_DATA_SIZE_D32]     = "d32",
   [LSC_DATA_SIZE_D64]     = "d64",
   [LSC_DATA_SIZE_D8U32]   = "d8u32",
   [LSC_DATA_SIZE_D16U32]  = "d16u32",
   [LSC_DATA_SIZE_D16BF32] = "d16bf32",
};

const char *
brw_lsc_data_size_to_string(unsigned s)
{
   assert(s < ARRAY_SIZE(lsc_data_size));
   return lsc_data_size[s];
}

static const char* const lsc_vect_size_str[] = {
   [LSC_VECT_SIZE_V1] = "V1",
   [LSC_VECT_SIZE_V2] = "V2",
   [LSC_VECT_SIZE_V3] = "V3",
   [LSC_VECT_SIZE_V4] = "V4",
   [LSC_VECT_SIZE_V8] = "V8",
   [LSC_VECT_SIZE_V16] = "V16",
   [LSC_VECT_SIZE_V32] = "V32",
   [LSC_VECT_SIZE_V64] = "V64",
};

static const char* const lsc_cmask_str[] = {
   [LSC_CMASK_X]      = "x",
   [LSC_CMASK_Y]      = "y",
   [LSC_CMASK_XY]     = "xy",
   [LSC_CMASK_Z]      = "z",
   [LSC_CMASK_XZ]     = "xz",
   [LSC_CMASK_YZ]     = "yz",
   [LSC_CMASK_XYZ]    = "xyz",
   [LSC_CMASK_W]      = "w",
   [LSC_CMASK_XW]     = "xw",
   [LSC_CMASK_YW]     = "yw",
   [LSC_CMASK_XYW]    = "xyw",
   [LSC_CMASK_ZW]     = "zw",
   [LSC_CMASK_XZW]    = "xzw",
   [LSC_CMASK_YZW]    = "yzw",
   [LSC_CMASK_XYZW]   = "xyzw",
};

static const char* const lsc_cache_load[] = {
   [LSC_CACHE_LOAD_L1STATE_L3MOCS]   = "L1STATE_L3MOCS",
   [LSC_CACHE_LOAD_L1UC_L3UC]        = "L1UC_L3UC",
   [LSC_CACHE_LOAD_L1UC_L3C]         = "L1UC_L3C",
   [LSC_CACHE_LOAD_L1C_L3UC]         = "L1C_L3UC",
   [LSC_CACHE_LOAD_L1C_L3C]          = "L1C_L3C",
   [LSC_CACHE_LOAD_L1S_L3UC]         = "L1S_L3UC",
   [LSC_CACHE_LOAD_L1S_L3C]          = "L1S_L3C",
   [LSC_CACHE_LOAD_L1IAR_L3C]        = "L1IAR_L3C",
};

static const char* const lsc_cache_store[] = {
   [LSC_CACHE_STORE_L1STATE_L3MOCS]  = "L1STATE_L3MOCS",
   [LSC_CACHE_STORE_L1UC_L3UC]       = "L1UC_L3UC",
   [LSC_CACHE_STORE_L1UC_L3WB]       = "L1UC_L3WB",
   [LSC_CACHE_STORE_L1WT_L3UC]       = "L1WT_L3UC",
   [LSC_CACHE_STORE_L1WT_L3WB]       = "L1WT_L3WB",
   [LSC_CACHE_STORE_L1S_L3UC]        = "L1S_L3UC",
   [LSC_CACHE_STORE_L1S_L3WB]        = "L1S_L3WB",
   [LSC_CACHE_STORE_L1WB_L3WB]       = "L1WB_L3WB",
};

static const char* const xe2_lsc_cache_load[] = {
   [XE2_LSC_CACHE_LOAD_L1STATE_L3MOCS]   = "L1STATE_L3MOCS",
   [XE2_LSC_CACHE_LOAD_L1UC_L3UC]        = "L1UC_L3UC",
   [XE2_LSC_CACHE_LOAD_L1UC_L3C]         = "L1UC_L3C",
   [XE2_LSC_CACHE_LOAD_L1UC_L3CC]        = "L1UC_L3CC",
   [XE2_LSC_CACHE_LOAD_L1C_L3UC]         = "L1C_L3UC",
   [XE2_LSC_CACHE_LOAD_L1C_L3C]          = "L1C_L3C",
   [XE2_LSC_CACHE_LOAD_L1C_L3CC]         = "L1C_L3CC",
   [XE2_LSC_CACHE_LOAD_L1S_L3UC]         = "L1S_L3UC",
   [XE2_LSC_CACHE_LOAD_L1S_L3C]          = "L1S_L3C",
   [XE2_LSC_CACHE_LOAD_L1IAR_L3IAR]      = "L1IAR_L3IAR",
};

static const char* const xe2_lsc_cache_store[] = {
   [XE2_LSC_CACHE_STORE_L1STATE_L3MOCS]  = "L1STATE_L3MOCS",
   [XE2_LSC_CACHE_STORE_L1UC_L3UC]       = "L1UC_L3UC",
   [XE2_LSC_CACHE_STORE_L1UC_L3WB]       = "L1UC_L3WB",
   [XE2_LSC_CACHE_STORE_L1WT_L3UC]       = "L1WT_L3UC",
   [XE2_LSC_CACHE_STORE_L1WT_L3WB]       = "L1WT_L3WB",
   [XE2_LSC_CACHE_STORE_L1S_L3UC]        = "L1S_L3UC",
   [XE2_LSC_CACHE_STORE_L1S_L3WB]        = "L1S_L3WB",
   [XE2_LSC_CACHE_STORE_L1WB_L3WB]       = "L1WB_L3WB",
};

static int column;

static int
string(FILE *file, const char *string)
{
   fputs(string, file);
   column += strlen(string);
   return 0;
}

static int
format(FILE *f, const char *format, ...) PRINTFLIKE(2, 3);

static int
format(FILE *f, const char *format, ...)
{
   char buf[1024];
   va_list args;
   va_start(args, format);

   vsnprintf(buf, sizeof(buf) - 1, format, args);
   va_end(args);
   string(f, buf);
   return 0;
}

static int
newline(FILE *f)
{
   putc('\n', f);
   column = 0;
   return 0;
}

static int
pad(FILE *f, int c)
{
   do
      string(f, " ");
   while (column < c);
   return 0;
}

static int
control(FILE *file, const char *name, const char *const ctrl[],
        unsigned id, int *space)
{
   if (!ctrl[id]) {
      fprintf(file, "*** invalid %s value %d ", name, id);
      return 1;
   }
   if (ctrl[id][0]) {
      if (space && *space)
         string(file, " ");
      string(file, ctrl[id]);
      if (space)
         *space = 1;
   }
   return 0;
}

static int
print_opcode(FILE *file, UNUSED const struct brw_isa_info *isa,
             gen_opcode id)
{
   const char *name = gen_opcode_to_string(id);
   if (!name) {
      format(file, "*** invalid opcode value %d ", (int)id);
      return 1;
   }
   string(file, name);
   return 0;
}

static int
reg(FILE *file, gen_file _reg_file, unsigned _reg_nr)
{
   if (_reg_file == GEN_ARF) {
      switch (_reg_nr & 0xf0) {
      case GEN_ARF_NULL:
         string(file, "null");
         break;
      case GEN_ARF_ADDRESS:
         format(file, "a%d", _reg_nr & 0x0f);
         break;
      case GEN_ARF_ACCUMULATOR:
         format(file, "acc%d", _reg_nr & 0x0f);
         break;
      case GEN_ARF_FLAG:
         format(file, "f%d", _reg_nr & 0x0f);
         break;
      case GEN_ARF_MASK:
         format(file, "mask%d", _reg_nr & 0x0f);
         break;
      case GEN_ARF_STATE:
         format(file, "sr%d", _reg_nr & 0x0f);
         break;
      case GEN_ARF_SCALAR:
         format(file, "s%d", _reg_nr & 0x0f);
         break;
      case GEN_ARF_CONTROL:
         format(file, "cr%d", _reg_nr & 0x0f);
         break;
      case GEN_ARF_NOTIFICATION_COUNT:
         format(file, "n%d", _reg_nr & 0x0f);
         break;
      case GEN_ARF_IP:
         string(file, "ip");
         return -1;
      case GEN_ARF_TDR:
         format(file, "tdr0");
         return -1;
      case GEN_ARF_TIMESTAMP:
         format(file, "tm%d", _reg_nr & 0x0f);
         break;
      default:
         format(file, "ARF%d", _reg_nr);
         break;
      }
   } else {
      assert(_reg_file == GEN_GRF);
      format(file, "g%d", _reg_nr);
   }

   return 0;
}

static int
dest(FILE *file, const struct brw_isa_info *isa, const gen_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;
   const gen_opcode opcode = inst->opcode;
   const unsigned elem_size = gen_type_size_bytes(inst->dst.type);
   int err = 0;

   if (is_split_send(devinfo, opcode)) {
      if (inst->dst.indirect) {
         string(file, "g[a0");
         if (inst->dst.subnr)
            format(file, ".1");
         if (inst->dst.addr_imm)
            format(file, " %d", inst->dst.addr_imm);
         string(file, "]");
         return 0;
      }

      err |= reg(file, inst->dst.file, inst->dst.nr);
      if (err == -1)
         return 0;
      if (inst->dst.subnr)
         format(file, ".1");
      return err;
   }

   if (!inst->align16) {
      if (!inst->dst.indirect) {
         err |= reg(file, inst->dst.file, inst->dst.nr);
         if (err == -1)
            return 0;
         if (is_send(opcode))
            return err;
         if (inst->dst.subnr)
            format(file, ".%u", inst->dst.subnr / elem_size);
         string(file, "<");
         format(file, "%u", inst->dst.region.hstride);
         string(file, ">");
         string(file, gen_type_letters(inst->dst.type));
      } else {
         string(file, "g[a0");
         if (inst->dst.subnr)
            format(file, ".%u", inst->dst.subnr / elem_size);
         if (inst->dst.addr_imm)
            format(file, " %d", inst->dst.addr_imm);
         string(file, "]<");
         format(file, "%u", inst->dst.region.hstride);
         string(file, ">");
         string(file, gen_type_letters(inst->dst.type));
      }
   } else {
      if (!inst->dst.indirect) {
         err |= reg(file, inst->dst.file, inst->dst.nr);
         if (err == -1)
            return 0;
         if (inst->dst.subnr)
            format(file, ".%u", 16 / elem_size);
         string(file, "<1>");
         err |= control(file, "writemask", writemask, inst->dst.writemask, NULL);
         string(file, gen_type_letters(inst->dst.type));
      } else {
         err = 1;
         string(file, "Indirect align16 address mode not supported");
      }
   }

   return err;
}

static int
gen_vert_stride(FILE *file, unsigned vstride)
{
   switch (vstride) {
   case 0:
   case 1:
   case 2:
   case 4:
   case 8:
   case 16:
   case 32:
      format(file, "%u", vstride);
      return 0;
   case GEN_VSTRIDE_ONE_DIMENSIONAL:
      string(file, "VxH");
      return 0;
   default:
      format(file, "*** invalid vert stride value %u ", vstride);
      return 1;
   }
}

static int
dest_3src(FILE *file, const struct intel_device_info *devinfo,
              const gen_inst *inst)
{
   const bool is_align1 = !inst->align16;
   const gen_operand *dst = &inst->dst;
   unsigned hstride_value = dst->region.hstride;
   int err = 0;

   if (devinfo->ver < 10 && is_align1)
      return 0;

   err |= reg(file, dst->file, dst->nr);
   if (err == -1)
      return 0;

   if (dst->subnr)
      format(file, ".%u", dst->subnr / gen_type_size_bytes(dst->type));

   if (inst->align16 && hstride_value == 0)
      hstride_value = 1;

   string(file, "<");
   format(file, "%u", hstride_value);
   string(file, ">");

   if (!is_align1)
      err |= control(file, "writemask", writemask, dst->writemask, NULL);

   string(file, gen_type_letters(dst->type));
   return err;
}

static int
dest_dpas_3src(FILE *file, const gen_inst *inst)
{
   const gen_operand *dst = &inst->dst;

   if (reg(file, dst->file, dst->nr) == -1)
      return 0;

   if (dst->subnr)
      format(file, ".%u", dst->subnr);
   string(file, "<1>");
   string(file, gen_type_letters(dst->type));
   return 0;
}

static int
src_align1_region(FILE *file,
                      unsigned vstride_value,
                      unsigned width_value,
                      unsigned hstride_value)
{
   int err = 0;
   string(file, "<");
   err |= gen_vert_stride(file, vstride_value);
   string(file, ",");
   format(file, "%u", width_value);
   string(file, ",");
   format(file, "%u", hstride_value);
   string(file, ">");
   return err;
}

static int
src_da1(FILE *file, gen_opcode opcode, const gen_operand *src)
{
   int err = 0;

   if (is_logic_instruction(opcode))
      err |= control(file, "bitnot", m_bitnot, src->negate, NULL);
   else
      err |= control(file, "negate", m_negate, src->negate, NULL);

   err |= control(file, "abs", _abs, src->abs, NULL);

   err |= reg(file, src->file, src->nr);
   if (err == -1)
      return 0;

   if (is_send(opcode))
      return err;

   if (src->subnr) {
      unsigned elem_size = gen_type_size_bytes(src->type);
      format(file, ".%u", src->subnr / elem_size);
   }

   err |= src_align1_region(file, src->region.vstride,
                                src->region.width,
                                src->region.hstride);
   string(file, gen_type_letters(src->type));
   return err;
}

static int
src_ia1(FILE *file,
        gen_opcode opcode,
        const gen_operand *src)
{
   int err = 0;

   if (is_logic_instruction(opcode))
      err |= control(file, "bitnot", m_bitnot, src->negate, NULL);
   else
      err |= control(file, "negate", m_negate, src->negate, NULL);

   err |= control(file, "abs", _abs, src->abs, NULL);

   string(file, "g[a0");
   if (src->subnr)
      format(file, ".%u", src->subnr);
   if (src->addr_imm)
      format(file, " %d", src->addr_imm);
   string(file, "]");
   err |= src_align1_region(file, src->region.vstride,
                                src->region.width,
                                src->region.hstride);
   string(file, gen_type_letters(src->type));
   return err;
}

static int
src_swizzle(FILE *file, unsigned swiz)
{
   unsigned x = BRW_GET_SWZ(swiz, BRW_CHANNEL_X);
   unsigned y = BRW_GET_SWZ(swiz, BRW_CHANNEL_Y);
   unsigned z = BRW_GET_SWZ(swiz, BRW_CHANNEL_Z);
   unsigned w = BRW_GET_SWZ(swiz, BRW_CHANNEL_W);
   int err = 0;

   if (x == y && x == z && x == w) {
      string(file, ".");
      err |= control(file, "channel select", chan_sel, x, NULL);
   } else if (swiz != BRW_SWIZZLE_XYZW) {
      string(file, ".");
      err |= control(file, "channel select", chan_sel, x, NULL);
      err |= control(file, "channel select", chan_sel, y, NULL);
      err |= control(file, "channel select", chan_sel, z, NULL);
      err |= control(file, "channel select", chan_sel, w, NULL);
   }

   return err;
}

static int
src_da16(FILE *file,
         gen_opcode opcode,
         const gen_operand *src)
{
   int err = 0;

   if (is_logic_instruction(opcode))
      err |= control(file, "bitnot", m_bitnot, src->negate, NULL);
   else
      err |= control(file, "negate", m_negate, src->negate, NULL);

   err |= control(file, "abs", _abs, src->abs, NULL);

   err |= reg(file, src->file, src->nr);
   if (err == -1)
      return 0;
   if (src->subnr) {
      unsigned elem_size = gen_type_size_bytes(src->type);
      format(file, ".%u", 16 / elem_size);
   }
   string(file, "<");
   err |= gen_vert_stride(file, src->region.vstride);
   string(file, ">");
   err |= src_swizzle(file, src->swizzle);
   string(file, gen_type_letters(src->type));
   return err;
}

static int
src0_3src(FILE *file, const struct intel_device_info *devinfo,
              const gen_inst *inst)
{
   const bool is_align1 = !inst->align16;
   const gen_operand *src = &inst->src[0];
   int err = 0;

   if (devinfo->ver < 10 && is_align1)
      return 0;

   if (is_align1 && src->file == GEN_IMM) {
      switch (src->type) {
      case GEN_TYPE_W:
         format(file, "%dW", (int16_t) src->imm);
         break;
      case GEN_TYPE_UW:
         format(file, "0x%04xUW", (uint16_t) src->imm);
         break;
      case GEN_TYPE_HF:
         format(file, "0x%04xHF", (uint16_t) src->imm);
         break;
      default:
         format(file, "*** invalid immediate type %d ", src->type);
         break;
      }
      return 0;
   }

   err |= control(file, "negate", m_negate, src->negate, NULL);
   err |= control(file, "abs", _abs, src->abs, NULL);

   err |= reg(file, src->file, src->nr);
   if (err == -1)
      return 0;

   if (src->subnr || (src->region.vstride == 0 && src->region.width == 1 &&
                      src->region.hstride == 0)) {
      format(file, ".%u", src->subnr / gen_type_size_bytes(src->type));
   }

   if (is_align1) {
      err |= src_align1_region(file, src->region.vstride,
                                   src->region.width,
                                   src->region.hstride);
   } else {
      const unsigned vstride_value = src->rep_ctrl ? 0 : 4;
      const unsigned width_value = src->rep_ctrl ? 1 : 4;
      const unsigned hstride_value = src->rep_ctrl ? 0 : 1;
      err |= src_align1_region(file, vstride_value, width_value, hstride_value);
      if (!src->rep_ctrl)
         err |= src_swizzle(file, src->swizzle);
   }

   string(file, gen_type_letters(src->type));
   return err;
}

static int
src1_3src(FILE *file, const struct brw_isa_info *isa, const gen_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;
   const bool is_align1 = !inst->align16;
   const gen_operand *src = &inst->src[1];
   const gen_opcode opcode = inst->opcode;
   int err = 0;

   if (devinfo->ver < 10 && is_align1)
      return 0;

   if (opcode != GEN_OP_BFN) {
      err |= control(file, "negate", m_negate, src->negate, NULL);
      err |= control(file, "abs", _abs, src->abs, NULL);
   }

   err |= reg(file, src->file, src->nr);
   if (err == -1)
      return 0;

   if (src->subnr || (src->region.vstride == 0 && src->region.width == 1 &&
                      src->region.hstride == 0)) {
      format(file, ".%u", src->subnr / gen_type_size_bytes(src->type));
   }

   if (is_align1) {
      err |= src_align1_region(file, src->region.vstride,
                                   src->region.width,
                                   src->region.hstride);
   } else {
      const unsigned vstride_value = src->rep_ctrl ? 0 : 4;
      const unsigned width_value = src->rep_ctrl ? 1 : 4;
      const unsigned hstride_value = src->rep_ctrl ? 0 : 1;
      err |= src_align1_region(file, vstride_value, width_value, hstride_value);
      if (!src->rep_ctrl)
         err |= src_swizzle(file, src->swizzle);
   }

   string(file, gen_type_letters(src->type));
   return err;
}

static int
src2_3src(FILE *file, const struct brw_isa_info *isa, const gen_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;
   const bool is_align1 = !inst->align16;
   const gen_operand *src = &inst->src[2];
   const gen_opcode opcode = inst->opcode;
   int err = 0;

   if (devinfo->ver < 10 && is_align1)
      return 0;

   if (is_align1 && src->file == GEN_IMM) {
      switch (src->type) {
      case GEN_TYPE_W:
         format(file, "%dW", (int16_t) src->imm);
         break;
      case GEN_TYPE_UW:
         format(file, "0x%04xUW", (uint16_t) src->imm);
         break;
      case GEN_TYPE_HF:
         format(file, "0x%04xHF", (uint16_t) src->imm);
         break;
      default:
         format(file, "*** invalid immediate type %d ", src->type);
         break;
      }
      return 0;
   }

   if (opcode != GEN_OP_BFN) {
      err |= control(file, "negate", m_negate, src->negate, NULL);
      err |= control(file, "abs", _abs, src->abs, NULL);
   }

   err |= reg(file, src->file, src->nr);
   if (err == -1)
      return 0;

   if (src->subnr || (src->region.vstride == 0 && src->region.width == 1 &&
                      src->region.hstride == 0)) {
      format(file, ".%u", src->subnr / gen_type_size_bytes(src->type));
   }

   if (is_align1) {
      err |= src_align1_region(file, src->region.vstride,
                                   src->region.width,
                                   src->region.hstride);
   } else {
      const unsigned vstride_value = src->rep_ctrl ? 0 : 4;
      const unsigned width_value = src->rep_ctrl ? 1 : 4;
      const unsigned hstride_value = src->rep_ctrl ? 0 : 1;
      err |= src_align1_region(file, vstride_value, width_value, hstride_value);
      if (!src->rep_ctrl)
         err |= src_swizzle(file, src->swizzle);
   }

   string(file, gen_type_letters(src->type));
   return err;
}

static int
src_dpas_3src(FILE *file, const gen_operand *src)
{
   if (reg(file, src->file, src->nr) == -1)
      return 0;

   if (src->subnr)
      format(file, ".%u", src->subnr);
   src_align1_region(file, 1, 1, 0);
   string(file, gen_type_letters(src->type));
   return 0;
}

static int
src0_dpas_3src(FILE *file, UNUSED const struct intel_device_info *devinfo,
               const gen_inst *inst)
{
   return src_dpas_3src(file, &inst->src[0]);
}

static int
src1_dpas_3src(FILE *file, UNUSED const struct intel_device_info *devinfo,
               const gen_inst *inst)
{
   return src_dpas_3src(file, &inst->src[1]);
}

static int
src2_dpas_3src(FILE *file, UNUSED const struct intel_device_info *devinfo,
               const gen_inst *inst)
{
   return src_dpas_3src(file, &inst->src[2]);
}

static int
imm(FILE *file, gen_reg_type type, uint64_t imm)
{
   union {
      uint32_t u32;
      float f;
   } f32 = { .u32 = (uint32_t) imm };
   union {
      uint64_t u64;
      double df;
   } f64 = { .u64 = imm };

   switch (type) {
   case GEN_TYPE_UQ:
      format(file, "0x%016"PRIx64"UQ", imm);
      break;
   case GEN_TYPE_Q:
      format(file, "0x%016"PRIx64"Q", imm);
      break;
   case GEN_TYPE_UD:
      format(file, "0x%08xUD", (uint32_t) imm);
      break;
   case GEN_TYPE_D:
      format(file, "%dD", (int32_t) imm);
      break;
   case GEN_TYPE_UW:
      format(file, "0x%04xUW", (uint16_t) imm);
      break;
   case GEN_TYPE_W:
      format(file, "%dW", (int16_t) imm);
      break;
   case GEN_TYPE_UV:
      format(file, "0x%08xUV", (uint32_t) imm);
      break;
   case GEN_TYPE_VF:
      format(file, "0x%xVF", (uint32_t) imm);
      pad(file, 48);
      format(file, "/* [%-gF, %-gF, %-gF, %-gF]VF */",
             brw_vf_to_float((uint32_t) imm),
             brw_vf_to_float((uint32_t) imm >> 8),
             brw_vf_to_float((uint32_t) imm >> 16),
             brw_vf_to_float((uint32_t) imm >> 24));
      break;
   case GEN_TYPE_V:
      format(file, "0x%08xV", (uint32_t) imm);
      break;
   case GEN_TYPE_F:
      format(file, "0x%xF", (uint32_t) imm);
      pad(file, 48);
      format(file, " /* %-gF */", f32.f);
      break;
   case GEN_TYPE_DF:
      format(file, "0x%016"PRIx64"DF", imm);
      pad(file, 48);
      format(file, "/* %-gDF */", f64.df);
      break;
   case GEN_TYPE_HF:
      format(file, "0x%04xHF", (uint16_t) imm);
      pad(file, 48);
      format(file, "/* %-gHF */", _mesa_half_to_float((uint16_t) imm));
      break;
   case GEN_TYPE_UB:
   case GEN_TYPE_B:
   default:
      format(file, "*** invalid immediate type %d ", type);
   }

   return 0;
}

static int
src_sends_da(FILE *file, const gen_operand *op)
{
   int err = reg(file, op->file, op->nr);
   if (err == -1)
      return 0;
   if (op->subnr)
      format(file, ".1");
   return err;
}

static int
src_sends_ia(FILE *file, const gen_operand *op)
{
   string(file, "g[a0");
   if (op->subnr)
      format(file, ".1");
   if (op->addr_imm)
      format(file, " %d", op->addr_imm);
   string(file, "]");
   return 0;
}

static int
src_send_desc_ia(FILE *file, UNUSED const struct intel_device_info *devinfo,
                 unsigned subnr)
{
   string(file, "a0");
   if (subnr)
      format(file, ".%u", subnr / 4);
   format(file, "<0>UD");
   return 0;
}

static int
src0(FILE *file, const struct brw_isa_info *isa, const gen_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;
   const gen_opcode opcode = inst->opcode;

   if (is_split_send(devinfo, opcode)) {
      if (is_send_gather(isa, inst)) {
         string(file, "r[");
         reg(file, inst->src[0].file, inst->src[0].nr);
         format(file, ".%u]", inst->src[0].subnr);
         return 0;
      } else if (devinfo->ver >= 12) {
         return src_sends_da(file, &inst->src[0]);
      } else if (!inst->src[0].indirect) {
         return src_sends_da(file, &inst->src[0]);
      } else {
         return src_sends_ia(file, &inst->src[0]);
      }
   } else if (inst->src[0].file == GEN_IMM) {
      return imm(file, inst->src[0].type, inst->src[0].imm);
   } else if (!inst->align16) {
      if (!inst->src[0].indirect)
         return src_da1(file, opcode, &inst->src[0]);
      else
         return src_ia1(file, opcode, &inst->src[0]);
   } else {
      if (!inst->src[0].indirect)
         return src_da16(file, opcode, &inst->src[0]);
      else {
         string(file, "Indirect align16 address mode not supported");
         return 1;
      }
   }
}

static int
src1(FILE *file, const struct brw_isa_info *isa, const gen_inst *inst)
{
   const struct intel_device_info *devinfo = isa->devinfo;
   const gen_opcode opcode = inst->opcode;

   if (is_split_send(devinfo, opcode)) {
      return src_sends_da(file, &inst->src[1]);
   } else if (inst->src[1].file == GEN_IMM) {
      return imm(file, inst->src[1].type, inst->src[1].imm);
   } else if (!inst->align16) {
      if (!inst->src[1].indirect)
         return src_da1(file, opcode, &inst->src[1]);
      else
         return src_ia1(file, opcode, &inst->src[1]);
   } else {
      if (!inst->src[1].indirect)
         return src_da16(file, opcode, &inst->src[1]);
      else {
         string(file, "Indirect align16 address mode not supported");
         return 1;
      }
   }
}

static int
qtr_ctrl(FILE *file, const struct intel_device_info *devinfo,
             const gen_inst *inst)
{
   int qtr_ctl = inst->chan_offset / 8;
   int exec_size_value = inst->exec_size;
   const unsigned nib_ctl = devinfo->ver >= 20 ? 0 : (inst->chan_offset / 4) % 2;

   if (exec_size_value < 8 || nib_ctl) {
      format(file, " %dN", qtr_ctl * 2 + nib_ctl + 1);
   } else if (exec_size_value == 8) {
      format(file, " %dQ", qtr_ctl + 1);
   } else if (exec_size_value == 16) {
      string(file, qtr_ctl < 2 ? " 1H" : " 2H");
   }

   return 0;
}

static int
swsb(FILE *file, UNUSED const struct brw_isa_info *isa, const gen_inst *inst)
{
   if (inst->swsb.regdist) {
      const char *pipe =
         inst->swsb.pipe == GEN_PIPE_FLOAT  ? "F" :
         inst->swsb.pipe == GEN_PIPE_INT    ? "I" :
         inst->swsb.pipe == GEN_PIPE_LONG   ? "L" :
         inst->swsb.pipe == GEN_PIPE_ALL    ? "A" :
         inst->swsb.pipe == GEN_PIPE_MATH   ? "M" :
         inst->swsb.pipe == GEN_PIPE_SCALAR ? "S" : "";
      format(file, " %s@%d", pipe, inst->swsb.regdist);
   }

   if (inst->swsb.mode) {
      format(file, " $%d%s", inst->swsb.sbid,
             (inst->swsb.mode & GEN_SBID_SET ? "" :
              inst->swsb.mode & GEN_SBID_DST ? ".dst" : ".src"));
   }

   return 0;
}

#if MESA_DEBUG
static __attribute__((__unused__)) int
brw_disassemble_imm(const struct brw_isa_info *isa,
                    uint32_t dw3, uint32_t dw2, uint32_t dw1, uint32_t dw0)
{
   brw_eu_inst inst;
   inst.data[0] = (((uint64_t) dw1) << 32) | ((uint64_t) dw0);
   inst.data[1] = (((uint64_t) dw3) << 32) | ((uint64_t) dw2);
   return brw_disassemble_inst(stderr, isa, &inst, false, 0, NULL);
}
#endif

static void
write_label(FILE *file, const struct intel_device_info *devinfo,
            const struct brw_label *root_label,
            int offset, int jump)
{
   if (root_label != NULL) {
      int to_bytes_scale = sizeof(brw_eu_inst) / brw_jump_scale(devinfo);
      const struct brw_label *label =
         brw_find_label(root_label, offset + jump * to_bytes_scale);
      if (label != NULL) {
         format(file, " LABEL%d", label->number);
      }
   }
}

static void
lsc_disassemble_ex_desc(const struct intel_device_info *devinfo,
                        uint32_t imm_desc,
                        uint32_t imm_ex_desc,
                        FILE *file)
{
   const gen_lsc_desc desc = gen_lsc_desc_decode(devinfo, imm_desc);
   const gen_lsc_ex_desc ex_desc =
      gen_lsc_ex_desc_decode(devinfo, desc.addr_type, imm_ex_desc);

   switch (ex_desc.addr_type) {
   case LSC_ADDR_SURFTYPE_FLAT:
      format(file, " base_offset %u ",
             ex_desc.flat.base_offset);
      break;
   case LSC_ADDR_SURFTYPE_BSS:
   case LSC_ADDR_SURFTYPE_SS:
      format(file, " surface_state_index %u ",
             ex_desc.surface_state.surface_state_index);
      break;
   case LSC_ADDR_SURFTYPE_BTI:
      format(file, " BTI %u ",
             ex_desc.bti.index);
      format(file, " base_offset %u ",
             ex_desc.bti.base_offset);
      break;
   default:
      format(file, "unsupported address surface type %d", ex_desc.addr_type);
      break;
   }
}

static inline bool
brw_sfid_is_lsc(unsigned sfid)
{
   switch (sfid) {
   case BRW_SFID_UGM:
   case BRW_SFID_SLM:
   case BRW_SFID_TGM:
      return true;
   default:
      break;
   }

   return false;
}

static const char *
gen_type_letters(gen_reg_type type)
{
   return brw_reg_type_to_letters((enum brw_reg_type) type);
}

static bool
gen_inst_has_dst_local(const gen_inst *inst)
{
   switch (inst->opcode) {
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
   case GEN_OP_NOP:
   case GEN_OP_ILLEGAL:
   case GEN_OP_SYNC:
   case GEN_OP_SMOV:
      return false;
   default:
      return true;
   }
}

static int
brw_disassemble_inst_gen(FILE *file, const struct brw_isa_info *isa,
                         const gen_inst *inst, bool is_compacted,
                         int offset, const struct brw_label *root_label)
{
   const struct intel_device_info *devinfo = isa->devinfo;
   const gen_opcode opcode = inst->opcode;
   const unsigned nsrc = gen_inst_num_sources(devinfo, inst);
   const bool has_dst = gen_inst_has_dst_local(inst);
   int err = 0;
   int space = 0;

   if (inst->pred_control) {
      string(file, "(");
      err |= control(file, "predicate inverse", pred_inv, inst->pred_inv, NULL);
      format(file, "f%u.%u", inst->flag_nr, inst->flag_subnr);
      if (devinfo->ver >= 20) {
         err |= control(file, "predicate control", xe2_pred_ctrl,
                        inst->pred_control, NULL);
      } else if (!inst->align16) {
         err |= control(file, "predicate control align1", pred_ctrl_align1,
                        inst->pred_control, NULL);
      } else {
         err |= control(file, "predicate control align16", pred_ctrl_align16,
                        inst->pred_control, NULL);
      }
      string(file, ") ");
   }

   err |= print_opcode(file, isa, opcode);

   if (opcode == GEN_OP_BFN)
      format(file, "[0x%x]", inst->boolean_func_ctrl);

   if (!is_send(opcode))
      err |= control(file, "saturate", saturate, inst->saturate, NULL);

   err |= control(file, "debug control", debug_ctrl, inst->debug_control, NULL);

   if (opcode == GEN_OP_MATH) {
      string(file, " ");
      err |= control(file, "function", math_function, inst->math.func, NULL);
   } else if (opcode == GEN_OP_SYNC) {
      string(file, " ");
      err |= control(file, "function", sync_function, inst->sync.func, NULL);
   } else if (opcode == GEN_OP_DPAS) {
      format(file, ".%ux%u", inst->dpas.sdepth, inst->dpas.rcount);
   } else if (opcode == GEN_OP_BFN) {
      err |= control(file, "conditional modifier", conditional_modifier,
                     inst->cmod, NULL);
      if (inst->cmod != 0)
         format(file, ".f%u.%u", inst->flag_nr, inst->flag_subnr);
   } else if (!is_send(opcode) &&
              (devinfo->ver < 12 ||
               inst->src[0].file != GEN_IMM ||
               gen_type_size_bytes(inst->src[0].type) < 8)) {
      err |= control(file, "conditional modifier", conditional_modifier,
                     inst->cmod, NULL);
      if (inst->cmod && opcode != GEN_OP_SEL && opcode != GEN_OP_CSEL &&
          opcode != GEN_OP_IF && opcode != GEN_OP_WHILE) {
         format(file, ".f%u.%u", inst->flag_nr, inst->flag_subnr);
      }
   }

   if (opcode != GEN_OP_NOP)
      format(file, "(%u)", inst->exec_size);

   if (gen_has_uip(opcode)) {
      pad(file, 16);
      string(file, "JIP: ");
      write_label(file, devinfo, root_label, offset, inst->src[0].imm);

      pad(file, 38);
      string(file, "UIP: ");
      write_label(file, devinfo, root_label, offset, inst->src[1].imm);
   } else if (gen_has_jip(opcode)) {
      pad(file, 16);
      string(file, "JIP: ");
      write_label(file, devinfo, root_label, offset, inst->src[0].imm);
   } else if (opcode == GEN_OP_DPAS) {
      pad(file, 16);
      err |= dest_dpas_3src(file, inst);

      pad(file, 32);
      err |= src0_dpas_3src(file, isa->devinfo, inst);

      pad(file, 48);
      err |= src1_dpas_3src(file, isa->devinfo, inst);

      pad(file, 64);
      err |= src2_dpas_3src(file, isa->devinfo, inst);
   } else if (nsrc == 3) {
      pad(file, 16);
      err |= dest_3src(file, devinfo, inst);

      pad(file, 32);
      err |= src0_3src(file, devinfo, inst);

      pad(file, 48);
      err |= src1_3src(file, isa, inst);

      pad(file, 64);
      err |= src2_3src(file, isa, inst);
   } else if (has_dst || nsrc > 0) {
      int next_pad = 16;

      if (has_dst) {
         pad(file, next_pad);
         err |= dest(file, isa, inst);
         next_pad += 16;
      }

      if (nsrc > 0) {
         pad(file, next_pad);
         err |= src0(file, isa, inst);
         next_pad += 16;
      }

      if (nsrc > 1 && !is_send_gather(isa, inst)) {
         pad(file, next_pad);
         err |= src1(file, isa, inst);
      }
   }

   if (is_send(opcode)) {
      enum brw_sfid sfid = (enum brw_sfid) inst->send.sfid;
      bool has_imm_desc = !inst->send.desc_is_reg;
      bool has_imm_ex_desc = !inst->send.ex_desc_is_reg;
      uint32_t imm_desc = inst->send.desc_imm;
      uint32_t imm_ex_desc = has_imm_ex_desc ? inst->send.ex_desc_imm : 0;

      if (is_split_send(devinfo, opcode)) {
         pad(file, 64);
         if (inst->send.desc_is_reg) {
            err |= src_send_desc_ia(file, devinfo, 0);
         } else {
            fprintf(file, "0x%08"PRIx32, imm_desc);
         }

         pad(file, 80);
         if (inst->send.ex_desc_is_reg) {
            err |= src_send_desc_ia(file, devinfo, inst->send.ex_desc_subnr);
            if (devinfo->ver >= 20)
               imm_ex_desc = inst->send.ex_desc_imm_extra;
         } else {
            fprintf(file, "0x%08"PRIx32, imm_ex_desc);
         }
      } else {
         if (inst->send.desc_is_reg) {
            pad(file, 48);
            err |= src_send_desc_ia(file, devinfo, 0);
            pad(file, 64);
         } else {
            pad(file, 48);
         }

         fprintf(file, "0x%08"PRIx32, imm_desc);
      }

      newline(file);
      pad(file, 16);
      space = 0;

      err |= control(file, "SFID", brw_sfid, sfid, &space);
      string(file, " MsgDesc:");

      if (!has_imm_desc) {
         format(file, " indirect");
      } else {
         bool unsupported = false;
         switch (sfid) {
         case BRW_SFID_SAMPLER:
            if (devinfo->ver >= 20) {
               err |= control(file, "sampler message", xe2_sampler_msg_type,
                              brw_sampler_desc_msg_type(devinfo, imm_desc),
                              &space);
               err |= control(file, "sampler simd mode", xe2_sampler_simd_mode,
                              brw_sampler_desc_simd_mode(devinfo, imm_desc),
                              &space);
               if (brw_sampler_desc_return_format(devinfo, imm_desc))
                  string(file, " HP");
               format(file, " Surface = %u Sampler = %u",
                      brw_sampler_desc_binding_table_index(devinfo, imm_desc),
                      brw_sampler_desc_sampler(devinfo, imm_desc));
            } else {
               err |= control(file, "sampler message", gfx5_sampler_msg_type,
                              brw_sampler_desc_msg_type(devinfo, imm_desc),
                              &space);
               err |= control(file, "sampler simd mode",
                              gfx5_sampler_simd_mode,
                              brw_sampler_desc_simd_mode(devinfo, imm_desc),
                              &space);
               if (brw_sampler_desc_return_format(devinfo, imm_desc))
                  string(file, " HP");
               format(file, " Surface = %u Sampler = %u",
                      brw_sampler_desc_binding_table_index(devinfo, imm_desc),
                      brw_sampler_desc_sampler(devinfo, imm_desc));
            }
            break;

         case BRW_SFID_HDC2:
         case BRW_SFID_HDC_READ_ONLY:
            format(file, " (bti %u, msg_ctrl %u, msg_type %u)",
                   brw_dp_desc_binding_table_index(devinfo, imm_desc),
                   brw_dp_desc_msg_control(devinfo, imm_desc),
                   brw_dp_desc_msg_type(devinfo, imm_desc));
            break;

         case BRW_SFID_RENDER_CACHE: {
            unsigned msg_type = brw_fb_desc_msg_type(devinfo, imm_desc);

            err |= control(file, "DP rc message type",
                           dp_rc_msg_type(devinfo), msg_type, &space);

            if (msg_type == GFX6_DATAPORT_WRITE_MESSAGE_RENDER_TARGET_WRITE) {
               unsigned rt_message_type = GET_BITS(imm_desc, 11, 8);
               err |= control(file, "RT message type",
                              devinfo->ver >= 20 ? m_rt_write_subtype_xe2 : m_rt_write_subtype,
                              rt_message_type, &space);
               if (GET_BITS(imm_desc, 11, 11))
                  string(file, " Hi");
               if (brw_fb_write_desc_last_render_target(devinfo, imm_desc))
                  string(file, " LastRT");
               if (devinfo->ver >= 10 &&
                   brw_fb_write_desc_coarse_write(devinfo, imm_desc))
                  string(file, " CoarseWrite");
            } else {
               format(file, " MsgCtrl = 0x%u",
                      brw_fb_desc_msg_control(devinfo, imm_desc));
            }

            format(file, " Surface = %u",
                   brw_fb_desc_binding_table_index(devinfo, imm_desc));
            break;
         }

         case BRW_SFID_URB: {
            if (devinfo->ver >= 20) {
               const gen_lsc_desc desc_lsc = gen_lsc_desc_decode(devinfo, imm_desc);
               const enum lsc_opcode op = desc_lsc.op;

               format(file, " (");
               err |= control(file, "operation", lsc_operation, op, &space);
               format(file, ",");
               err |= control(file, "addr_size", lsc_addr_size,
                              desc_lsc.addr_size, &space);
               format(file, ",");
               err |= control(file, "data_size", lsc_data_size,
                              desc_lsc.data_size, &space);
               format(file, ",");
               if (lsc_opcode_has_cmask(op)) {
                  err |= control(file, "component_mask", lsc_cmask_str,
                                 desc_lsc.cmask, &space);
               } else {
                  err |= control(file, "vector_size", lsc_vect_size_str,
                                 desc_lsc.vect_size, &space);
                  if (desc_lsc.transpose)
                     format(file, ", transpose");
               }

               switch (op) {
               case LSC_OP_LOAD_CMASK:
               case LSC_OP_LOAD:
               case LSC_OP_LOAD_CMASK_MSRT:
                  format(file, ",");
                  err |= control(file, "cache_load",
                                 devinfo->ver >= 20 ? xe2_lsc_cache_load : lsc_cache_load,
                                 desc_lsc.cache_ctrl, &space);
                  break;
               default:
                  format(file, ",");
                  err |= control(file, "cache_store",
                                 devinfo->ver >= 20 ? xe2_lsc_cache_store : lsc_cache_store,
                                 desc_lsc.cache_ctrl, &space);
                  break;
               }

               format(file, " dst_len = %u,", brw_message_desc_rlen(devinfo, imm_desc) / reg_unit(devinfo));
               format(file, " src0_len = %u,", brw_message_desc_mlen(devinfo, imm_desc) / reg_unit(devinfo));
               if (!is_send_gather(isa, inst))
                  format(file, " src1_len = %u",
                         brw_message_ex_desc_ex_mlen(devinfo, imm_ex_desc) / reg_unit(devinfo));
               err |= control(file, "address_type", lsc_addr_surface_type,
                              desc_lsc.addr_type, &space);
               format(file, " )");
            } else {
               unsigned urb_opcode = brw_urb_desc_msg_type(devinfo, imm_desc);

               format(file, " offset %u", GET_BITS(imm_desc, 14, 4));
               space = 1;

               err |= control(file, "urb opcode", gfx7_urb_opcode,
                              urb_opcode, &space);

               if (GET_BITS(imm_desc, 17, 17))
                  string(file, " per-slot");

               if (urb_opcode == GFX8_URB_OPCODE_SIMD8_WRITE ||
                   urb_opcode == GFX8_URB_OPCODE_SIMD8_READ) {
                  if (GET_BITS(imm_desc, 15, 15))
                     string(file, " masked");
               } else if (urb_opcode != GFX125_URB_OPCODE_FENCE) {
                  err |= control(file, "urb swizzle", urb_swizzle,
                                 GET_BITS(imm_desc, 15, 15), &space);
               }
            }
            break;
         }

         case BRW_SFID_THREAD_SPAWNER:
            break;

         case BRW_SFID_MESSAGE_GATEWAY:
            format(file, " (%s)",
                   gfx7_gateway_subfuncid[GET_BITS(imm_desc, 2, 0)]);
            break;

         case BRW_SFID_SLM:
         case BRW_SFID_TGM:
         case BRW_SFID_UGM: {
            const gen_lsc_desc desc_lsc = gen_lsc_desc_decode(devinfo, imm_desc);
            const enum lsc_opcode op = desc_lsc.op;

            format(file, " (");
            err |= control(file, "operation", lsc_operation, op, &space);
            format(file, ",");
            err |= control(file, "addr_size", lsc_addr_size,
                           desc_lsc.addr_size, &space);

            if (op == LSC_OP_FENCE) {
               format(file, ",");
               err |= control(file, "scope", lsc_fence_scope,
                              desc_lsc.fence.scope, &space);
               format(file, ",");
               err |= control(file, "flush_type", lsc_flush_type,
                              desc_lsc.fence.flush_type, &space);
               format(file, ",");
               err |= control(file, "backup_mode_fence_routing",
                              lsc_backup_fence_routing,
                              desc_lsc.fence.route_to_lsc, &space);
            } else {
               format(file, ",");
               err |= control(file, "data_size", lsc_data_size,
                              desc_lsc.data_size, &space);
               format(file, ",");
               if (lsc_opcode_has_cmask(op)) {
                  err |= control(file, "component_mask", lsc_cmask_str,
                                 desc_lsc.cmask, &space);
               } else {
                  err |= control(file, "vector_size", lsc_vect_size_str,
                                 desc_lsc.vect_size, &space);
                  if (desc_lsc.transpose)
                     format(file, ", transpose");
               }

               switch (op) {
               case LSC_OP_LOAD_CMASK:
               case LSC_OP_LOAD:
                  format(file, ",");
                  err |= control(file, "cache_load",
                                 devinfo->ver >= 20 ? xe2_lsc_cache_load : lsc_cache_load,
                                 desc_lsc.cache_ctrl, &space);
                  break;
               default:
                  format(file, ",");
                  err |= control(file, "cache_store",
                                 devinfo->ver >= 20 ? xe2_lsc_cache_store : lsc_cache_store,
                                 desc_lsc.cache_ctrl, &space);
                  break;
               }
            }

            format(file, " dst_len = %u,", brw_message_desc_rlen(devinfo, imm_desc) / reg_unit(devinfo));
            format(file, " src0_len = %u,", brw_message_desc_mlen(devinfo, imm_desc) / reg_unit(devinfo));

            if (!inst->send.ex_desc_is_reg && !is_send_gather(isa, inst))
               format(file, " src1_len = %u",
                      brw_message_ex_desc_ex_mlen(devinfo, imm_ex_desc) / reg_unit(devinfo));

            err |= control(file, "address_type", lsc_addr_surface_type,
                           desc_lsc.addr_type, &space);
            format(file, " )");
            break;
         }

         case BRW_SFID_HDC0:
            format(file, " (");
            space = 0;
            err |= control(file, "DP DC0 message type", dp_dc0_msg_type_gfx7,
                           brw_dp_desc_msg_type(devinfo, imm_desc), &space);
            format(file, ", bti %u, ", brw_dp_desc_binding_table_index(devinfo, imm_desc));
            switch (brw_dp_desc_msg_type(devinfo, imm_desc)) {
            case GFX7_DATAPORT_DC_UNTYPED_ATOMIC_OP:
               control(file, "atomic op", aop,
                       brw_dp_desc_msg_control(devinfo, imm_desc) & 0xf,
                       &space);
               break;
            case GFX7_DATAPORT_DC_OWORD_BLOCK_READ:
            case GFX7_DATAPORT_DC_OWORD_BLOCK_WRITE: {
               unsigned msg_ctrl = brw_dp_desc_msg_control(devinfo, imm_desc);
               assert(dp_oword_block_rw[msg_ctrl & 7]);
               format(file, "owords = %s, aligned = %d",
                      dp_oword_block_rw[msg_ctrl & 7], (msg_ctrl >> 3) & 3);
               break;
            }
            default:
               format(file, "%u", brw_dp_desc_msg_control(devinfo, imm_desc));
            }
            format(file, ")");
            break;

         case BRW_SFID_HDC1: {
            format(file, " (");
            space = 0;

            unsigned msg_ctrl = brw_dp_desc_msg_control(devinfo, imm_desc);
            unsigned msg_type = brw_dp_desc_msg_type(devinfo, imm_desc);

            err |= control(file, "DP DC1 message type", dp_dc1_msg_type_hsw,
                           msg_type, &space);
            format(file, ", Surface = %u, ",
                   brw_dp_desc_binding_table_index(devinfo, imm_desc));

            switch (msg_type) {
            case HSW_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_OP:
            case HSW_DATAPORT_DC_PORT1_TYPED_ATOMIC_OP:
            case HSW_DATAPORT_DC_PORT1_ATOMIC_COUNTER_OP:
               format(file, "SIMD%d,", (msg_ctrl & (1 << 4)) ? 8 : 16);
               FALLTHROUGH;
            case HSW_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_OP_SIMD4X2:
            case HSW_DATAPORT_DC_PORT1_TYPED_ATOMIC_OP_SIMD4X2:
            case HSW_DATAPORT_DC_PORT1_ATOMIC_COUNTER_OP_SIMD4X2:
            case GFX8_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_OP:
            case GFX12_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_HALF_INT_OP:
               control(file, "atomic op", aop, msg_ctrl & 0xf, &space);
               break;
            case HSW_DATAPORT_DC_PORT1_UNTYPED_SURFACE_READ:
            case HSW_DATAPORT_DC_PORT1_UNTYPED_SURFACE_WRITE:
            case HSW_DATAPORT_DC_PORT1_TYPED_SURFACE_READ:
            case HSW_DATAPORT_DC_PORT1_TYPED_SURFACE_WRITE:
            case GFX8_DATAPORT_DC_PORT1_A64_UNTYPED_SURFACE_WRITE:
            case GFX8_DATAPORT_DC_PORT1_A64_UNTYPED_SURFACE_READ: {
               static const char *simd_modes[] = { "4x2", "16", "8" };
               format(file, "SIMD%s, Mask = 0x%x", simd_modes[msg_ctrl >> 4], msg_ctrl & 0xf);
               break;
            }
            case GFX9_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_FLOAT_OP:
            case GFX9_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_FLOAT_OP:
            case GFX12_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_HALF_FLOAT_OP:
               format(file, "SIMD%d,", (msg_ctrl & (1 << 4)) ? 8 : 16);
               control(file, "atomic float op", aop_float, msg_ctrl & 0xf, &space);
               break;
            case GFX9_DATAPORT_DC_PORT1_A64_OWORD_BLOCK_WRITE:
            case GFX9_DATAPORT_DC_PORT1_A64_OWORD_BLOCK_READ:
               assert(dp_oword_block_rw[msg_ctrl & 7]);
               format(file, "owords = %s, aligned = %d",
                      dp_oword_block_rw[msg_ctrl & 7], (msg_ctrl >> 3) & 3);
               break;
            default:
               format(file, "0x%x", msg_ctrl);
            }
            format(file, ")");
            break;
         }

         case BRW_SFID_PIXEL_INTERPOLATOR:
            format(file, " (%s, %s, 0x%02x)",
                   GET_BITS(imm_desc, 14, 14) ? "linear" : "persp",
                   pixel_interpolator_msg_types[GET_BITS(imm_desc, 13, 12)],
                   GET_BITS(imm_desc, 7, 0));
            break;

         case BRW_SFID_RAY_TRACE_ACCELERATOR:
            if (devinfo->has_ray_tracing)
               format(file, " SIMD%d,", brw_rt_trace_ray_desc_exec_size(devinfo, imm_desc));
            else
               unsupported = true;
            break;

         default:
            unsupported = true;
            break;
         }

         if (unsupported)
            format(file, "unsupported shared function ID %d", sfid);

         if (space)
            string(file, " ");
      }

      if (devinfo->verx10 >= 125 && inst->send.ex_desc_is_reg && inst->send.ex_bso) {
         format(file, " src1_len = %u", inst->send.src1_len);
         format(file, " ex_bso");
      }

      if (brw_sfid_is_lsc(sfid) || (sfid == BRW_SFID_URB && devinfo->ver >= 20)) {
         lsc_disassemble_ex_desc(devinfo, imm_desc, imm_ex_desc, file);
      } else {
         if (has_imm_desc)
            format(file, " mlen %u", brw_message_desc_mlen(devinfo, imm_desc) / reg_unit(devinfo));
         if (has_imm_ex_desc)
            format(file, " ex_mlen %u", brw_message_ex_desc_ex_mlen(devinfo, imm_ex_desc) / reg_unit(devinfo));
         if (has_imm_desc)
            format(file, " rlen %u", brw_message_desc_rlen(devinfo, imm_desc) / reg_unit(devinfo));
      }
   }

   pad(file, 64);
   if (opcode != GEN_OP_NOP) {
      string(file, "{");
      space = 1;
      err |= control(file, "access mode", access_mode, inst->align16, &space);
      err |= control(file, "write enable control", wectrl, inst->no_mask, &space);

      if (devinfo->ver < 12) {
         err |= control(file, "dependency control", dep_ctrl,
                        ((inst->no_dd_check << 1) | inst->no_dd_clear), &space);
      }

      err |= qtr_ctrl(file, devinfo, inst);

      if (devinfo->ver >= 12)
         err |= swsb(file, isa, inst);

      err |= control(file, "compaction", cmpt_ctrl, is_compacted, &space);
      err |= control(file, "thread control", thread_ctrl,
                     devinfo->ver >= 12 ? inst->atomic_control : inst->thread_control,
                     &space);

      if (gen_has_branch_ctrl(opcode)) {
         err |= control(file, "branch ctrl", branch_ctrl,
                        inst->branch_control, &space);
      } else if (devinfo->ver < 20) {
         err |= control(file, "acc write control", accwr,
                        inst->acc_wr_control, &space);
      }

      if (devinfo->ver == 12 && is_send(opcode)) {
         err |= control(file, "fusion ctrl", fusion_ctrl,
                        inst->fusion_control, &space);
      }

      if (is_send(opcode))
         err |= control(file, "end of thread", end_of_thread,
                        inst->send.eot, &space);
      if (space)
         string(file, " ");
      string(file, "}");
   }

   string(file, ";");
   newline(file);
   return err;
}

int
brw_disassemble_inst(FILE *file, const struct brw_isa_info *isa,
                     const brw_eu_inst *inst, bool is_compacted,
                     int offset, const struct brw_label *root_label)
{
   void *mem_ctx = ralloc_context(NULL);
   brw_eu_inst uncompacted;
   const void *raw = inst;
   int raw_size = sizeof(*inst);

   if (is_compacted && ((*(const uint32_t *)inst & BITFIELD_BIT(29)) != 0)) {
      raw_size = sizeof(brw_eu_compact_inst);
      brw_uncompact_instruction(isa, &uncompacted, (brw_eu_compact_inst *) inst);
      inst = &uncompacted;
   }

   gen_decode_params params = {
      .devinfo = isa->devinfo,
      .raw_bytes = raw,
      .raw_bytes_size = raw_size,
      .mem_ctx = mem_ctx,
   };

   const bool ok = gen_decode(&params);
   if (!ok || params.num_insts != 1) {
      ralloc_free(mem_ctx);
      return 1;
   }

   int err = brw_disassemble_inst_gen(file, isa, params.insts[0], is_compacted,
                                      offset, root_label);
   ralloc_free(mem_ctx);
   return err;
}

int
brw_disassemble_find_end(const struct brw_isa_info *isa,
                         const void *assembly, int start)
{
   return start + gen_find_shader_size(isa->devinfo, assembly, start, 0);
}

void
brw_disassemble_with_errors(const struct brw_isa_info *isa,
                            const void *assembly, int start,
                            int64_t *lineno_offset, FILE *out)
{
   int end = brw_disassemble_find_end(isa, assembly, start);

   /* Make a dummy disasm structure that brw_validate_instructions
    * can work from.
    */
   struct disasm_info *disasm_info = disasm_initialize(isa, NULL);
   disasm_new_inst_group(disasm_info, start);
   disasm_new_inst_group(disasm_info, end);

   brw_validate_instructions(isa, assembly, start, end, disasm_info);

   void *mem_ctx = ralloc_context(NULL);
   const struct brw_label *root_label =
      brw_label_assembly(isa, assembly, start, end, mem_ctx);

   brw_foreach_list_typed(struct inst_group, group, link,
                      &disasm_info->group_list) {
      struct brw_exec_node *next_node = brw_exec_node_get_next(&group->link);
      if (brw_exec_node_is_tail_sentinel(next_node))
         break;

      struct inst_group *next =
         brw_exec_node_data(struct inst_group, next_node, link);

      int start_offset = group->offset;
      int end_offset = next->offset;

      brw_disassemble(isa, assembly, start_offset, end_offset,
                      root_label, lineno_offset, out);

      if (group->error) {
         fputs(group->error, out);
      }
   }

   ralloc_free(mem_ctx);
   ralloc_free(disasm_info);
}

void
brw_disassemble_with_lineno(const struct brw_isa_info *isa, uint32_t stage,
                            int dispatch_width, uint32_t src_hash,
                            const void *assembly, int start,
                            int64_t lineno_offset, FILE *out)
{
   fprintf(out, "\nDumping shader asm for %s", _mesa_shader_stage_to_abbrev(stage));
   if (dispatch_width > 0)
      fprintf(out, " SIMD%i", dispatch_width);
   fprintf(out, " (src_hash 0x%x):\n\n", src_hash);
   brw_disassemble_with_errors(isa, assembly, start, &lineno_offset, out);
}
