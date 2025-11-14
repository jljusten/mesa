/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <inttypes.h>
#include <memory>
#include <vector>

#include "util/half_float.h"
#include "util/ralloc.h"

#include "gen_private.h"

static const char *const chan_mask_str[16] = {
   "",   "x",  "y",  "xy",  "z",  "xz",  "yz",  "xyz",
   "w",  "xw", "yw", "xyw", "zw", "xzw", "yzw", "xyzw",
};

static const char *const chan_sel[4] = {
   "x",
   "y",
   "z",
   "w",
};

static const char *const pred_ctrl_align16[16] = {
   "",      /* none */
   "",      /* normal */
   ".x",
   ".y",
   ".z",
   ".w",
   ".any4h",
   ".all4h",
   "",
   "",
   "",
   "",
   "",
   "",
   "",
   "",
};

static const char *const pred_ctrl_align1[16] = {
   "",        /* none */
   "",        /* normal */
   ".anyv",
   ".allv",
   ".any2h",
   ".all2h",
   ".any4h",
   ".all4h",
   ".any8h",
   ".all8h",
   ".any16h",
   ".all16h",
   ".any32h",
   ".all32h",
   "",
   "",
};

static const char *const xe2_pred_ctrl[4] = {
   "",
   "",
   ".any",
   ".all",
};

static const char *
pred_ctrl_suffix(const intel_device_info *devinfo, bool align16,
                 unsigned pred_control)
{
   if (devinfo->ver >= 20)
      return xe2_pred_ctrl[pred_control];
   return align16 ? pred_ctrl_align16[pred_control]
                  : pred_ctrl_align1[pred_control];
}

static int
emit_swsb(const intel_device_info *devinfo, FILE *fp, gen_swsb swsb,
          const char *sep)
{
   int n = 0;

   if (swsb.regdist) {
      const char *pipe = "";
      if (devinfo && devinfo->verx10 >= 125) {
         switch (swsb.pipe) {
         case GEN_PIPE_FLOAT:  pipe = "F"; break;
         case GEN_PIPE_INT:    pipe = "I"; break;
         case GEN_PIPE_LONG:   pipe = "L"; break;
         case GEN_PIPE_ALL:    pipe = "A"; break;
         case GEN_PIPE_MATH:   pipe = "M"; break;
         case GEN_PIPE_SCALAR: pipe = "S"; break;
         case GEN_PIPE_NONE:   break;
         }
      }
      n += fprintf(fp, "%s@%u", pipe, swsb.regdist);
   }

   if (swsb.mode) {
      const char *suffix = (swsb.mode & GEN_SBID_SET) ? "" :
                           (swsb.mode & GEN_SBID_DST) ? ".dst" : ".src";
      n += fprintf(fp, "%s$%u%s", n > 0 ? sep : "", swsb.sbid, suffix);
   }

   return n;
}

static const char *
type_name(enum gen_reg_type type)
{
   switch (type) {
   case GEN_TYPE_UB: return "ub";
   case GEN_TYPE_B:  return "b";
   case GEN_TYPE_UW: return "uw";
   case GEN_TYPE_W:  return "w";
   case GEN_TYPE_UD: return "ud";
   case GEN_TYPE_D:  return "d";
   case GEN_TYPE_UQ: return "uq";
   case GEN_TYPE_Q:  return "q";
   case GEN_TYPE_HF: return "hf";
   case GEN_TYPE_BF: return "bf";
   case GEN_TYPE_F:  return "f";
   case GEN_TYPE_DF: return "df";
   case GEN_TYPE_V:  return "v";
   case GEN_TYPE_VF: return "vf";
   case GEN_TYPE_UV: return "uv";
   default:          return "UNKNOWN";
   }
}

static const char *
cond_modifier_name(enum gen_condition cmod)
{
   switch (cmod) {
   case GEN_CONDITION_NONE: return "";
   case GEN_CONDITION_EQ:   return "eq";
   case GEN_CONDITION_NE:   return "ne";
   case GEN_CONDITION_GT:   return "gt";
   case GEN_CONDITION_GE:   return "ge";
   case GEN_CONDITION_LT:   return "lt";
   case GEN_CONDITION_LE:   return "le";
   case GEN_CONDITION_OV:   return "ov";
   case GEN_CONDITION_UN:   return "un";
   default:                 return "UNKNOWN";
   }
}

static const char *
math_function_name(unsigned func)
{
   switch (func) {
   case GEN_MATH_INV:               return "inv";
   case GEN_MATH_LOG:               return "log";
   case GEN_MATH_EXP:               return "exp";
   case GEN_MATH_SQRT:              return "sqt";
   case GEN_MATH_RSQ:               return "rsq";
   case GEN_MATH_SIN:               return "sin";
   case GEN_MATH_COS:               return "cos";
   case GEN_MATH_FDIV:              return "fdiv";
   case GEN_MATH_POW:               return "pow";
   case GEN_MATH_INT_DIV_BOTH:      return "intdiv_qr";
   case GEN_MATH_INT_DIV_QUOTIENT:  return "intdiv_q";
   case GEN_MATH_INT_DIV_REMAINDER: return "intdiv_r";
   case GEN_MATH_INVM:              return "invm";
   case GEN_MATH_RSQRTM:            return "rsqrtm";
   default:                         return "UNKNOWN";
   }
}

static const char *
sync_function_name(gen_sync_func func)
{
   switch (func) {
   case GEN_SYNC_NOP:   return "nop";
   case GEN_SYNC_ALLRD: return "allrd";
   case GEN_SYNC_ALLWR: return "allwr";
   case GEN_SYNC_FENCE: return "fence";
   case GEN_SYNC_BAR:   return "bar";
   case GEN_SYNC_HOST:  return "host";
   default:             return "UNKNOWN";
   }
}

static const char *
gen_sfid_to_string(const intel_device_info *devinfo, gen_sfid sfid)
{
   switch (sfid) {
   case GEN_SFID_NULL:                  return "null";
   case GEN_SFID_SAMPLER:               return "smpl";
   case GEN_SFID_MESSAGE_GATEWAY:       return "gtwy";
   case GEN_SFID_HDC2:                  return "hdc2";
   case GEN_SFID_RENDER_CACHE:          return "render";
   case GEN_SFID_URB:                   return "urb";
   case GEN_SFID_THREAD_SPAWNER:        return devinfo->verx10 <= 120 ? "ts" : "btd";
   case GEN_SFID_RAY_TRACE_ACCELERATOR: return "rtaccel";
   case GEN_SFID_HDC_READ_ONLY:         return "hdc_ro";
   case GEN_SFID_HDC0:                  return "hdc0";
   case GEN_SFID_PIXEL_INTERPOLATOR:    return "pi";
   case GEN_SFID_HDC1:                  return "hdc1";
   case GEN_SFID_SLM:                   return "slm";
   case GEN_SFID_TGM:                   return "tgm";
   case GEN_SFID_UGM:                   return "ugm";
   default:                             return "UNKNOWN";
   }
}

static int
branch_target_index(int idx, int32_t rel_bytes)
{
   if (rel_bytes % 16 != 0)
      return INT32_MIN;

   return idx + rel_bytes / 16;
}

struct gen_label {
   int target;
   unsigned label;
};

static bool
lsc_opcode_uses_load_cache(enum lsc_opcode opcode)
{
   return opcode == LSC_OP_LOAD ||
          opcode == LSC_OP_LOAD_CMASK ||
          opcode == LSC_OP_LOAD_CMASK_MSRT;
}

static const char *
lsc_opcode_name(enum lsc_opcode opcode)
{
   switch (opcode) {
   case LSC_OP_LOAD:             return "load";
   case LSC_OP_LOAD_CMASK:       return "load_cmask";
   case LSC_OP_STORE:            return "store";
   case LSC_OP_STORE_CMASK:      return "store_cmask";
   case LSC_OP_ATOMIC_INC:       return "atomic_inc";
   case LSC_OP_ATOMIC_DEC:       return "atomic_dec";
   case LSC_OP_ATOMIC_LOAD:      return "atomic_load";
   case LSC_OP_ATOMIC_STORE:     return "atomic_store";
   case LSC_OP_ATOMIC_ADD:       return "atomic_add";
   case LSC_OP_ATOMIC_SUB:       return "atomic_sub";
   case LSC_OP_ATOMIC_MIN:       return "atomic_min";
   case LSC_OP_ATOMIC_MAX:       return "atomic_max";
   case LSC_OP_ATOMIC_UMIN:      return "atomic_umin";
   case LSC_OP_ATOMIC_UMAX:      return "atomic_umax";
   case LSC_OP_ATOMIC_CMPXCHG:   return "atomic_cmpxchg";
   case LSC_OP_ATOMIC_FADD:      return "atomic_fadd";
   case LSC_OP_ATOMIC_FSUB:      return "atomic_fsub";
   case LSC_OP_ATOMIC_FMIN:      return "atomic_fmin";
   case LSC_OP_ATOMIC_FMAX:      return "atomic_fmax";
   case LSC_OP_ATOMIC_FCMPXCHG:  return "atomic_fcmpxchg";
   case LSC_OP_ATOMIC_AND:       return "atomic_and";
   case LSC_OP_ATOMIC_OR:        return "atomic_or";
   case LSC_OP_ATOMIC_XOR:       return "atomic_xor";
   case LSC_OP_FENCE:            return "fence";
   case LSC_OP_LOAD_CMASK_MSRT:  return "load_cmask_msrt";
   case LSC_OP_STORE_CMASK_MSRT: return "store_cmask_msrt";
   default:                      return "UNKNOWN";
   }
}

static const char *
lsc_addr_size_name(enum lsc_addr_size addr_size)
{
   switch (addr_size) {
   case LSC_ADDR_SIZE_A16: return "a16";
   case LSC_ADDR_SIZE_A32: return "a32";
   case LSC_ADDR_SIZE_A64: return "a64";
   default:                return NULL;
   }
}

static unsigned
lsc_vector_length_local(enum lsc_vect_size vect_size)
{
   switch (vect_size) {
   case LSC_VECT_SIZE_V1:  return 1;
   case LSC_VECT_SIZE_V2:  return 2;
   case LSC_VECT_SIZE_V3:  return 3;
   case LSC_VECT_SIZE_V4:  return 4;
   case LSC_VECT_SIZE_V8:  return 8;
   case LSC_VECT_SIZE_V16: return 16;
   case LSC_VECT_SIZE_V32: return 32;
   case LSC_VECT_SIZE_V64: return 64;
   default:                return 0;
   }
}

static const char *
lsc_data_size_name(enum lsc_data_size data_size)
{
   switch (data_size) {
   case LSC_DATA_SIZE_D8:      return "d8";
   case LSC_DATA_SIZE_D16:     return "d16";
   case LSC_DATA_SIZE_D32:     return "d32";
   case LSC_DATA_SIZE_D64:     return "d64";
   case LSC_DATA_SIZE_D8U32:   return "d8u32";
   case LSC_DATA_SIZE_D16U32:  return "d16u32";
   case LSC_DATA_SIZE_D16BF32: return "d16bf32";
   default:                    return NULL;
   }
}

static const char *
lsc_cmask_name(enum lsc_cmask cmask)
{
   if (cmask < LSC_CMASK_X || cmask > LSC_CMASK_XYZW)
      return NULL;
   return chan_mask_str[cmask];
}

static const char *
lsc_fence_scope_name(enum lsc_fence_scope scope)
{
   switch (scope) {
   case LSC_FENCE_THREADGROUP:    return "threadgroup";
   case LSC_FENCE_LOCAL:          return "local";
   case LSC_FENCE_TILE:           return "tile";
   case LSC_FENCE_GPU:            return "gpu";
   case LSC_FENCE_ALL_GPU:        return "all_gpu";
   case LSC_FENCE_SYSTEM_RELEASE: return "system_release";
   case LSC_FENCE_SYSTEM_ACQUIRE: return "system_acquire";
   default:                       return NULL;
   }
}

static const char *
lsc_flush_type_name(enum lsc_flush_type flush_type)
{
   switch (flush_type) {
   case LSC_FLUSH_TYPE_NONE:       return "none";
   case LSC_FLUSH_TYPE_EVICT:      return "evict";
   case LSC_FLUSH_TYPE_INVALIDATE: return "invalidate";
   case LSC_FLUSH_TYPE_DISCARD:    return "discard";
   case LSC_FLUSH_TYPE_CLEAN:      return "clean";
   case LSC_FLUSH_TYPE_L3ONLY:     return "l3only";
   case LSC_FLUSH_TYPE_NONE_6:     return "none_6";
   default:                        return NULL;
   }
}

static const char *
xe2_lsc_cache_load_name(enum xe2_lsc_cache_load v)
{
   switch (v) {
   case XE2_LSC_CACHE_LOAD_L1UC_L3UC:   return "uc.uc";
   case XE2_LSC_CACHE_LOAD_L1UC_L3C:    return "uc.ca";
   case XE2_LSC_CACHE_LOAD_L1UC_L3CC:   return "uc.cc";
   case XE2_LSC_CACHE_LOAD_L1C_L3UC:    return "ca.uc";
   case XE2_LSC_CACHE_LOAD_L1C_L3C:     return "ca.ca";
   case XE2_LSC_CACHE_LOAD_L1C_L3CC:    return "ca.cc";
   case XE2_LSC_CACHE_LOAD_L1S_L3UC:    return "st.uc";
   case XE2_LSC_CACHE_LOAD_L1S_L3C:     return "st.ca";
   case XE2_LSC_CACHE_LOAD_L1IAR_L3IAR: return "ri.ri";
   default:                             return NULL;
   }
}

static const char *
lsc_cache_load_name(enum lsc_cache_load v)
{
   switch (v) {
   case LSC_CACHE_LOAD_L1UC_L3UC: return "uc.uc";
   case LSC_CACHE_LOAD_L1UC_L3C:  return "uc.ca";
   case LSC_CACHE_LOAD_L1C_L3UC:  return "ca.uc";
   case LSC_CACHE_LOAD_L1C_L3C:   return "ca.ca";
   case LSC_CACHE_LOAD_L1S_L3UC:  return "st.uc";
   case LSC_CACHE_LOAD_L1S_L3C:   return "st.ca";
   case LSC_CACHE_LOAD_L1IAR_L3C: return "ri.ca";
   default:                       return NULL;
   }
}

static const char *
xe2_lsc_cache_store_name(enum xe2_lsc_cache_store v)
{
   switch (v) {
   case XE2_LSC_CACHE_STORE_L1UC_L3UC: return "uc.uc";
   case XE2_LSC_CACHE_STORE_L1UC_L3WB: return "uc.wb";
   case XE2_LSC_CACHE_STORE_L1WT_L3UC: return "wt.uc";
   case XE2_LSC_CACHE_STORE_L1WT_L3WB: return "wt.wb";
   case XE2_LSC_CACHE_STORE_L1S_L3UC:  return "st.uc";
   case XE2_LSC_CACHE_STORE_L1S_L3WB:  return "st.wb";
   case XE2_LSC_CACHE_STORE_L1WB_L3WB: return "wb.wb";
   default:                            return NULL;
   }
}

static const char *
lsc_cache_store_name(enum lsc_cache_store v)
{
   switch (v) {
   case LSC_CACHE_STORE_L1UC_L3UC: return "uc.uc";
   case LSC_CACHE_STORE_L1UC_L3WB: return "uc.wb";
   case LSC_CACHE_STORE_L1WT_L3UC: return "wt.uc";
   case LSC_CACHE_STORE_L1WT_L3WB: return "wt.wb";
   case LSC_CACHE_STORE_L1S_L3UC:  return "st.uc";
   case LSC_CACHE_STORE_L1S_L3WB:  return "st.wb";
   case LSC_CACHE_STORE_L1WB_L3WB: return "wb.wb";
   default:                        return NULL;
   }
}

static const char *
lsc_cache_ctrl_name(const intel_device_info *devinfo,
                    enum lsc_opcode op, unsigned cache_ctrl)
{
   if (cache_ctrl == 0)
      return "";

   const bool xe2  = devinfo->ver >= 20;
   const bool load = lsc_opcode_uses_load_cache(op);

   if (load) {
      return xe2 ? xe2_lsc_cache_load_name ((enum xe2_lsc_cache_load )cache_ctrl)
                 : lsc_cache_load_name     ((enum lsc_cache_load     )cache_ctrl);
   } else {
      return xe2 ? xe2_lsc_cache_store_name((enum xe2_lsc_cache_store)cache_ctrl)
                 : lsc_cache_store_name    ((enum lsc_cache_store    )cache_ctrl);
   }
}

static const char *
sampler_msg_type_to_string(const intel_device_info *devinfo,
                           unsigned msg_type)
{
   switch (msg_type) {
   case GEN_SAMPLER_MESSAGE_SAMPLE:                     return "sample";
   case GEN_SAMPLER_MESSAGE_SAMPLE_BIAS:                return "sample_b";
   case GEN_SAMPLER_MESSAGE_SAMPLE_LOD:                 return "sample_l";
   case GEN_SAMPLER_MESSAGE_SAMPLE_COMPARE:             return "sample_c";
   case GEN_SAMPLER_MESSAGE_SAMPLE_DERIVS:              return "sample_d";
   case GEN_SAMPLER_MESSAGE_SAMPLE_BIAS_COMPARE:        return "sample_b_c";
   case GEN_SAMPLER_MESSAGE_SAMPLE_LOD_COMPARE:         return "sample_l_c";
   case GEN_SAMPLER_MESSAGE_SAMPLE_LD:                  return "ld";
   case GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4:             return "gather4";
   case GEN_SAMPLER_MESSAGE_LOD:                        return "lod";
   case GEN_SAMPLER_MESSAGE_SAMPLE_RESINFO:             return "resinfo";
   case GEN_SAMPLER_MESSAGE_SAMPLE_SAMPLEINFO:          return "sampleinfo";
   case GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4_C:           return "gather4_c";
   case GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO:          return "gather4_po";
   case GEN_SAMPLER_MESSAGE_SAMPLE_DERIV_COMPARE:       return "sample_d_c";
   case GEN_SAMPLER_MESSAGE_SAMPLE_LZ:                  return "sample_lz";
   case GEN_SAMPLER_MESSAGE_SAMPLE_C_LZ:                return "sample_c_lz";
   case GEN_SAMPLER_MESSAGE_SAMPLE_LD_LZ:               return "ld_lz";
   case GEN_SAMPLER_MESSAGE_SAMPLE_LD2DMS_W:            return "ld2dms_w";
   case GEN_SAMPLER_MESSAGE_SAMPLE_LD_MCS:              return "ld_mcs";
   case GEN_SAMPLER_MESSAGE_SAMPLE_LD2DMS:              return "ld2dms";
   case GEN_SAMPLER_MESSAGE_SAMPLE_LD2DSS:              return "ld2dss";

   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L:       return "gather4_l";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_B:       return "gather4_b";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I:       return "gather4_i";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_COMPARE_MLOD:    return "sample_c_mlod";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I_C:     return "gather4_i_c";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L_C:     return "gather4_l_c";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO:              return "sample_po";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_BIAS:         return "sample_po_b";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_LOD:          return "sample_po_l";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_COMPARE:      return "sample_po_c";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_DERIVS:       return "sample_po_d";
   case GEN_XE3_SAMPLER_MESSAGE_SAMPLE_PO_BIAS_COMPARE: return "sample_po_b_c";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_LOD_COMPARE:  return "sample_po_l_c";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_L:    return "gather4_po_l";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_B:    return "gather4_po_b";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_I:    return "gather4_po_i";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_C:    return "gather4_po_c";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_D_C:          return "sample_po_d_c";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_I_C:  return "gather4_po_i_c";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_L_C:  return "gather4_po_l_c";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_LZ:           return "sample_po_lz";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_C_LZ:         return "sample_po_c_lz";

   /* Value 18 aliases GATHER4_PO_C pre-Xe2 and MLOD on Xe2+. */
   case GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_C:
      return devinfo->ver >= 20 ? "sample_mlod" : "gather4_po_c";
   }
   return NULL;
}

/* TODO: This was initially based on the unambiguous cases from brw_sampler.c,
 * see if we can consolidate some of this between here and brw_sampler.c code.
 * Note some cases might be incomplete, sometimes brw would've made one
 * decision on the params but others are possible based on other factors.
 */
static const char *
sampler_params_to_string(const intel_device_info *devinfo, unsigned msg_type)
{
   switch (msg_type) {
   case GEN_SAMPLER_MESSAGE_SAMPLE:                    return "u,v,r,ai";
   case GEN_SAMPLER_MESSAGE_SAMPLE_COMPARE:            return "ref,u,v,r,ai";
   case GEN_SAMPLER_MESSAGE_SAMPLE_LD:                 return "u,v,lod,r";
   case GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4:            return "u,v,r,ai";
   case GEN_SAMPLER_MESSAGE_LOD:                       return "u,v,r,ai";
   case GEN_SAMPLER_MESSAGE_SAMPLE_RESINFO:            return "lod";
   case GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4_C:          return "ref,u,v,r,ai";
   case GEN_SAMPLER_MESSAGE_SAMPLE_LZ:                 return "u,v,r,ai";
   case GEN_SAMPLER_MESSAGE_SAMPLE_C_LZ:               return "ref,u,v,r,ai";
   case GEN_SAMPLER_MESSAGE_SAMPLE_LD_LZ:              return "u,v,r";
   case GEN_SAMPLER_MESSAGE_SAMPLE_LD_MCS:             return "u,v,r,lod";
   case GEN_SAMPLER_MESSAGE_SAMPLE_LD2DMS:             return "si,mcs,u,v,r,lod";
   case GEN_SAMPLER_MESSAGE_SAMPLE_LD2DSS:             return "ssi,u,v,r,lod";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I:      return "u,v,r,ai";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I_C:    return "ref,u,v,r,ai";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_B:      return "bias,u,v,r,ai";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L:      return "lod,u,v,r,ai";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L_C:    return "ref,lod,u,v,r,ai";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_DERIVS:      return "u,dudx,dudy,v,dvdx,dvdy,offuvr4_r,mlod";
   case GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_D_C:         return "ref,u,dudx,dudy,v,dvdx,dvdy,offuv4_r";

   case GEN_SAMPLER_MESSAGE_SAMPLE_LD2DMS_W:
      return devinfo->verx10 >= 125 ? "si,mcs0,mcs1,mcs2,mcs3,u,v,r,lod"
                                    : "si,mcsl,mcsh,u,v,r,lod";

   case GEN_SAMPLER_MESSAGE_SAMPLE_DERIVS:
      return devinfo->verx10 >= 125 ? "u,dudx,dudy,v,dvdx,dvdy,r,mlod"
                                    : "u,dudx,dudy,v,dvdx,dvdy,r,drdx,drdy,ai,mlod";

   case GEN_SAMPLER_MESSAGE_SAMPLE_DERIV_COMPARE:
      return devinfo->ver >= 20 ? NULL
                                : "ref,u,dudx,dudy,v,dvdx,dvdy,r,drdx,drdy,ai";

   default:
      /* TODO: Figure out whether instruction has enough information to
       * disambiguate those cases.
       */
      return NULL;
   }
}

static const char *
urb_opcode_to_string(unsigned opcode)
{
   switch (opcode) {
   case GEN_URB_OPCODE_ATOMIC_MOV:  return "atomic_mov";
   case GEN_URB_OPCODE_ATOMIC_INC:  return "atomic_inc";
   case GEN_URB_OPCODE_ATOMIC_ADD:  return "atomic_add";
   case GEN_URB_OPCODE_SIMD8_WRITE: return "simd8_write";
   case GEN_URB_OPCODE_SIMD8_READ:  return "simd8_read";
   case GEN_GFX125_URB_OPCODE_FENCE:     return "fence";
   }
   return NULL;
}

static const char *
hdc1_surface_simd_mode_to_string(unsigned field)
{
   switch (field) {
   case 0: return "simd4x2";
   case 1: return "simd16";
   case 2: return "simd8";
   }
   return NULL;
}

static const char *
hdc1_aop_to_string(unsigned aop)
{
   switch (aop) {
   case GEN_AOP_AND:    return "and";
   case GEN_AOP_OR:     return "or";
   case GEN_AOP_XOR:    return "xor";
   case GEN_AOP_MOV:    return "mov";
   case GEN_AOP_INC:    return "inc";
   case GEN_AOP_DEC:    return "dec";
   case GEN_AOP_ADD:    return "add";
   case GEN_AOP_SUB:    return "sub";
   case GEN_AOP_REVSUB: return "revsub";
   case GEN_AOP_IMAX:   return "imax";
   case GEN_AOP_IMIN:   return "imin";
   case GEN_AOP_UMAX:   return "umax";
   case GEN_AOP_UMIN:   return "umin";
   case GEN_AOP_CMPWR:  return "cmpwr";
   case GEN_AOP_PREDEC: return "predec";
   }
   return NULL;
}

static const char *
hdc1_aop_float_to_string(unsigned aop)
{
   switch (aop) {
   case GEN_AOP_FMAX:   return "fmax";
   case GEN_AOP_FMIN:   return "fmin";
   case GEN_AOP_FCMPWR: return "fcmpwr";
   case GEN_AOP_FADD:   return "fadd";
   }
   return NULL;
}

static const char *
hdc1_owords_to_string(unsigned v)
{
   switch (v) {
   case GEN_DATAPORT_OWORD_BLOCK_1_OWORDLOW:  return "owords1_low";
   case GEN_DATAPORT_OWORD_BLOCK_1_OWORDHIGH: return "owords1_high";
   case GEN_DATAPORT_OWORD_BLOCK_2_OWORDS:    return "owords2";
   case GEN_DATAPORT_OWORD_BLOCK_4_OWORDS:    return "owords4";
   case GEN_DATAPORT_OWORD_BLOCK_8_OWORDS:    return "owords8";
   case GEN_GFX12_DATAPORT_OWORD_BLOCK_16_OWORDS: return "owords16";
   }
   return NULL;
}

static const char *
rt_write_subtype_to_string(const intel_device_info *devinfo,
                              unsigned subtype)
{
   if (devinfo->ver >= 20) {
      switch (subtype) {
      case 0: return "simd16";
      case 1: return "simd32";
      case 2: return "simd16_dualsrc";
      }
   } else {
      switch (subtype) {
      case 0: return "simd16";
      case 1: return "simd16_repdata";
      case 2: return "simd8_dualsrc_low";
      case 3: return "simd8_dualsrc_high";
      case 4: return "simd8";
      case 5: return "simd8_imagewrite";
      case 7: return "simd16_repdata_7";
      }
   }
   return NULL;
}


struct gen_printer {
   enum {
      OPCODE_COLUMN = 8,
      MODIFIER_COLUMN = 23,
      DST_COLUMN = 34,
      SRC_START_COLUMN = 48,
      SRC_STRIDE = 18,
      OPTIONS_COLUMN = 82,
      SEND_SRC0_COLUMN = 42,
      SEND_SRC1_COLUMN = 49,
      SEND_EX_DESC_COLUMN = 57,
      SEND_DESC_COLUMN = 69,
      SEND_COMMENT_COLUMN = 87,
      BRANCH_SRC_COLUMN = 46,
      BRANCH_UIP_COLUMN = 66,
   };

   const gen_print_params *params;
   const intel_device_info *devinfo;
   FILE *fp;
   gen_print_flags flags;
   std::vector<gen_inst_layout> inst_layouts;
   uint64_t address_base;
   std::vector<gen_label> labels;
   unsigned column;

   const gen_inst *inst;
   int idx;
   gen_format inst_format;

   /* True when the opcode column contains the translated send form instead of
    * the raw send opcode. Otherwise the translated form appears in a comment.
    */
   bool translated_send_as_opcode = false;

   gen_printer(const gen_print_params *print_params)
      : params(print_params), devinfo(print_params->devinfo), fp(print_params->fp),
        flags(print_params->flags), address_base(print_params->address_base),
        column(0)
   {
      if (params->raw_bytes && params->num_insts > 0) {
         inst_layouts.resize(params->num_insts);
         gen_scan_raw_layout_params layout = {};
         layout.raw_bytes = params->raw_bytes;
         layout.raw_bytes_size = params->raw_bytes_size;
         layout.layouts = inst_layouts.data();
         layout.num_insts = params->num_insts;
         if (!gen_scan_raw_layout(&layout) ||
             layout.num_insts != params->num_insts) {
            inst_layouts.clear();
         }
      }
   }

   void
   print_program()
   {
      assert(params->insts);

      const gen_inst *insts = params->insts;
      const int num_insts = params->num_insts;
      const gen_error *errors = params->errors;
      const int num_errors = params->num_errors;
      const char *const *annotations = params->annotations;
      const char *const *label_annotations = params->label_annotations;

      labels = collect_labels(insts, num_insts);

      unsigned next_label = 0;
      int next_error = 0;
      const char *last_annotation = NULL;
      for (int i = 0; i < num_insts; i++) {
         const char *label_annotation =
            label_annotations ? label_annotations[i] : NULL;
         if (next_label < labels.size() && labels[next_label].target == i) {
            print_label(labels[next_label].label, label_annotation);
            next_label++;
         } else {
            print_annotation_text(label_annotation);
         }

         const char *annotation = annotations ? annotations[i] : NULL;
         if (!annotation_text_equal(last_annotation, annotation))
            print_annotation_text(annotation);
         last_annotation = annotation;

         print_inst(&insts[i], i);

         while (next_error < num_errors &&
                (int)errors[next_error].index == i) {
            fprintf(fp, "    ERROR: %s\n", errors[next_error].msg);
            next_error++;
         }
      }

      if (next_label < labels.size() && labels[next_label].target == num_insts) {
         fprintf(fp, "L%u:\n", labels[next_label].label);
         column = 0;
      }
   }

   void
   print_inst(const gen_inst *inst_, int idx_)
   {
      inst = inst_;
      idx = idx_;
      inst_format = gen_inst_format(inst->opcode);
      translated_send_as_opcode = false;

      print_offset_prefix();
      print_prefix();
      pad(OPCODE_COLUMN);
      print_opcode();

      if (inst->exec_size) {
         text(" ");
         print_exec_control();
      }

      if (has_cond_modifier()) {
         field_sep(MODIFIER_COLUMN);
         print_cond_modifier();
      }

      print_operands();
      print_options();
      print_send_comment();
      newline();
   }

private:
   static bool
   annotation_text_equal(const char *a, const char *b)
   {
      const bool na = a && a[0];
      const bool nb = b && b[0];
      if (!na || !nb)
         return na == nb;
      return strcmp(a, b) == 0;
   }

   void
   print_annotation_text(const char *s)
   {
      if (!s || !s[0])
         return;

      fputs("\n// ", fp);
      fputs(s, fp);
      if (s[strlen(s) - 1] != '\n')
         fputc('\n', fp);
      column = 0;
   }

   void
   print_label(unsigned label, const char *s)
   {
      fprintf(fp, "\nL%u:", label);
      if (s && s[0])
         fprintf(fp, " // %s", s);
      fputc('\n', fp);
      column = 0;
   }

   static std::vector<gen_label>
   collect_labels(const gen_inst *insts, int num_insts)
   {
      std::vector<gen_label> labels;

      for (int i = 0; i < num_insts; i++) {
         const gen_inst *inst = &insts[i];
         const gen_format format = gen_inst_format(inst->opcode);
         if (format != GEN_FORMAT_BRANCH_ONE_SRC &&
             format != GEN_FORMAT_BRANCH_TWO_SRC)
            continue;

         for (unsigned s = 0; s < 2; s++) {
            if (s == 1 && format != GEN_FORMAT_BRANCH_TWO_SRC)
               break;
            if (inst->src[s].file != GEN_IMM || !inst->src[s].imm)
               continue;
            const int target = branch_target_index(i, inst->src[s].imm);
            if (target < 0 || target > num_insts)
               continue;

            auto label = std::lower_bound(labels.begin(), labels.end(), target,
                                          [](const gen_label &label, int target) {
                                             return label.target < target;
                                          });
            if (label == labels.end() || label->target != target)
               labels.insert(label, { target, 0 });
         }
      }

      for (unsigned i = 0; i < labels.size(); i++)
         labels[i].label = i;

      return labels;
   }

   void
   unknown()
   {
      text("?");
   }

   void
   print_offset_prefix()
   {
      if (!(flags & GEN_PRINT_BYTE_OFFSETS) || inst_layouts.empty())
         return;

      fprintf(fp, "0x%08" PRIx64 ": ",
              address_base + (uint64_t)inst_layouts[idx].offset);
   }

   bool
   verbose() const
   {
      return flags & GEN_PRINT_VERBOSE;
   }

   bool
   has_cond_modifier() const
   {
      return gen_inst_has_cond_modifier(devinfo, inst_format, inst) &&
             inst->cmod != GEN_CONDITION_NONE;
   }

   bool
   omit_dst_region(const gen_operand &dst) const
   {
      return !verbose() && dst.region.hstride == 1;
   }

   /* True when reading 'exec_size' elements from a region '<v;w,h>' yields
    * 'exec_size' consecutive elements starting at the base, i.e. the region
    * is semantically a fully-packed contiguous read. Element k of the
    * source lands at offset (k/w)*v + (k%w)*h; that equals k iff
    *
    *   v == w && (w == 1 || h == 1)
    *
    * which captures the common encodings <1;1,0>, <8;8,1>, <16;16,1>, etc.
    *
    * The exec_size itself does not appear in the predicate: any sequential
    * region produces a packed read no matter how many elements the
    * instruction consumes. */
   static bool
   sequential_src_region(const gen_region &r)
   {
      return r.vstride == r.width && (r.width == 1 || r.hstride == 1);
   }

   bool
   omit_src_region(const gen_operand &src) const
   {
      return !verbose() && sequential_src_region(src.region);
   }

   bool
   uniform_src_region(const gen_region &region) const
   {
      return !verbose() &&
             region.vstride == 0 &&
             region.width == 1 &&
             region.hstride == 0;
   }

   bool
   shorthand_src_region_stride(const gen_region &region, unsigned &stride) const
   {
      if (verbose() || region.vstride == GEN_VSTRIDE_ONE_DIMENSIONAL)
         return false;

      if (region.width == 1) {
         stride = region.vstride;
         return true;
      }

      if (region.hstride > 1 && region.vstride == region.width * region.hstride) {
         stride = region.hstride;
         return true;
      }

      return false;
   }

   bool
   omit_type(gen_reg_type type) const
   {
      return !verbose() && type == GEN_TYPE_UD;
   }

   void
   print_flag_ref(unsigned nr, unsigned subnr)
   {
      format("f%u.%u", nr, subnr);
   }

   void
   print_prefix()
   {
      if (!inst->no_mask && !inst->pred_control)
         return;

      text("(");

      if (inst->no_mask)
         text("W");

      if (inst->no_mask && inst->pred_control)
         text("&");

      if (inst->pred_control) {
         if (inst->pred_inv)
            text("~");

         print_flag_ref(inst->flag_nr, inst->flag_subnr);
         text(pred_ctrl_suffix(devinfo, inst->align16, inst->pred_control));
      }

      text(") ");
   }

   void
   print_opcode()
   {
      if (flags & GEN_PRINT_TRANSLATED_SENDS &&
          gen_inst_is_send(inst) &&
          is_lsc_translated_sfid(inst->send.sfid) &&
          print_translated_send()) {
         translated_send_as_opcode = true;
         return;
      }

      text(gen_opcode_to_string(inst->opcode));

      if (gen_inst_is_send(inst)) {
         text(".");
         text(gen_sfid_to_string(devinfo, inst->send.sfid));
      }

      switch (inst->opcode) {
      case GEN_OP_MATH: {
         text(".");
         text(math_function_name(inst->math.func));
         break;
      }

      case GEN_OP_SYNC: {
         const char *name = sync_function_name(inst->sync.func);
         if (name) {
            text(".");
            text(name);
         }
         break;
      }

      case GEN_OP_BFN:
         /* TODO: Have a table for a nicer formatting for BFN function. */
         format(".0x%02x", inst->boolean_func_ctrl);
         break;

      case GEN_OP_DPAS: {
         text(".");
         uint(inst->dpas.sdepth);
         text("x");
         uint(inst->dpas.rcount);
         break;
      }

      default:
         break;
      }

      if (gen_has_branch_ctrl(inst->opcode) && inst->branch_control)
         text(".b");

      if (gen_inst_has_saturate(inst_format, inst) && inst->saturate)
         text(".sat");
   }

   void
   print_cond_modifier()
   {
      text("(");
      text(cond_modifier_name(inst->cmod));
      text(")");

      print_flag_ref(inst->flag_nr, inst->flag_subnr);
   }

   void
   print_exec_control()
   {
      text("(");
      uint(inst->exec_size);
      if (verbose() || inst->chan_offset != 0) {
         text("|M");
         uint(inst->chan_offset);
      }
      text(")");
   }

   void
   print_operands()
   {
      switch (inst_format) {
      case GEN_FORMAT_BASIC_ONE_SRC:
      case GEN_FORMAT_BASIC_TWO_SRC:
      case GEN_FORMAT_BASIC_THREE_SRC:
      case GEN_FORMAT_DPAS_THREE_SRC: {
         const unsigned num_sources = gen_inst_num_sources(devinfo, inst);
         const bool has_dst = gen_inst_has_dst(inst_format, inst->opcode);

         if (inst->opcode == GEN_OP_WAIT) {
            field_sep(DST_COLUMN);
            print_dst();
            break;
         }

         if (has_dst) {
            field_sep(DST_COLUMN);
            print_dst();
         }

         for (unsigned i = 0; i < num_sources; i++) {
            field_sep(SRC_START_COLUMN + SRC_STRIDE * i);
            print_src(i);
         }
         break;
      }

      case GEN_FORMAT_SEND: {
         field_sep(DST_COLUMN);
         print_send_reg(inst->dst, false,
                        translated_send_as_opcode ? gen_inst_send_dst_len(inst) : -1);

         field_sep(SEND_SRC0_COLUMN);
         print_send_reg(inst->src[0], true,
                        translated_send_as_opcode
                           ? gen_inst_send_src0_len(inst)
                           : -1);

         if (translated_send_as_opcode) {
            if (gen_inst_is_split_send(devinfo, inst) &&
                !is_null(inst->src[1])) {
               field_sep(SEND_SRC1_COLUMN);
               print_send_reg(inst->src[1], true,
                              gen_inst_send_src1_len(devinfo, inst));
            }
            break;
         }

         field_sep(SEND_SRC1_COLUMN);
         if (gen_inst_is_split_send(devinfo, inst)) {
            const bool src1_needs_length =
               inst->send.ex_bso ||
               (devinfo->ver >= 20 &&
                inst->send.sfid == GEN_SFID_UGM) ||
               (devinfo->verx10 >= 125 &&
                !inst->send.ex_desc_is_reg);
            int src1_length = -1;
            if (src1_needs_length) {
               src1_length = gen_inst_send_src1_len(devinfo, inst);
               /* For a null payload there is no length encoded in the
                * descriptor (and for reg-ex_desc the helper can't recover
                * one); the canonical printout is 'null:0' under the
                * length-emitting rule. */
               if (src1_length < 0 && is_null(inst->src[1]))
                  src1_length = 0;
            }
            print_send_reg(inst->src[1], true, src1_length);
         }

         field_sep(SEND_EX_DESC_COLUMN);
         print_send_ex_desc();

         field_sep(SEND_DESC_COLUMN);
         if (inst->send.desc_is_reg)
            text("a0.0");
         else
            hex_pad0(inst->send.desc_imm);
         break;
      }

      case GEN_FORMAT_BRANCH_ONE_SRC:
      case GEN_FORMAT_BRANCH_TWO_SRC: {
         const bool two_src = (inst_format == GEN_FORMAT_BRANCH_TWO_SRC);

         field_sep(BRANCH_SRC_COLUMN);
         if (inst->src[0].file != GEN_IMM) {
            print_src(0);
            break;
         }

         if (two_src)
            text("jip:");
         print_branch_target(inst->src[0].imm);

         if (two_src) {
            field_sep(BRANCH_UIP_COLUMN);
            text("uip:");
            print_branch_target(inst->src[1].imm);
         }
         break;
      }

      case GEN_FORMAT_ILLEGAL:
      case GEN_FORMAT_NOP:
         break;
      }
   }

   void
   print_branch_target(int32_t rel_bytes)
   {
      const int target = branch_target_index(idx, rel_bytes);
      auto it = std::lower_bound(labels.begin(), labels.end(), target,
                                 [](const gen_label &label, int target) {
                                    return label.target < target;
                                 });
      if (it != labels.end() && it->target == target) {
         format("L%u", it->label);
         return;
      }

      char buf[32];
      snprintf(buf, sizeof(buf), "0x%x", (uint32_t)rel_bytes);
      text(buf);
   }

   void
   print_send_ex_desc()
   {
      if (inst->send.ex_desc_is_reg) {
         if (inst->send.ex_desc_imm_extra) {
            hex_pad0(inst->send.ex_desc_imm_extra);
            text(":");
         }
         text("a0.");
         uint(inst->send.ex_desc_subnr / 2);
      } else {
         hex_pad0(inst->send.ex_desc_imm);
      }
   }

   void
   print_send_reg(const gen_operand &op, bool allow_scalar_arf, int length = -1)
   {
      if (op.file == GEN_GRF && !op.indirect) {
         text("r");
         uint(op.nr);
         if (op.subnr) {
            text(".");
            uint(op.subnr / 16);
         }
      } else if (allow_scalar_arf && op.file == GEN_ARF && !op.indirect &&
                 (op.nr & 0xf0) == GEN_ARF_SCALAR) {
         text("r[");
         print_register(op, true, false);
         text("]");
      } else if (is_null(op)) {
         text("null");
      } else {
         print_register(op, false, false);
      }

      /* Caller signals ":N emission" by passing length >= 0. The role-based
       * gating happens entirely in the caller; this helper does not
       * special-case null - 'null:0' is the correct printout when the role
       * rule selects the operand for a length suffix. */
      if (length >= 0) {
         text(":");
         uint(length);
      }
   }

   void
   print_dst()
   {
      const gen_operand &dst = inst->dst;

      print_register(dst, verbose(), false);
      if (!omit_dst_region(dst)) {
         text("<");
         uint(dst.region.hstride);
         text(">");
      }
      if (inst->align16) {
         if (dst.writemask == 0) {
            text(".");
         } else if (dst.writemask != 0xf) {
            text(".");
            text(chan_mask_str[dst.writemask]);
         }
      }
      print_type(dst.type);
   }

   void
   print_src(unsigned src_idx)
   {
      assert(src_idx < 3);
      const gen_operand &src = inst->src[src_idx];
      const bool three_src = (inst_format == GEN_FORMAT_BASIC_THREE_SRC);

      if (src.negate)
         text("-");
      if (src.abs)
         text("(abs)");

      if (src.file == GEN_IMM) {
         print_imm(src);
      } else if (!three_src && src.file == GEN_ARF && src.nr == GEN_ARF_NULL) {
         print_arf(src, false, false);
      } else {
         print_register(src, verbose(), false);
         if (!omit_src_region(src))
            print_region(src.region, src_idx);

         if (inst->align16) {
            if (three_src) {
               if (src.rep_ctrl)
                  text(".r");
            } else {
               print_swizzle(src.swizzle);
            }
         }

         print_type(src.type);
      }
   }

   void
   print_region(const gen_region &region, unsigned src_idx)
   {
      if (uniform_src_region(region)) {
         text("<0>");
         return;
      }

      const bool three_src = (inst_format == GEN_FORMAT_BASIC_THREE_SRC);

      unsigned stride = 0;

      text("<");
      if (three_src) {
         if (src_idx < 2) {
            uint(region.vstride);
            text(";");
            uint(region.hstride);
         } else {
            uint(region.hstride);
         }
      } else if (shorthand_src_region_stride(region, stride)) {
         uint(stride);
      } else {
         if (region.vstride == GEN_VSTRIDE_ONE_DIMENSIONAL) {
            text("VxH");
         } else {
            uint(region.vstride);
         }
         text(";");
         uint(region.width);
         text(",");
         uint(region.hstride);
      }
      text(">");
   }

   void
   print_swizzle(unsigned swizzle)
   {
      const unsigned x = (swizzle >> 0) & 0x3;
      const unsigned y = (swizzle >> 2) & 0x3;
      const unsigned z = (swizzle >> 4) & 0x3;
      const unsigned w = (swizzle >> 6) & 0x3;

      if (x == y && x == z && x == w) {
         text(".");
         text(chan_sel[x]);
      } else if (swizzle != gen_swizzle4(0, 1, 2, 3)) {
         text(".");
         text(chan_sel[x]);
         text(chan_sel[y]);
         text(chan_sel[z]);
         text(chan_sel[w]);
      }
   }

   void
   print_register(const gen_operand &op, bool show_default_zero, bool send_desc_reg)
   {
      if (op.indirect) {
         print_indirect_register(op);
         return;
      }

      switch (op.file) {
      case GEN_GRF:
         text("r");
         uint(op.nr);
         print_grf_subreg(op, show_default_zero);
         return;

      case GEN_ARF:
         print_arf(op, show_default_zero, send_desc_reg);
         return;

      case GEN_IMM:
         print_imm(op);
         return;

      case GEN_BAD_FILE:
      default:
         text("bad");
         return;
      }
   }

   void
   print_grf_subreg(const gen_operand &op, bool show_default_zero)
   {
      const unsigned type_size = MAX2(gen_type_size_bytes(op.type), 1u);
      if (!show_default_zero && op.subnr == 0)
         return;

      text(".");
      uint(op.subnr / type_size);
   }

   void
   print_arf(const gen_operand &op, bool show_default_zero, bool send_desc_reg)
   {
      const unsigned arf = op.nr & 0xf0;
      const unsigned idx = op.nr & 0x0f;

      switch (arf) {
      case GEN_ARF_NULL:
         text("null");
         return;

      case GEN_ARF_IP:
         text("ip");
         return;

      case GEN_ARF_TDR:
         text("tdr");
         uint(idx);
         return;

      case GEN_ARF_ADDRESS:
         text("a");
         uint(idx);
         print_chan_subreg(op.subnr, show_default_zero, send_desc_reg);
         return;

      case GEN_ARF_ACCUMULATOR:
         text("acc");
         uint(idx);
         print_chan_subreg(op.subnr, show_default_zero, true);
         return;

      case GEN_ARF_FLAG:
         text("f");
         uint(idx);
         if (show_default_zero || op.subnr) {
            text(".");
            uint(op.subnr);
         }
         return;

      case GEN_ARF_MASK:
         text("mask");
         uint(idx);
         return;

      case GEN_ARF_STATE:
         text("sr");
         uint(idx);
         return;

      case GEN_ARF_SCALAR:
         text("s");
         uint(idx);
         print_chan_subreg(op.subnr, show_default_zero, send_desc_reg);
         return;

      case GEN_ARF_CONTROL:
         text("cr");
         uint(idx);
         print_chan_subreg(op.subnr, show_default_zero, send_desc_reg);
         return;

      case GEN_ARF_NOTIFICATION_COUNT:
         text("n");
         uint(idx);
         if (show_default_zero || op.subnr) {
            text(".");
            uint(op.subnr / MAX2(gen_type_size_bytes(op.type), 1u));
         }
         return;

      case GEN_ARF_TIMESTAMP:
         text("tm");
         uint(idx);
         return;

      default:
         text("arf");
         uint(op.nr);
         return;
      }
   }

   void
   print_chan_subreg(unsigned subnr, bool show_default_zero, bool numeric)
   {
      if (!show_default_zero && subnr == 0)
         return;

      text(".");
      if (numeric) {
         uint(subnr / 2);
      } else if ((subnr % 2) == 0 && (subnr / 2) < 4) {
         text(chan_sel[subnr / 2]);
      } else {
         uint(subnr);
      }
   }

   void
   print_indirect_register(const gen_operand &op)
   {
      text("r[");
      text("a0");
      print_chan_subreg(op.subnr, true, false);
      if (op.addr_imm > 0) {
         text(" + ");
         uint(op.addr_imm);
      } else if (op.addr_imm < 0) {
         text(" - ");
         uint(-op.addr_imm);
      }
      text("]");
   }

   void
   print_type(enum gen_reg_type type)
   {
      if (omit_type(type))
         return;

      text(":");
      text(type_name(type));
   }

   void
   print_imm(const gen_operand &src)
   {
      switch (src.type) {
      case GEN_TYPE_UQ:
      case GEN_TYPE_Q:
      case GEN_TYPE_DF:
         format("0x%016" PRIx64, src.imm);
         break;
      case GEN_TYPE_UD:
      case GEN_TYPE_UV:
      case GEN_TYPE_V:
      case GEN_TYPE_VF:
      case GEN_TYPE_F:
         format("0x%08x", (uint32_t)src.imm);
         break;
      case GEN_TYPE_D:
         format("%" PRId32, (int32_t)src.imm);
         break;
      case GEN_TYPE_UW:
         format("%" PRIu16, (uint16_t)src.imm);
         break;
      case GEN_TYPE_W:
         format("%" PRId16, (int16_t)src.imm);
         break;
      case GEN_TYPE_HF:
         format("0x%04x", (uint16_t)src.imm);
         break;
      case GEN_TYPE_UB:
         format("%" PRIu8, (uint8_t)src.imm);
         break;
      case GEN_TYPE_B:
         format("%" PRId8, (int8_t)src.imm);
         break;
      default:
         text("<?>:?");
         return;
      }
      print_type(src.type);
   }

   void
   print_options()
   {
      bool started = false;
      auto next = [&] {
         if (!started) { field_sep(OPTIONS_COLUMN); text("{"); started = true; }
         else          { text(","); }
      };

      if (inst->acc_wr_control)                         { next(); text("AccWrEn"); }
      if (inst->atomic_control ||
          inst->thread_control == GEN_THREAD_ATOMIC)    { next(); text("Atomic"); }
      if (inst->debug_control)                          { next(); text("Breakpoint"); }
      if (inst->align16)                                { next(); text("Align16"); }
      if (!inst_layouts.empty() && inst_layouts[idx].was_compacted) { next(); text("Compacted"); }
      if (gen_inst_is_send(inst) && inst->send.eot)     { next(); text("EOT"); }
      if (inst->no_dd_check)                            { next(); text("NoDDChk"); }
      if (inst->no_dd_clear)                            { next(); text("NoDDClr"); }
      if (inst->thread_control == GEN_THREAD_SWITCH)    { next(); text("Switch"); }
      if (inst->fusion_control)                         { next(); text("Serialize"); }
      if (gen_inst_is_send(inst) && inst->send.ex_bso)  { next(); text("ExBSO"); }

      if (inst->swsb.regdist || inst->swsb.mode) {
         next();
         column += emit_swsb(devinfo, fp, inst->swsb, ",");
      }

      if (started) text("}");
   }

   void
   print_send_comment()
   {
      if (translated_send_as_opcode || !translated_send_may_apply())
         return;

      pad(column + 2);
      text("// ");

      /* All translation comments (LSC and pre-LSC) are prefixed with the
       * message-length info derivable purely from the descriptor. The
       * SFID-specific text follows the prefix. */
      print_send_lengths();

      if (!print_translated_send())
         unknown();
   }

   void
   print_send_lengths()
   {
      const gen_message_desc msg =
         gen_message_desc_decode(devinfo, inst->send.desc_imm);
      const bool header = msg.header_present &&
                          !is_lsc_translated_sfid(inst->send.sfid);
      const int ex_mlen = gen_inst_send_src1_len(devinfo, inst);
      text("wr:");
      uint(msg.msg_length);
      if (header)
         text("h");
      text("+");
      if (ex_mlen >= 0) {
         uint(ex_mlen);
      } else {
         text("a0.");
         uint(inst->send.ex_desc_subnr / 2);
      }
      text(", rd:");
      uint(msg.response_length);
      text("; ");
   }

   void
   field_sep(unsigned col)
   {
      if (column < col)
         pad(col);
      else
         text(" ");
   }

   void
   format(const char *fmt, ...) PRINTFLIKE(2, 3)
   {
      char buf[1024];
      va_list args;
      va_start(args, fmt);
      vsnprintf(buf, sizeof(buf), fmt, args);
      va_end(args);
      text(buf);
   }

   void
   pad(unsigned col)
   {
      while (column < col)
         text(" ");
   }

   void
   uint(unsigned n)
   {
      char buf[16];
      int len = snprintf(buf, sizeof(buf), "%u", n);
      fputs(buf, fp);
      column += len;
   }

   void
   hex_pad0(unsigned n)
   {
      fprintf(fp, "0x%08X", n);
      column += 10;
   }

   void
   text(const char *s)
   {
      fputs(s, fp);
      for (const char *p = s; *p; p++)
         column = *p == '\n' ? 0 : column + 1;
   }

   void
   text(const char *a, const char *b)
   {
      text(a);
      text(b);
   }

   void
   newline()
   {
      fputc('\n', fp);
      column = 0;
   }

   uint32_t
   lsc_ex_desc_surface_bits() const
   {
      if (inst->send.ex_desc_is_reg)
         return 0;

      const int src1_len = gen_inst_send_src1_len(devinfo, inst);
      const uint32_t ex_mlen =
         src1_len > 0 ? gen_message_ex_desc(devinfo, src1_len) : 0;
      return inst->send.ex_desc_imm & ~ex_mlen;
   }

   bool
   send_ex_desc_is_only_ex_mlen() const
   {
      if (inst->send.ex_desc_is_reg || inst->send.ex_desc_imm_extra)
         return false;

      const uint32_t ex_mlen_mask =
         gen_message_ex_desc(devinfo, devinfo->ver >= 20 ? 0x1f : 0xf);
      return (inst->send.ex_desc_imm & ~ex_mlen_mask) == 0;
   }

   bool
   is_lsc_translated_sfid(gen_sfid sfid) const
   {
      switch (sfid) {
      case GEN_SFID_SLM:
      case GEN_SFID_TGM:
      case GEN_SFID_UGM:
         return devinfo->has_lsc;
      case GEN_SFID_URB:
         return devinfo->has_lsc && devinfo->ver >= 20;
      default:
         return false;
      }
   }

   void
   print_lsc_immediate_offset(int signed_off)
   {
      if (signed_off >= 0)
         format("A+0x%x", (unsigned)signed_off);
      else
         format("A-0x%x", (unsigned)-signed_off);
   }

   bool
   print_lsc_symbolic_surface_name(enum lsc_addr_surface_type addr_type)
   {
      const uint32_t surface_bits = lsc_ex_desc_surface_bits();
      const gen_lsc_ex_desc ex_desc =
         gen_lsc_ex_desc_decode(devinfo, addr_type, surface_bits,
                                inst->send.ex_desc_imm_extra);

      switch (inst->send.sfid) {
      case GEN_SFID_SLM:
      case GEN_SFID_URB:
         return addr_type == LSC_ADDR_SURFTYPE_FLAT &&
                !inst->send.ex_desc_is_reg && !inst->send.ex_desc_imm_extra &&
                ex_desc.flat.base_offset == 0;

      case GEN_SFID_UGM:
      case GEN_SFID_TGM:
         switch (addr_type) {
         case LSC_ADDR_SURFTYPE_FLAT:
            if (inst->send.ex_desc_is_reg || inst->send.ex_desc_imm_extra)
               return false;
            if (ex_desc.flat.base_offset != 0) {
               text(".flat[");
               print_lsc_immediate_offset(ex_desc.flat.base_offset);
               text("]");
            }
            return true;

         case LSC_ADDR_SURFTYPE_BSS:
         case LSC_ADDR_SURFTYPE_SS: {
            if (!inst->send.ex_desc_is_reg)
               return false;
            format(".%s[a0.%u]",
                   addr_type == LSC_ADDR_SURFTYPE_BSS ? "bss" : "ss",
                   inst->send.ex_desc_subnr / 2);
            if (ex_desc.surface_state.base_offset != 0) {
               text("[");
               print_lsc_immediate_offset(ex_desc.surface_state.base_offset);
               text("]");
            }
            return true;
         }

         case LSC_ADDR_SURFTYPE_BTI:
            if (inst->send.ex_desc_is_reg || inst->send.ex_desc_imm_extra)
               return false;
            format(".bti[%u]", ex_desc.bti.index);
            if (ex_desc.bti.base_offset != 0) {
               text("[");
               print_lsc_immediate_offset(ex_desc.bti.base_offset);
               text("]");
            }
            return true;

         default:
            return false;
         }

      default:
         return false;
      }
   }

   void
   print_hdc1_surface_tail(unsigned msg_ctrl)
   {
      const unsigned simd_field = (msg_ctrl >> 4) & 0x3u;
      const char *simd_name = hdc1_surface_simd_mode_to_string(simd_field);
      format(" %s", simd_name ? simd_name : "simd?");
   }

   void
   print_hdc1_atomic_tail(unsigned msg_ctrl)
   {
      const unsigned simd_bit = (msg_ctrl >> 4) & 0x1u;
      text(simd_bit ? " simd8" : " simd16");
   }

   void
   print_translation_exec_size()
   {
      format(" (%u)", inst->exec_size);
   }

   void
   print_translation_bti(unsigned bti)
   {
      format(" bti(%u)", bti);
   }

   bool
   print_lsc_translated_send()
   {
      if (!gen_inst_is_send(inst) || !is_lsc_translated_sfid(inst->send.sfid) ||
          inst->send.desc_is_reg)
         return false;

      const char *sfid_name = gen_sfid_to_string(devinfo, inst->send.sfid);
      const gen_lsc_desc desc = gen_lsc_desc_decode(devinfo, inst->send.desc_imm);

      const enum lsc_opcode op = desc.op;
      const char *op_name = lsc_opcode_name(op);
      if (!op_name) {
         unknown();
         return true;
      }

      if (op == LSC_OP_FENCE) {
         if (desc.addr_type != LSC_ADDR_SURFTYPE_FLAT || inst->send.ex_desc_is_reg ||
             lsc_ex_desc_surface_bits() != 0 ||
             inst->send.ex_desc_imm_extra) {
            unknown();
            return true;
         }

         const char *scope_name = lsc_fence_scope_name(desc.fence.scope);
         const char *flush_name = lsc_flush_type_name(desc.fence.flush_type);
         if (!scope_name || !flush_name) {
            unknown();
            return true;
         }

         format("%s.%s.%s.%s", op_name, sfid_name, scope_name, flush_name);
         if (desc.fence.route_to_lsc)
            text(".route_to_lsc");
         return true;
      }

      const char *data_name = lsc_data_size_name(desc.data_size);
      if (!data_name) {
         unknown();
         return true;
      }

      format("%s.%s.%s", op_name, sfid_name, data_name);

      if (lsc_opcode_has_cmask(op)) {
         const char *cmask_name = lsc_cmask_name(desc.cmask);
         if (!cmask_name) {
            text(".?");
            return true;
         }
         text(".", cmask_name);
      } else if (!lsc_opcode_is_atomic(op)) {
         const unsigned num_values = lsc_vector_length_local(desc.vect_size);
         const bool transpose = desc.transpose;
         if (num_values == 0) {
            text(".?");
            return true;
         }
         if (num_values != 1 || transpose) {
            format("x%u", num_values);
            if (transpose)
               text("t");
         }
      }

      const char *addr_name = lsc_addr_size_name(desc.addr_size);
      if (!addr_name) {
         text(".?");
         return true;
      }
      text(".", addr_name);

      const char *cache = lsc_cache_ctrl_name(devinfo, op, desc.cache_ctrl);
      if (!cache) {
         text(".?");
         return true;
      }
      if (cache[0])
         text(".", cache);

      if (!print_lsc_symbolic_surface_name(desc.addr_type)) {
         text(".?");
         return true;
      }

      return true;
   }

   bool
   print_sampler_translated_send()
   {
      assert(gen_inst_is_send(inst));
      assert(!inst->send.desc_is_reg);
      assert(inst->send.sfid == GEN_SFID_SAMPLER);

      const gen_sampler_desc desc =
         gen_sampler_desc_decode(devinfo, inst->send.desc_imm);

      const char *op_name = sampler_msg_type_to_string(devinfo, desc.msg_type);
      if (!op_name) {
         unknown();
         return true;
      }

      /* msg_type[.hp][:params] */
      text(op_name);
      if (desc.return_hp)
         text(".hp");

      const char *params = sampler_params_to_string(devinfo, desc.msg_type);
      if (params)
         format(":%s", params);

      print_translation_exec_size();

      if (inst->send.ex_desc_is_reg && desc.bti == GEN_BTI_BINDLESS)
         format(" bss(a0.%u)", inst->send.ex_desc_subnr / 2);
      else
         print_translation_bti(desc.bti);

      format(" using sampler index %u", desc.sampler_index);
      return true;
   }

   bool
   print_urb_translated_send()
   {
      assert(gen_inst_is_send(inst));
      assert(!inst->send.desc_is_reg);
      assert(inst->send.sfid == GEN_SFID_URB);

      if (devinfo->ver >= 20)
         return false;

      const gen_urb_desc desc = gen_urb_desc_decode(devinfo, inst->send.desc_imm);

      const char *op_name = urb_opcode_to_string(desc.op);
      if (!op_name) {
         unknown();
         return true;
      }

      if (!send_ex_desc_is_only_ex_mlen()) {
         unknown();
         return true;
      }

      text(op_name);

      if (desc.op == GEN_GFX125_URB_OPCODE_FENCE) {
         if (desc.global_offset != 0 || desc.swizzle || desc.per_slot_offset)
            text(" ?");
         print_translation_exec_size();
         return true;
      }

      if (desc.global_offset != 0)
         format(" off=%u", desc.global_offset);

      if (desc.swizzle)
         text(desc.op == GEN_URB_OPCODE_SIMD8_WRITE ||
              desc.op == GEN_URB_OPCODE_SIMD8_READ ? " masked" : " interleave");

      if (desc.per_slot_offset)
         text(" per_slot");

      print_translation_exec_size();
      return true;
   }

   bool
   print_hdc1_translated_send()
   {
      assert(gen_inst_is_send(inst));
      assert(!inst->send.desc_is_reg);
      assert(inst->send.sfid == GEN_SFID_HDC1);

      const gen_hdc_desc hdc = gen_hdc_desc_decode(devinfo, inst->send.desc_imm);
      const unsigned bti      = hdc.bti;
      const unsigned msg_ctrl = hdc.msg_ctrl;
      const unsigned msg_type = hdc.msg_type;

      if (!send_ex_desc_is_only_ex_mlen()) {
         unknown();
         return true;
      }

      bool is_a64 = false;
      switch (msg_type) {
      case GEN_DATAPORT_DC_PORT1_A64_SCATTERED_READ:
      case GEN_DATAPORT_DC_PORT1_A64_UNTYPED_SURFACE_READ:
      case GEN_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_OP:
      case GEN_GFX12_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_HALF_INT_OP:
      case GEN_DATAPORT_DC_PORT1_A64_OWORD_BLOCK_READ:
      case GEN_DATAPORT_DC_PORT1_A64_OWORD_BLOCK_WRITE:
      case GEN_DATAPORT_DC_PORT1_A64_UNTYPED_SURFACE_WRITE:
      case GEN_DATAPORT_DC_PORT1_A64_SCATTERED_WRITE:
      case GEN_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_FLOAT_OP:
      case GEN_GFX12_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_HALF_FLOAT_OP:
         is_a64 = true;
         break;
      }

      switch (msg_type) {
      case GEN_DATAPORT_DC_PORT1_UNTYPED_SURFACE_READ:
      case GEN_DATAPORT_DC_PORT1_TYPED_SURFACE_READ:
      case GEN_DATAPORT_DC_PORT1_UNTYPED_SURFACE_WRITE:
      case GEN_DATAPORT_DC_PORT1_TYPED_SURFACE_WRITE:
      case GEN_DATAPORT_DC_PORT1_A64_UNTYPED_SURFACE_READ:
      case GEN_DATAPORT_DC_PORT1_A64_UNTYPED_SURFACE_WRITE:
      {
         const char *op = NULL;
         switch (msg_type) {
         case GEN_DATAPORT_DC_PORT1_UNTYPED_SURFACE_READ:      op = "untyped_read"; break;
         case GEN_DATAPORT_DC_PORT1_TYPED_SURFACE_READ:        op = "typed_read"; break;
         case GEN_DATAPORT_DC_PORT1_UNTYPED_SURFACE_WRITE:     op = "untyped_write"; break;
         case GEN_DATAPORT_DC_PORT1_TYPED_SURFACE_WRITE:       op = "typed_write"; break;
         case GEN_DATAPORT_DC_PORT1_A64_UNTYPED_SURFACE_READ:  op = "a64_untyped_read"; break;
         case GEN_DATAPORT_DC_PORT1_A64_UNTYPED_SURFACE_WRITE: op = "a64_untyped_write"; break;
         }
         const unsigned cmask_enable = (~msg_ctrl) & 0xfu;
         const char *cmask_name = lsc_cmask_name((enum lsc_cmask)cmask_enable);
         format("%s:%s", op, cmask_name ? cmask_name : "?");
         print_hdc1_surface_tail(msg_ctrl);
         break;
      }

      case GEN_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_OP:
      case GEN_DATAPORT_DC_PORT1_TYPED_ATOMIC_OP:
      case GEN_DATAPORT_DC_PORT1_ATOMIC_COUNTER_OP:
      case GEN_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_OP:
      case GEN_GFX12_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_HALF_INT_OP:
      {
         const char *aop_name = hdc1_aop_to_string(msg_ctrl & 0xfu);
         if (!aop_name) {
            unknown();
            return true;
         }

         const char *prefix = NULL;
         switch (msg_type) {
         case GEN_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_OP: prefix = "untyped_atomic"; break;
         case GEN_DATAPORT_DC_PORT1_TYPED_ATOMIC_OP: prefix = "typed_atomic"; break;
         case GEN_DATAPORT_DC_PORT1_ATOMIC_COUNTER_OP: prefix = "atomic_counter"; break;
         case GEN_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_OP: prefix = "a64_untyped_atomic"; break;
         case GEN_GFX12_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_HALF_INT_OP: prefix = "a64_untyped_atomic_half"; break;
         }
         format("%s_%s", prefix, aop_name);
         print_hdc1_atomic_tail(msg_ctrl);
         break;
      }

      case GEN_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_FLOAT_OP:
      case GEN_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_FLOAT_OP:
      case GEN_GFX12_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_HALF_FLOAT_OP:
      {
         const char *aop_name = hdc1_aop_float_to_string(msg_ctrl & 0xfu);
         if (!aop_name) {
            unknown();
            return true;
         }

         const char *prefix = NULL;
         switch (msg_type) {
         case GEN_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_FLOAT_OP: prefix = "untyped_atomic"; break;
         case GEN_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_FLOAT_OP: prefix = "a64_untyped_atomic"; break;
         case GEN_GFX12_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_HALF_FLOAT_OP: prefix = "a64_untyped_atomic_half"; break;
         }
         format("%s_%s", prefix, aop_name);
         print_hdc1_atomic_tail(msg_ctrl);
         break;
      }

      case GEN_DATAPORT_DC_PORT1_A64_OWORD_BLOCK_READ:
      case GEN_DATAPORT_DC_PORT1_A64_OWORD_BLOCK_WRITE:
      {
         const char *op = msg_type == GEN_DATAPORT_DC_PORT1_A64_OWORD_BLOCK_READ
            ? "a64_oword_block_read" : "a64_oword_block_write";
         const char *owords_name = hdc1_owords_to_string(msg_ctrl & 0x7u);
         format("%s:%s", op, owords_name ? owords_name : "?");

         const unsigned aligned = (msg_ctrl >> 3) & 0x3u;
         if (aligned != 0)
            format(" aligned=%u", aligned);
         break;
      }

      default:
         unknown();
         return true;
      }

      print_translation_exec_size();
      if (is_a64)
         text(" flat+0x0");
      else
         print_translation_bti(bti);

      return true;
   }

   bool
   print_hdc0_translated_send()
   {
      assert(gen_inst_is_send(inst));
      assert(!inst->send.desc_is_reg);
      assert(inst->send.sfid == GEN_SFID_HDC0);

      const gen_hdc_desc hdc = gen_hdc_desc_decode(devinfo, inst->send.desc_imm);
      const unsigned bti      = hdc.bti;
      const unsigned msg_ctrl = hdc.msg_ctrl;
      const unsigned msg_type = hdc.msg_type;

      if (!send_ex_desc_is_only_ex_mlen()) {
         unknown();
         return true;
      }

      switch (msg_type) {
      case GEN_DATAPORT_DC_OWORD_BLOCK_READ:
      case GEN_DATAPORT_DC_UNALIGNED_OWORD_BLOCK_READ:
      case GEN_DATAPORT_DC_OWORD_BLOCK_WRITE:
      {
         const char *op = msg_type == GEN_DATAPORT_DC_OWORD_BLOCK_READ ? "oword_block_read" :
                          msg_type == GEN_DATAPORT_DC_UNALIGNED_OWORD_BLOCK_READ
                             ? "oword_unaligned_block_read" :
                               "oword_block_write";
         const char *owords_name = hdc1_owords_to_string(msg_ctrl & 0x7u);
         format("%s:%s", op, owords_name ? owords_name : "?");
         break;
      }

      case GEN_DATAPORT_DC_UNTYPED_SURFACE_READ:
      case GEN_DATAPORT_DC_UNTYPED_SURFACE_WRITE:
      {
         const unsigned cmask_enable = (~msg_ctrl) & 0xfu;
         const char *cmask_name = lsc_cmask_name((enum lsc_cmask)cmask_enable);
         format("%s:%s",
                msg_type == GEN_DATAPORT_DC_UNTYPED_SURFACE_READ
                   ? "untyped_read" : "untyped_write",
                cmask_name ? cmask_name : "?");
         print_hdc1_surface_tail(msg_ctrl);
         break;
      }

      case GEN_DATAPORT_DC_DWORD_SCATTERED_READ:
      case GEN_DATAPORT_DC_DWORD_SCATTERED_WRITE:
         format("%s simd%u",
                msg_type == GEN_DATAPORT_DC_DWORD_SCATTERED_READ
                   ? "dword_scattered_read" : "dword_scattered_write",
                (msg_ctrl & 0x1u) ? 16u : 8u);
         break;

      case GEN_DATAPORT_DC_BYTE_SCATTERED_READ:
      case GEN_DATAPORT_DC_BYTE_SCATTERED_WRITE:
      {
         const char *ds_name = NULL;
         switch ((msg_ctrl >> 2) & 0x3u) {
         case 0: ds_name = "d8"; break;
         case 1: ds_name = "d16"; break;
         case 2: ds_name = "d32"; break;
         }
         format("%s:%s simd%u",
                msg_type == GEN_DATAPORT_DC_BYTE_SCATTERED_READ
                   ? "byte_scattered_read" : "byte_scattered_write",
                ds_name ? ds_name : "?",
                (msg_ctrl & 0x1u) ? 16u : 8u);
         break;
      }

      case GEN_DATAPORT_DC_UNTYPED_ATOMIC_OP:
      {
         const char *aop_name = hdc1_aop_to_string(msg_ctrl & 0xfu);
         if (!aop_name) {
            unknown();
            return true;
         }
         format("untyped_atomic_%s", aop_name);
         print_hdc1_atomic_tail(msg_ctrl);
         break;
      }

      case GEN_DATAPORT_DC_MEMORY_FENCE:
         text("memory_fence");
         break;

      default:
         unknown();
         return true;
      }

      print_translation_exec_size();
      if (msg_type != GEN_DATAPORT_DC_MEMORY_FENCE)
         print_translation_bti(bti);

      return true;
   }

   bool
   print_hdc_ro_translated_send()
   {
      assert(gen_inst_is_send(inst));
      assert(!inst->send.desc_is_reg);
      assert(inst->send.sfid == GEN_SFID_HDC_READ_ONLY);

      if (!send_ex_desc_is_only_ex_mlen()) {
         unknown();
         return true;
      }

      const gen_hdc_desc hdc = gen_hdc_desc_decode(devinfo, inst->send.desc_imm);
      const unsigned bti      = hdc.bti;
      const unsigned msg_ctrl = hdc.msg_ctrl;
      const unsigned msg_type = hdc.msg_type;

      const char *op = NULL;
      switch (msg_type) {
      case GEN_DATAPORT_DC_OWORD_BLOCK_READ:           op = "oword_block_read"; break;
      case GEN_DATAPORT_DC_UNALIGNED_OWORD_BLOCK_READ: op = "oword_unaligned_block_read"; break;
      default:
         unknown();
         return true;
      }

      const char *owords_name = hdc1_owords_to_string(msg_ctrl & 0x7u);
      format("%s:%s", op, owords_name ? owords_name : "?");
      print_translation_exec_size();
      print_translation_bti(bti);
      return true;
   }

   bool
   print_render_translated_send()
   {
      assert(gen_inst_is_send(inst));
      assert(!inst->send.desc_is_reg);
      assert(inst->send.sfid == GEN_SFID_RENDER_CACHE);

      const gen_render_desc rc =
         gen_render_desc_decode(devinfo, inst->send.desc_imm);
      const unsigned bti      = rc.bti;
      const unsigned msg_type = rc.msg_type;

      if (!send_ex_desc_is_only_ex_mlen()) {
         unknown();
         return true;
      }

      switch (msg_type) {
      case GEN_DATAPORT_WRITE_MESSAGE_RENDER_TARGET_WRITE:
      {
         const unsigned subtype  = rc.msg_ctrl & 0x7u;
         const bool hi           = (rc.msg_ctrl >> 3) & 0x1u;
         const bool last_rt      = (rc.msg_ctrl >> 4) & 0x1u;
         const bool coarse_write = devinfo->ver >= 10 && rc.coarse_write;

         const char *subtype_name = rt_write_subtype_to_string(devinfo, subtype);
         if (subtype_name)
            format("%s rt_write", subtype_name);
         else
            text("rt_write");

         if (hi)
            text(" hi");
         if (last_rt)
            text(" last_rt");
         if (coarse_write)
            text(" coarse_write");
         break;
      }

      case GEN_DATAPORT_RC_RENDER_TARGET_READ:
      {
         const bool simd8      = rc.msg_ctrl & 0x1u;
         const bool per_sample = (rc.msg_ctrl >> 5) & 0x1u;
         format("simd%u rt_read", simd8 ? 8u : 16u);
         if (per_sample)
            text(" per_sample");
         break;
      }

      default:
         unknown();
         return true;
      }

      print_translation_exec_size();
      print_translation_bti(bti);
      return true;
   }

   bool
   print_pi_translated_send()
   {
      assert(gen_inst_is_send(inst));
      assert(!inst->send.desc_is_reg);
      assert(inst->send.sfid == GEN_SFID_PIXEL_INTERPOLATOR);

      if (!send_ex_desc_is_only_ex_mlen()) {
         unknown();
         return true;
      }

      const uint32_t desc = inst->send.desc_imm;
      const unsigned msg_data = desc & 0xffu;
      const unsigned msg_type = (desc >> 12) & 0x3u;
      const bool linear = (desc >> 14) & 0x1u;

      const char *op_name = NULL;
      switch (msg_type) {
      case GEN_PIXEL_INTERPOLATOR_LOC_SHARED_OFFSET:   op_name = "per_message_offset"; break;
      case GEN_PIXEL_INTERPOLATOR_LOC_SAMPLE:          op_name = "sample_position"; break;
      case GEN_PIXEL_INTERPOLATOR_LOC_CENTROID:        op_name = "centroid"; break;
      case GEN_PIXEL_INTERPOLATOR_LOC_PER_SLOT_OFFSET: op_name = "per_slot_offset"; break;
      }
      if (!op_name) {
         unknown();
         return true;
      }

      format("%s %s", op_name, linear ? "linear" : "persp");
      if (msg_data != 0)
         format(" data=0x%02x", msg_data);
      print_translation_exec_size();

      return true;
   }

   bool
   print_gateway_translated_send()
   {
      assert(gen_inst_is_send(inst));
      assert(!inst->send.desc_is_reg);
      assert(inst->send.sfid == GEN_SFID_MESSAGE_GATEWAY);

      if (!send_ex_desc_is_only_ex_mlen()) {
         unknown();
         return true;
      }

      if (inst->send.desc_imm & 0x7fff8u) {
         unknown();
         return true;
      }

      const unsigned subfunc = inst->send.desc_imm & 0x7u;
      const char *op_name = NULL;
      switch (subfunc) {
      case GEN_MESSAGE_GATEWAY_SFID_OPEN_GATEWAY:         op_name = "open"; break;
      case GEN_MESSAGE_GATEWAY_SFID_CLOSE_GATEWAY:        op_name = "close"; break;
      case GEN_MESSAGE_GATEWAY_SFID_FORWARD_MSG:          op_name = "forward_msg"; break;
      case GEN_MESSAGE_GATEWAY_SFID_GET_TIMESTAMP:        op_name = "get_timestamp"; break;
      case GEN_MESSAGE_GATEWAY_SFID_BARRIER_MSG:          op_name = "barrier_msg"; break;
      case GEN_MESSAGE_GATEWAY_SFID_UPDATE_GATEWAY_STATE: op_name = "update_state"; break;
      case GEN_MESSAGE_GATEWAY_SFID_MMIO_READ_WRITE:      op_name = "mmio_rw"; break;
      }
      if (!op_name) {
         unknown();
         return true;
      }

      text(op_name);
      print_translation_exec_size();
      return true;
   }

   bool
   print_rtaccel_translated_send()
   {
      assert(gen_inst_is_send(inst));
      assert(!inst->send.desc_is_reg);
      assert(inst->send.sfid == GEN_SFID_RAY_TRACE_ACCELERATOR);

      if (!devinfo->has_ray_tracing)
         return false;

      if (!send_ex_desc_is_only_ex_mlen()) {
         unknown();
         return true;
      }

      const bool simd16 = (inst->send.desc_imm >> 8) & 0x1u;
      format("trace_ray simd%u", simd16 ? 16u : 8u);
      print_translation_exec_size();
      return true;
   }

   bool
   print_btd_translated_send()
   {
      assert(gen_inst_is_send(inst));
      assert(!inst->send.desc_is_reg);
      assert(inst->send.sfid == GEN_SFID_BINDLESS_THREAD_DISPATCH);

      if (devinfo->verx10 < 125 || !devinfo->has_ray_tracing)
         return false;

      if (!send_ex_desc_is_only_ex_mlen()) {
         unknown();
         return true;
      }

      const uint32_t desc = inst->send.desc_imm;
      const unsigned msg_type = (desc >> 14) & 0xfu;
      const bool simd16 = (desc >> 8) & 0x1u;

      const char *op_name = NULL;
      switch (msg_type) {
      case GEN_RT_BTD_MESSAGE_SPAWN: op_name = "spawn"; break;
      }
      if (!op_name) {
         unknown();
         return true;
      }

      format("%s simd%u", op_name, simd16 ? 16u : 8u);
      print_translation_exec_size();
      return true;
   }

   bool
   translated_send_may_apply() const
   {
      if (!gen_inst_is_send(inst) || inst->send.desc_is_reg)
         return false;

      if (is_lsc_translated_sfid(inst->send.sfid))
         return true;

      switch (inst->send.sfid) {
      case GEN_SFID_SAMPLER:
      case GEN_SFID_MESSAGE_GATEWAY:
      case GEN_SFID_RENDER_CACHE:
      case GEN_SFID_PIXEL_INTERPOLATOR:
      case GEN_SFID_HDC0:
      case GEN_SFID_HDC1:
      case GEN_SFID_HDC_READ_ONLY:
         return true;
      case GEN_SFID_URB:
         return devinfo->ver < 20;
      case GEN_SFID_RAY_TRACE_ACCELERATOR:
         return devinfo->has_ray_tracing;
      case GEN_SFID_BINDLESS_THREAD_DISPATCH:
         return devinfo->verx10 >= 125 && devinfo->has_ray_tracing;
      default:
         return false;
      }
   }

   bool
   print_translated_send()
   {
      if (!translated_send_may_apply())
         return false;

      if (is_lsc_translated_sfid(inst->send.sfid))
         return print_lsc_translated_send();

      switch (inst->send.sfid) {
      case GEN_SFID_SAMPLER:
         return print_sampler_translated_send();
      case GEN_SFID_URB:
         return print_urb_translated_send();
      case GEN_SFID_MESSAGE_GATEWAY:
         return print_gateway_translated_send();
      case GEN_SFID_RENDER_CACHE:
         return print_render_translated_send();
      case GEN_SFID_RAY_TRACE_ACCELERATOR:
         return print_rtaccel_translated_send();
      case GEN_SFID_BINDLESS_THREAD_DISPATCH:
         return print_btd_translated_send();
      case GEN_SFID_PIXEL_INTERPOLATOR:
         return print_pi_translated_send();
      case GEN_SFID_HDC0:
         return print_hdc0_translated_send();
      case GEN_SFID_HDC1:
         return print_hdc1_translated_send();
      case GEN_SFID_HDC_READ_ONLY:
         return print_hdc_ro_translated_send();
      default:
         return false;
      }
   }
};

const char *
gen_opcode_to_string(gen_opcode op)
{
   switch (op) {
   case GEN_OP_ILLEGAL:  return "illegal";
   case GEN_OP_ADD3:     return "add3";
   case GEN_OP_ADD:      return "add";
   case GEN_OP_ADDC:     return "addc";
   case GEN_OP_AND:      return "and";
   case GEN_OP_ASR:      return "asr";
   case GEN_OP_AVG:      return "avg";
   case GEN_OP_BFE:      return "bfe";
   case GEN_OP_BFI1:     return "bfi1";
   case GEN_OP_BFI2:     return "bfi2";
   case GEN_OP_BFN:      return "bfn";
   case GEN_OP_BFREV:    return "bfrev";
   case GEN_OP_BRC:      return "brc";
   case GEN_OP_BRD:      return "brd";
   case GEN_OP_BREAK:    return "break";
   case GEN_OP_CALL:     return "call";
   case GEN_OP_CALLA:    return "calla";
   case GEN_OP_CBIT:     return "cbit";
   case GEN_OP_CMP:      return "cmp";
   case GEN_OP_CMPN:     return "cmpn";
   case GEN_OP_CONTINUE: return "continue";
   case GEN_OP_CSEL:     return "csel";
   case GEN_OP_DP2:      return "dp2";
   case GEN_OP_DP3:      return "dp3";
   case GEN_OP_DP4:      return "dp4";
   case GEN_OP_DP4A:     return "dp4a";
   case GEN_OP_DPAS:     return "dpas";
   case GEN_OP_DPH:      return "dph";
   case GEN_OP_ELSE:     return "else";
   case GEN_OP_ENDIF:    return "endif";
   case GEN_OP_FBH:      return "fbh";
   case GEN_OP_FBL:      return "fbl";
   case GEN_OP_FRC:      return "frc";
   case GEN_OP_GOTO:     return "goto";
   case GEN_OP_HALT:     return "halt";
   case GEN_OP_IF:       return "if";
   case GEN_OP_JMPI:     return "jmpi";
   case GEN_OP_JOIN:     return "join";
   case GEN_OP_LINE:     return "line";
   case GEN_OP_LRP:      return "lrp";
   case GEN_OP_LZD:      return "lzd";
   case GEN_OP_MAC:      return "mac";
   case GEN_OP_MACH:     return "mach";
   case GEN_OP_MACL:     return "macl";
   case GEN_OP_MAD:      return "mad";
   case GEN_OP_MADM:     return "madm";
   case GEN_OP_MATH:     return "math";
   case GEN_OP_MOV:      return "mov";
   case GEN_OP_MOVI:     return "movi";
   case GEN_OP_MUL:      return "mul";
   case GEN_OP_NOP:      return "nop";
   case GEN_OP_NOT:      return "not";
   case GEN_OP_OR:       return "or";
   case GEN_OP_PLN:      return "pln";
   case GEN_OP_RET:      return "ret";
   case GEN_OP_RNDD:     return "rndd";
   case GEN_OP_RNDE:     return "rnde";
   case GEN_OP_RNDU:     return "rndu";
   case GEN_OP_RNDZ:     return "rndz";
   case GEN_OP_ROL:      return "rol";
   case GEN_OP_ROR:      return "ror";
   case GEN_OP_SEL:      return "sel";
   case GEN_OP_SEND:     return "send";
   case GEN_OP_SENDC:    return "sendc";
   case GEN_OP_SENDS:    return "sends";
   case GEN_OP_SENDSC:   return "sendsc";
   case GEN_OP_SHL:      return "shl";
   case GEN_OP_SHR:      return "shr";
   case GEN_OP_SMOV:     return "smov";
   case GEN_OP_SRND:     return "srnd";
   case GEN_OP_SUBB:     return "subb";
   case GEN_OP_SYNC:     return "sync";
   case GEN_OP_WAIT:     return "wait";
   case GEN_OP_WHILE:    return "while";
   case GEN_OP_XOR:      return "xor";
   default:
      UNREACHABLE("invalid gen opcode");
   }
}

bool
gen_print(gen_print_params *params)
{
   assert(params);
   assert(params->devinfo);
   assert(params->insts || params->raw_bytes);
   assert(params->errors || params->num_errors == 0);
   assert(params->raw_bytes || params->raw_bytes_size == 0);
   assert(params->raw_bytes_size >= 0);
   assert(params->raw_bytes == NULL || params->raw_bytes_size % 8 == 0);

   if (!params->fp)
      params->fp = stderr;

   gen_print_params effective = *params;
   void *mem_ctx = NULL;

   if (!effective.insts || (!effective.errors && effective.validate))
      mem_ctx = ralloc_context(NULL);

   if (!effective.insts) {
      gen_decode_params decode = {};
      decode.devinfo = effective.devinfo;
      decode.raw_bytes = effective.raw_bytes;
      decode.raw_bytes_size = effective.raw_bytes_size;
      decode.program_subset = effective.program_subset;
      decode.mem_ctx = mem_ctx;

      if (!gen_decode(&decode)) {
         ralloc_free(mem_ctx);
         return false;
      }

      effective.insts = decode.insts;
      effective.num_insts = decode.num_insts;
   }

   if (!effective.errors && effective.validate) {
      gen_validate_params validate = {};
      validate.devinfo = effective.devinfo;
      validate.insts = effective.insts;
      validate.num_insts = effective.num_insts;
      validate.mem_ctx = mem_ctx;
      gen_validate(&validate);
      effective.errors = validate.errors;
      effective.num_errors = validate.num_errors;
   }

   gen_printer p(&effective);
   p.print_program();
   ralloc_free(mem_ctx);
   return true;
}

void
gen_print_swsb(const intel_device_info *devinfo, FILE *fp, gen_swsb swsb)
{
   assert(fp);

   emit_swsb(devinfo, fp, swsb, " ");
}

void
gen_print_inst(const intel_device_info *devinfo,
               FILE *fp,
               const gen_inst *inst,
               gen_print_flags flags)
{
   assert(devinfo);
   assert(inst);

   if (!fp)
      fp = stderr;

   gen_print_params params = {};
   params.devinfo = devinfo;
   params.fp = fp;
   params.flags = flags;

   gen_printer p(&params);
   p.print_inst(inst, 0);
}
