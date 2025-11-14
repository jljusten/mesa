/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <inttypes.h>
#include <map>
#include <string>
#include <vector>

#include "util/half_float.h"

#include "gen_private.h"

static const char *const writemask[16] = {
   ".",
   ".x",
   ".y",
   ".xy",
   ".z",
   ".xz",
   ".yz",
   ".xyz",
   ".w",
   ".xw",
   ".yw",
   ".xyw",
   ".zw",
   ".xzw",
   ".yzw",
   "",
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
   case GEN_CONDITION_Z:    return "eq";
   case GEN_CONDITION_NZ:   return "ne";
   case GEN_CONDITION_G:    return "gt";
   case GEN_CONDITION_GE:   return "ge";
   case GEN_CONDITION_L:    return "lt";
   case GEN_CONDITION_LE:   return "le";
   case GEN_CONDITION_O:    return "o";
   case GEN_CONDITION_U:    return "u";
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
gen_sfid_to_string(gen_sfid sfid)
{
   switch (sfid) {
   case GEN_SFID_NULL:                  return "null";
   case GEN_SFID_SAMPLER:               return "sampler";
   case GEN_SFID_MESSAGE_GATEWAY:       return "gtwy";
   case GEN_SFID_HDC2:                  return "hdc2";
   case GEN_SFID_RENDER_CACHE:          return "render";
   case GEN_SFID_URB:                   return "urb";
   case GEN_SFID_THREAD_SPAWNER:        return "ts/btd";
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
   switch (cmask) {
   case LSC_CMASK_X:    return "x";
   case LSC_CMASK_Y:    return "y";
   case LSC_CMASK_XY:   return "xy";
   case LSC_CMASK_Z:    return "z";
   case LSC_CMASK_XZ:   return "xz";
   case LSC_CMASK_YZ:   return "yz";
   case LSC_CMASK_XYZ:  return "xyz";
   case LSC_CMASK_W:    return "w";
   case LSC_CMASK_XW:   return "xw";
   case LSC_CMASK_YW:   return "yw";
   case LSC_CMASK_XYW:  return "xyw";
   case LSC_CMASK_ZW:   return "zw";
   case LSC_CMASK_XZW:  return "xzw";
   case LSC_CMASK_YZW:  return "yzw";
   case LSC_CMASK_XYZW: return "xyzw";
   default:             return NULL;
   }
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

static std::string
lsc_cache_ctrl_name(const intel_device_info *devinfo,
                    enum lsc_opcode op, unsigned cache_ctrl)
{
   if (cache_ctrl == 0)
      return "";

   if (lsc_opcode_uses_load_cache(op)) {
      if (devinfo->ver >= 20) {
         switch ((enum xe2_lsc_cache_load)cache_ctrl) {
         case XE2_LSC_CACHE_LOAD_L1UC_L3UC:   return "uc.uc";
         case XE2_LSC_CACHE_LOAD_L1UC_L3C:    return "uc.ca";
         case XE2_LSC_CACHE_LOAD_L1UC_L3CC:   return "uc.cc";
         case XE2_LSC_CACHE_LOAD_L1C_L3UC:    return "ca.uc";
         case XE2_LSC_CACHE_LOAD_L1C_L3C:     return "ca.ca";
         case XE2_LSC_CACHE_LOAD_L1C_L3CC:    return "ca.cc";
         case XE2_LSC_CACHE_LOAD_L1S_L3UC:    return "st.uc";
         case XE2_LSC_CACHE_LOAD_L1S_L3C:     return "st.ca";
         case XE2_LSC_CACHE_LOAD_L1IAR_L3IAR: return "ri.ri";
         default:
            return "";
         }
      }

      switch ((enum lsc_cache_load)cache_ctrl) {
      case LSC_CACHE_LOAD_L1UC_L3UC: return "uc.uc";
      case LSC_CACHE_LOAD_L1UC_L3C:  return "uc.ca";
      case LSC_CACHE_LOAD_L1C_L3UC:  return "ca.uc";
      case LSC_CACHE_LOAD_L1C_L3C:   return "ca.ca";
      case LSC_CACHE_LOAD_L1S_L3UC:  return "st.uc";
      case LSC_CACHE_LOAD_L1S_L3C:   return "st.ca";
      case LSC_CACHE_LOAD_L1IAR_L3C: return "ri.ca";
      default:
         return "";
      }
   }

   if (devinfo->ver >= 20) {
      switch ((enum xe2_lsc_cache_store)cache_ctrl) {
      case XE2_LSC_CACHE_STORE_L1UC_L3UC: return "uc.uc";
      case XE2_LSC_CACHE_STORE_L1UC_L3WB: return "uc.wb";
      case XE2_LSC_CACHE_STORE_L1WT_L3UC: return "wt.uc";
      case XE2_LSC_CACHE_STORE_L1WT_L3WB: return "wt.wb";
      case XE2_LSC_CACHE_STORE_L1S_L3UC:  return "st.uc";
      case XE2_LSC_CACHE_STORE_L1S_L3WB:  return "st.wb";
      case XE2_LSC_CACHE_STORE_L1WB_L3WB: return "wb.wb";
      default:
         return "";
      }
   }

   switch ((enum lsc_cache_store)cache_ctrl) {
   case LSC_CACHE_STORE_L1UC_L3UC: return "uc.uc";
   case LSC_CACHE_STORE_L1UC_L3WB: return "uc.wb";
   case LSC_CACHE_STORE_L1WT_L3UC: return "wt.uc";
   case LSC_CACHE_STORE_L1WT_L3WB: return "wt.wb";
   case LSC_CACHE_STORE_L1S_L3UC:  return "st.uc";
   case LSC_CACHE_STORE_L1S_L3WB:  return "st.wb";
   case LSC_CACHE_STORE_L1WB_L3WB: return "wb.wb";
   default:
      return "";
   }
}

static bool
lsc_send_has_symbolic_src1(enum lsc_opcode op)
{
   return lsc_opcode_is_store(op) ||
          (lsc_opcode_is_atomic(op) && lsc_op_num_data_values(op) > 0);
}

static inline uint32_t
gen_message_ex_desc(const struct intel_device_info *devinfo,
                    unsigned ex_msg_length)
{
   return devinfo->ver >= 20 ?
      SET_BITS(ex_msg_length, 10, 6) :
      SET_BITS(ex_msg_length, 9, 6);
}

static uint32_t
lsc_ex_desc_surface_bits(const intel_device_info *devinfo, const gen_inst *inst)
{
   if (inst->send.ex_desc_is_reg)
      return 0;

   const int src1_len = gen_inst_send_src1_len(devinfo, inst);
   const uint32_t ex_mlen = src1_len > 0 ? gen_message_ex_desc(devinfo, src1_len) : 0;
   return inst->send.ex_desc_imm & ~ex_mlen;
}

static bool
lsc_symbolic_surface_name(const intel_device_info *devinfo,
                          const gen_inst *inst,
                          enum lsc_addr_surface_type addr_type,
                          std::string &surface)
{
   surface.clear();

   if (inst->send.ex_desc_imm_extra)
      return false;

   const uint32_t surface_bits = lsc_ex_desc_surface_bits(devinfo, inst);
   const gen_lsc_ex_desc ex_desc =
      gen_lsc_ex_desc_decode(devinfo, addr_type, surface_bits);

   switch (inst->send.sfid) {
   case GEN_SFID_SLM:
      return addr_type == LSC_ADDR_SURFTYPE_FLAT &&
             !inst->send.ex_desc_is_reg && ex_desc.flat.base_offset == 0;

   case GEN_SFID_UGM:
   case GEN_SFID_TGM:
      switch (addr_type) {
      case LSC_ADDR_SURFTYPE_FLAT:
         return !inst->send.ex_desc_is_reg && ex_desc.flat.base_offset == 0;

      case LSC_ADDR_SURFTYPE_BSS:
      case LSC_ADDR_SURFTYPE_SS:
         if (!inst->send.ex_desc_is_reg)
            return false;
         surface = addr_type == LSC_ADDR_SURFTYPE_BSS ? "bss[a0." : "ss[a0.";
         surface += std::to_string(inst->send.ex_desc_subnr / 2);
         surface += "]";
         return true;

      case LSC_ADDR_SURFTYPE_BTI:
         if (inst->send.ex_desc_is_reg || ex_desc.bti.base_offset != 0)
            return false;
         surface = "bti[" + std::to_string(ex_desc.bti.index) + "]";
         return true;

      default:
         return false;
      }

   default:
      return false;
   }
}

struct lsc_symbolic_info {
   std::string mnemonic;
   bool has_src1 = false;
};

static bool
lsc_symbolic_info_from_inst(const intel_device_info *devinfo,
                            const gen_inst *inst,
                            lsc_symbolic_info &info)
{
   info = {};

   if (!gen_inst_is_send(inst) || !devinfo->has_lsc || inst->send.desc_is_reg)
      return false;

   switch (inst->send.sfid) {
   case GEN_SFID_SLM:
   case GEN_SFID_TGM:
   case GEN_SFID_UGM:
      /* The SEND is for LSC unit, keep going. */
      break;
   default:
      return false;
   }

   const char *sfid_name = gen_sfid_to_string(inst->send.sfid);
   const gen_lsc_desc desc = gen_lsc_desc_decode(devinfo, inst->send.desc_imm);

   const enum lsc_opcode op = desc.op;
   const char *op_name = lsc_opcode_name(op);
   if (!op_name)
      return false;

   if (op == LSC_OP_FENCE) {
      if (desc.addr_type != LSC_ADDR_SURFTYPE_FLAT)
         return false;
      if (inst->send.ex_desc_is_reg || lsc_ex_desc_surface_bits(devinfo, inst) != 0 ||
          inst->send.ex_desc_imm_extra)
         return false;

      const char *scope_name = lsc_fence_scope_name(desc.fence.scope);
      const char *flush_name = lsc_flush_type_name(desc.fence.flush_type);
      if (!scope_name || !flush_name)
         return false;

      info.mnemonic = std::string(op_name) + "." + sfid_name + "." +
                      scope_name + "." + flush_name;
      if (desc.fence.route_to_lsc)
         info.mnemonic += ".route_to_lsc";
      return true;
   }

   const char *data_name = lsc_data_size_name(desc.data_size);
   if (!data_name)
      return false;

   info.mnemonic = std::string(op_name) + "." + sfid_name + "." + data_name;

   if (lsc_opcode_has_cmask(op)) {
      const char *cmask_name = lsc_cmask_name(desc.cmask);
      if (!cmask_name)
         return false;
      info.mnemonic += ".";
      info.mnemonic += cmask_name;
   } else if (!lsc_opcode_is_atomic(op)) {
      const unsigned num_values = lsc_vector_length_local(desc.vect_size);
      const bool transpose = desc.transpose;
      if (num_values == 0)
         return false;
      if (num_values != 1 || transpose) {
         info.mnemonic += "x";
         info.mnemonic += std::to_string(num_values);
         if (transpose)
            info.mnemonic += "t";
      }
   }

   const char *addr_name = lsc_addr_size_name(desc.addr_size);
   if (!addr_name)
      return false;
   info.mnemonic += ".";
   info.mnemonic += addr_name;

   const unsigned cache_ctrl = desc.cache_ctrl;
   const std::string cache = lsc_cache_ctrl_name(devinfo, op, cache_ctrl);
   if (cache_ctrl != 0 && cache.empty())
      return false;
   if (!cache.empty()) {
      info.mnemonic += ".";
      info.mnemonic += cache;
   }

   std::string surface;
   if (!lsc_symbolic_surface_name(devinfo, inst, desc.addr_type, surface))
      return false;
   if (!surface.empty()) {
      info.mnemonic += ".";
      info.mnemonic += surface;
   }

   info.has_src1 = lsc_send_has_symbolic_src1(op);
   return true;
}

struct gen_printer {
   enum {
      OPCODE_COLUMN = 8,
      FIRST_FIELD_COLUMN = 24,
      FIELD_STRIDE = 16,
      RAW_SEND_DST_COLUMN = 33,
      RAW_SEND_SRC0_COLUMN = 42,
      RAW_SEND_SRC1_COLUMN = 49,
      RAW_SEND_EX_DESC_COLUMN = 57,
      RAW_SEND_DESC_COLUMN = 69,
      RAW_SEND_ANNOTATION_COLUMN = 87,
   };

   const intel_device_info *devinfo;
   FILE *fp;
   gen_print_flags flags;
   const bool *was_compacted;
   const std::map<int, std::string> &labels;
   unsigned column;

   gen_printer(const intel_device_info *devinfo,
               const gen_print_params *params,
               const std::map<int, std::string> &labels)
      : devinfo(devinfo), fp(params->fp), flags(params->flags),
        was_compacted(params->was_compacted), labels(labels), column(0) {}

   void
   print(const gen_inst *inst, int idx)
   {
      print_prefix(inst);
      if (aligned_columns())
         pad(OPCODE_COLUMN);
      print_opcode(inst);

      if (inst->exec_size) {
         text(" ");
         print_exec_control(inst);
      }

      unsigned field_col = FIRST_FIELD_COLUMN;
      if (has_cond_modifier(inst)) {
         field_sep(field_col);
         print_cond_modifier(inst);
         if (aligned_columns())
            field_col += FIELD_STRIDE;
      }

      field_col = print_operands(inst, idx, field_col);
      if (print_annotation(inst, idx, field_col) && aligned_columns())
         field_col += FIELD_STRIDE;
      print_send_comment(inst, field_col);
      newline();
   }

private:
   bool
   aligned_columns() const
   {
      return !concise();
   }

   bool
   concise() const
   {
      return !(flags & GEN_PRINT_VERBOSE);
   }

   bool
   has_cond_modifier(const gen_inst *inst) const
   {
      return gen_inst_has_cond_modifier(gen_inst_format(inst->opcode), inst) &&
             inst->cmod != GEN_CONDITION_NONE;
   }

   bool
   omit_dst_region(const gen_operand &dst) const
   {
      return concise() && dst.region.hstride == 1;
   }

   bool
   omit_src_region(const gen_operand &src) const
   {
      return concise() &&
             src.region.vstride == 1 &&
             src.region.width == 1 &&
             src.region.hstride == 0;
   }

   bool
   uniform_src_region(const gen_region &region) const
   {
      return concise() &&
             region.vstride == 0 &&
             region.width == 1 &&
             region.hstride == 0;
   }

   bool
   omit_type(gen_reg_type type) const
   {
      return concise() && type == GEN_TYPE_UD;
   }

   bool
   show_operand_subreg() const
   {
      return !concise();
   }

   bool
   symbolic_lsc_source(const gen_inst *inst, lsc_symbolic_info *info = NULL) const
   {
      if (flags & GEN_PRINT_RAW_SENDS)
         return false;

      if (inst->opcode != GEN_OP_SEND)
         return false;

      lsc_symbolic_info tmp;
      if (!lsc_symbolic_info_from_inst(devinfo, inst, tmp))
         return false;

      if (info)
         *info = tmp;
      return true;
   }

   bool
   symbolic_lsc_comment(const gen_inst *inst, lsc_symbolic_info *info = NULL) const
   {
      if (!(flags & GEN_PRINT_RAW_SENDS))
         return false;

      lsc_symbolic_info tmp;
      if (!lsc_symbolic_info_from_inst(devinfo, inst, tmp))
         return false;

      if (info)
         *info = tmp;
      return true;
   }

   void
   print_prefix(const gen_inst *inst)
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

         text("f");
         uint(inst->flag_nr);
         text(".");
         uint(inst->flag_subnr);

         if (devinfo->ver >= 20)
            text(xe2_pred_ctrl[inst->pred_control]);
         else if (inst->align16)
            text(pred_ctrl_align16[inst->pred_control]);
         else
            text(pred_ctrl_align1[inst->pred_control]);
      }

      text(") ");
   }

   void
   print_opcode(const gen_inst *inst)
   {
      lsc_symbolic_info lsc;
      if (symbolic_lsc_source(inst, &lsc)) {
         text(lsc.mnemonic.c_str());
         return;
      }

      text(gen_opcode_to_string(inst->opcode));

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

      case GEN_OP_SEND:
      case GEN_OP_SENDC:
      case GEN_OP_SENDS:
      case GEN_OP_SENDSC: {
         text(".");
         text(gen_sfid_to_string(inst->send.sfid));
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

      if (gen_inst_has_saturate(gen_inst_format(inst->opcode), inst) &&
          inst->saturate)
         text(".sat");
   }

   void
   print_cond_modifier(const gen_inst *inst)
   {
      text("(");
      text(cond_modifier_name(inst->cmod));
      text(")");

      text("f");
      uint(inst->flag_nr);
      text(".");
      uint(inst->flag_subnr);
   }

   void
   print_exec_control(const gen_inst *inst)
   {
      text("(");
      uint(inst->exec_size);
      if (!concise() || inst->chan_offset != 0) {
         text("|M");
         uint(inst->chan_offset);
      }
      text(")");
   }

   unsigned
   print_operands(const gen_inst *inst, int idx, unsigned field_col)
   {
      switch (gen_inst_format(inst->opcode)) {
      case GEN_FORMAT_BASIC_ONE_SRC:
      case GEN_FORMAT_BASIC_TWO_SRC:
      case GEN_FORMAT_DPAS_THREE_SRC: {
         const unsigned num_sources = gen_inst_num_sources(devinfo, inst);
         const bool has_dst = gen_inst_has_dst(gen_inst_format(inst->opcode),
                                               inst->opcode);

         if (has_dst) {
            field_sep(field_col);
            print_dst(inst->dst, inst->align16);
            if (aligned_columns())
               field_col += FIELD_STRIDE;
         }

         for (unsigned i = 0; i < num_sources; i++) {
            field_sep(field_col);
            print_src(inst->src[i], inst->align16);
            if (aligned_columns())
               field_col += FIELD_STRIDE;
         }
         return aligned_columns() ? field_col : 0;
      }

      case GEN_FORMAT_BASIC_THREE_SRC: {
         const unsigned num_sources = gen_inst_num_sources(devinfo, inst);
         const bool has_dst = gen_inst_has_dst(gen_inst_format(inst->opcode),
                                               inst->opcode);

         if (has_dst) {
            field_sep(field_col);
            print_dst(inst->dst, inst->align16);
            if (aligned_columns())
               field_col += FIELD_STRIDE;
         }

         for (unsigned i = 0; i < num_sources; i++) {
            field_sep(field_col);
            print_src_3src(inst, inst->src[i], i);
            if (aligned_columns())
               field_col += FIELD_STRIDE;
         }
         return aligned_columns() ? field_col : 0;
      }

      case GEN_FORMAT_SEND: {
         lsc_symbolic_info lsc;
         if (symbolic_lsc_source(inst, &lsc))
            return print_symbolic_lsc_send(inst, lsc, field_col);
         else
            return print_send(inst, field_col);
      }

      case GEN_FORMAT_BRANCH:
         if (const unsigned num_sources = gen_inst_num_sources(devinfo, inst)) {
            for (unsigned i = 0; i < num_sources; i++) {
               field_sep(field_col);
               print_src(inst->src[i], inst->align16);
               if (aligned_columns())
                  field_col += FIELD_STRIDE;
            }
            return aligned_columns() ? field_col : 0;
         } else {
            return print_branch_targets(inst, idx, field_col);
         }

      case GEN_FORMAT_ILLEGAL:
      case GEN_FORMAT_NOP:
         return aligned_columns() ? field_col : 0;
      }

      return aligned_columns() ? field_col : 0;
   }

   unsigned
   print_branch_targets(const gen_inst *inst, int idx, unsigned field_col)
   {
      if (inst->branch.jip) {
         field_sep(field_col);
         text("jip:");
         print_branch_target(idx, inst->branch.jip);
         if (aligned_columns())
            field_col += FIELD_STRIDE;
      }

      if (gen_has_uip(inst->opcode) && inst->branch.uip) {
         field_sep(field_col);
         text("uip:");
         print_branch_target(idx, inst->branch.uip);
         if (aligned_columns())
            field_col += FIELD_STRIDE;
      }

      return aligned_columns() ? field_col : 0;
   }

   void
   print_branch_target(int idx, int32_t rel_bytes)
   {
      const int target = branch_target_index(idx, rel_bytes);
      auto it = labels.find(target);
      if (it != labels.end()) {
         text(it->second.c_str());
         return;
      }

      char buf[32];
      snprintf(buf, sizeof(buf), "0x%x", (uint32_t)rel_bytes);
      text(buf);
   }

   unsigned
   print_send(const gen_inst *inst, unsigned field_col)
   {
      if (aligned_columns())
         field_col = RAW_SEND_DST_COLUMN;

      field_sep(field_col);
      print_send_reg_with_length(inst->dst, false, -1);
      if (aligned_columns())
         field_col = RAW_SEND_SRC0_COLUMN;

      field_sep(field_col);
      print_send_reg_with_length(inst->src[0], true,
                                 gen_inst_send_src0_len(inst));
      if (aligned_columns())
         field_col = RAW_SEND_SRC1_COLUMN;

      field_sep(field_col);
      if (gen_inst_is_split_send(devinfo, inst)) {
         if (is_null(inst->src[1]))
            text("null:0");
         else
            print_send_reg_with_length(inst->src[1], true,
                                       gen_inst_send_src1_len(devinfo, inst));
      } else {
         text("null:0");
      }
      if (aligned_columns())
         field_col = RAW_SEND_EX_DESC_COLUMN;

      field_sep(field_col);
      print_send_ex_desc(inst);
      if (aligned_columns())
         field_col = RAW_SEND_DESC_COLUMN;

      field_sep(field_col);
      print_send_desc(inst);

      return aligned_columns() ? RAW_SEND_ANNOTATION_COLUMN : 0;
   }

   unsigned
   print_symbolic_lsc_send(const gen_inst *inst, const lsc_symbolic_info &lsc,
                           unsigned field_col)
   {
      field_sep(field_col);
      print_send_reg(inst->dst, false);
      if (aligned_columns())
         field_col += FIELD_STRIDE;

      field_sep(field_col);
      print_send_reg(inst->src[0], true);
      if (aligned_columns())
         field_col += FIELD_STRIDE;

      if (lsc.has_src1) {
         field_sep(field_col);
         print_send_reg(inst->src[1], true);
         if (aligned_columns())
            field_col += FIELD_STRIDE;
      }

      return aligned_columns() ? field_col : 0;
   }

   void
   print_send_desc(const gen_inst *inst)
   {
      if (inst->send.desc_is_reg) {
         text("a0.0");
      } else {
         hex_pad0(inst->send.desc_imm);
      }
   }

   void
   print_send_ex_desc(const gen_inst *inst)
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
   print_send_reg(const gen_operand &op, bool allow_scalar_arf)
   {
      if (op.file == GEN_GRF && !op.indirect) {
         text("r");
         uint(op.nr);
         if (op.subnr) {
            text(".");
            uint(op.subnr / 16);
         }
         return;
      }

      if (allow_scalar_arf && op.file == GEN_ARF && !op.indirect &&
          (op.nr & 0xf0) == GEN_ARF_SCALAR) {
         print_register(op, true, false);
         return;
      }

      if (is_null(op)) {
         text("null");
         return;
      }

      print_register(op, false, false);
   }

   void
   print_send_reg_with_length(const gen_operand &op, bool allow_scalar_arf,
                              int length)
   {
      print_send_reg(op, allow_scalar_arf);
      if (length > 0 && !is_null(op)) {
         text(":");
         uint(length);
      }
   }

   void
   print_dst(const gen_operand &dst, bool align16)
   {
      print_register(dst, show_operand_subreg(), false);
      if (!omit_dst_region(dst)) {
         text("<");
         uint(dst.region.hstride);
         text(">");
      }
      if (align16)
         text(writemask[dst.writemask]);
      print_type(dst.type);
   }

   void
   print_src(const gen_operand &src, bool align16)
   {
      if (src.negate && src.abs)
         text("-|");
      else if (src.negate)
         text("-");
      else if (src.abs)
         text("|");

      if (src.file == GEN_IMM) {
         print_imm(src);
      } else if (src.file == GEN_ARF && src.nr == GEN_ARF_NULL) {
         print_arf(src, false, false);
      } else {
         print_register(src, show_operand_subreg(), false);
         if (!omit_src_region(src))
            print_region(src.region);

         if (align16)
            print_swizzle(src.swizzle);

         print_type(src.type);
      }

      if (src.abs)
         text("|");
   }

   void
   print_src_3src(const gen_inst *inst, const gen_operand &src, unsigned src_idx)
   {
      assert(src_idx < 3);

      if (src.negate && src.abs)
         text("-|");
      else if (src.negate)
         text("-");
      else if (src.abs)
         text("|");

      if (src.file == GEN_IMM) {
         print_imm(src);
      } else {
         print_register(src, show_operand_subreg(), false);
         if (!omit_src_region(src))
            print_region_3src(src.region, src_idx);
         if (inst->align16 && src.rep_ctrl)
            text(".r");
         print_type(src.type);
      }

      if (src.abs)
         text("|");
   }

   void
   print_region(const gen_region &region)
   {
      if (uniform_src_region(region)) {
         text("<0>");
         return;
      }

      text("<");
      if (region.vstride == GEN_VSTRIDE_ONE_DIMENSIONAL) {
         text("VxH");
      } else {
         uint(region.vstride);
      }
      text(";");
      uint(region.width);
      text(",");
      uint(region.hstride);
      text(">");
   }

   void
   print_region_3src(const gen_region &region, unsigned src_idx)
   {
      if (uniform_src_region(region)) {
         text("<0>");
         return;
      }

      text("<");
      if (src_idx < 2) {
         uint(region.vstride);
         text(";");
         uint(region.hstride);
      } else {
         uint(region.hstride);
      }
      text(">");
   }

   void
   print_swizzle(unsigned swizzle)
   {
#define BRW_SWIZZLE4(a,b,c,d) (((a)<<0) | ((b)<<2) | ((c)<<4) | ((d)<<6))
#define BRW_GET_SWZ(swz, idx) (((swz) >> ((idx)*2)) & 0x3)
#define BRW_SWIZZLE_XYZW      BRW_SWIZZLE4(0,1,2,3)
      const unsigned x = BRW_GET_SWZ(swizzle, 0);
      const unsigned y = BRW_GET_SWZ(swizzle, 1);
      const unsigned z = BRW_GET_SWZ(swizzle, 2);
      const unsigned w = BRW_GET_SWZ(swizzle, 3);

      if (x == y && x == z && x == w) {
         text(".");
         text(chan_sel[x]);
      } else if (swizzle != BRW_SWIZZLE_XYZW) {
         text(".");
         text(chan_sel[x]);
         text(chan_sel[y]);
         text(chan_sel[z]);
         text(chan_sel[w]);
      }
#undef BRW_SWIZZLE4
#undef BRW_GET_SWZ
#undef BRW_SWIZZLE_XYZW
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
         format("0x%016" PRIx64, src.imm);
         print_type(src.type);
         return;
      case GEN_TYPE_Q:
         format("0x%016" PRIx64, src.imm);
         print_type(src.type);
         return;
      case GEN_TYPE_UD:
         format("0x%08x", (uint32_t)src.imm);
         print_type(src.type);
         return;
      case GEN_TYPE_D:
         format("%" PRId32, (int32_t)src.imm);
         print_type(src.type);
         return;
      case GEN_TYPE_UW:
         format("%" PRIu16, (uint16_t)src.imm);
         print_type(src.type);
         return;
      case GEN_TYPE_W:
         format("%" PRId16, (int16_t)src.imm);
         print_type(src.type);
         return;
      case GEN_TYPE_UV:
         format("0x%08x", (uint32_t)src.imm);
         print_type(src.type);
         return;
      case GEN_TYPE_V:
         format("0x%08x", (uint32_t)src.imm);
         print_type(src.type);
         return;
      case GEN_TYPE_VF:
         format("0x%08x", (uint32_t)src.imm);
         print_type(src.type);
         return;
      case GEN_TYPE_F:
         format("0x%08x", (uint32_t)src.imm);
         print_type(src.type);
         return;
      case GEN_TYPE_DF:
         format("0x%016" PRIx64, src.imm);
         print_type(src.type);
         return;
      case GEN_TYPE_HF:
         format("0x%04x", (uint16_t)src.imm);
         print_type(src.type);
         return;
      case GEN_TYPE_UB:
         format("%" PRIu8, (uint8_t)src.imm);
         print_type(src.type);
         return;
      case GEN_TYPE_B:
         format("%" PRId8, (int8_t)src.imm);
         print_type(src.type);
         return;
      default:
         text("<?>:?");
         return;
      }
   }

   bool
   print_annotation(const gen_inst *inst, int idx, unsigned field_col)
   {
      std::vector<std::string> notes;

      if (inst->acc_wr_control)
         notes.push_back("AccWrEn");
      if (inst->atomic_control || inst->thread_control == GEN_THREAD_ATOMIC)
         notes.push_back("Atomic");
      if (inst->debug_control)
         notes.push_back("Breakpoint");
      if (inst->align16)
         notes.push_back("Align16");
      if (was_compacted && was_compacted[idx])
         notes.push_back("Compacted");
      if (gen_inst_is_send(inst) && inst->send.eot)
         notes.push_back("EOT");
      if (inst->no_dd_check)
         notes.push_back("NoDDChk");
      if (inst->no_dd_clear)
         notes.push_back("NoDDClr");
      if (inst->thread_control == GEN_THREAD_SWITCH)
         notes.push_back("Switch");
      if (inst->fusion_control)
         notes.push_back("Serialize");
      if (gen_inst_is_send(inst) && inst->send.ex_bso)
         notes.push_back("ExBSO");

      if (inst->swsb.regdist) {
         std::string note;
         switch (inst->swsb.pipe) {
         case GEN_PIPE_FLOAT:  note = "F"; break;
         case GEN_PIPE_INT:    note = "I"; break;
         case GEN_PIPE_LONG:   note = "L"; break;
         case GEN_PIPE_ALL:    note = "A"; break;
         case GEN_PIPE_MATH:   note = "M"; break;
         case GEN_PIPE_SCALAR: note = "S"; break;
         case GEN_PIPE_NONE:   break;
         }
         note += "@" + std::to_string(inst->swsb.regdist);
         notes.push_back(note);
      }

      if (inst->swsb.mode) {
         std::string note = "$" + std::to_string(inst->swsb.sbid);
         if (!(inst->swsb.mode & GEN_SBID_SET))
            note += inst->swsb.mode & GEN_SBID_DST ? ".dst" : ".src";
         notes.push_back(note);
      }

      if (notes.empty())
         return false;

      field_sep(field_col);
      text("{");
      for (unsigned i = 0; i < notes.size(); i++) {
         if (i)
            text(",");
         text(notes[i].c_str());
      }
      text("}");
      return true;
   }

   void
   print_send_comment(const gen_inst *inst, unsigned field_col)
   {
      lsc_symbolic_info lsc;
      if (!symbolic_lsc_comment(inst, &lsc))
         return;

      field_sep(field_col);
      text("// ");
      text(lsc.mnemonic.c_str());
   }

   void
   field_sep(unsigned col)
   {
      if (!aligned_columns()) {
         text(" ");
         return;
      }

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
      format("%u", n);
   }

   void
   hex_pad0(unsigned n)
   {
      format("0x%08X", n);
   }

   void
   text(const char *s)
   {
      fputs(s, fp);
      for (const char *p = s; *p; p++)
         column = *p == '\n' ? 0 : column + 1;
   }

   void
   newline()
   {
      fputc('\n', fp);
      column = 0;
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

static void
print_annotation_text(FILE *fp, const char *text)
{
   if (!text || !text[0])
      return;

   fputs(text, fp);
   if (text[strlen(text) - 1] != '\n')
      fputc('\n', fp);
}

static void
gen_print_with_labels(const intel_device_info *devinfo,
                      FILE *fp,
                      gen_inst **insts,
                      int num_insts,
                      gen_print_flags flags,
                      const std::map<int, std::string> &labels,
                      const gen_error *errors,
                      int num_errors,
                      const char *const *annotations,
                      const bool *was_compacted)
{
   gen_print_params opts = {
      .devinfo = devinfo,
      .fp = fp,
      .flags = flags,
      .insts = insts,
      .num_insts = num_insts,
      .errors = errors,
      .num_errors = num_errors,
      .annotations = annotations,
      .was_compacted = was_compacted,
   };

   gen_printer p(devinfo, &opts, labels);

   int next_error = 0;
   for (int i = 0; i < num_insts; i++) {
      auto it = labels.find(i);
      if (it != labels.end())
         fprintf(fp, "%s:\n", it->second.c_str());

      if (annotations)
         print_annotation_text(fp, annotations[i]);

      p.print(insts[i], i);

      while (next_error < num_errors &&
             (int)errors[next_error].offset == i) {
         fprintf(fp, "    ERROR: %s\n", errors[next_error].msg);
         next_error++;
      }
   }

   auto end = labels.find(num_insts);
   if (end != labels.end())
      fprintf(fp, "%s:\n", end->second.c_str());
}

const char *
gen_reg_type_to_string(gen_reg_type type)
{
   switch (type) {
   case GEN_TYPE_UB:  return "UB";
   case GEN_TYPE_UW:  return "UW";
   case GEN_TYPE_UD:  return "UD";
   case GEN_TYPE_UQ:  return "UQ";

   case GEN_TYPE_B:   return "B";
   case GEN_TYPE_W:   return "W";
   case GEN_TYPE_D:   return "D";
   case GEN_TYPE_Q:   return "Q";

   case GEN_TYPE_HF8: return "HF8";
   case GEN_TYPE_HF:  return "HF";
   case GEN_TYPE_F:   return "F";
   case GEN_TYPE_DF:  return "DF";

   case GEN_TYPE_BF8: return "BF8";
   case GEN_TYPE_BF:  return "BF";

   case GEN_TYPE_UV:  return "UV";
   case GEN_TYPE_V:   return "V";
   case GEN_TYPE_VF:  return "VF";

   default:           return "INVALID";
   }
}

void
gen_print(gen_print_params *params)
{
   assert(params);
   assert(params->devinfo);
   assert(params->insts);
   assert(params->errors || params->num_errors == 0);

   if (!params->fp)
      params->fp = stderr;

   const intel_device_info *devinfo = params->devinfo;
   gen_inst **insts = params->insts;
   const int num_insts = params->num_insts;

   std::vector<int> targets;
   for (int i = 0; i < num_insts; i++) {
      gen_inst *inst = insts[i];
      if (gen_inst_format(inst->opcode) != GEN_FORMAT_BRANCH)
         continue;

      if (inst->branch.jip) {
         const int target = branch_target_index(i, inst->branch.jip);
         if (target >= 0 && target <= num_insts)
            targets.push_back(target);
      }

      if (gen_has_uip(inst->opcode) && inst->branch.uip) {
         const int target = branch_target_index(i, inst->branch.uip);
         if (target >= 0 && target <= num_insts)
            targets.push_back(target);
      }
   }

   std::sort(targets.begin(), targets.end());
   targets.erase(std::unique(targets.begin(), targets.end()), targets.end());

   std::map<int, std::string> labels;
   for (unsigned i = 0; i < targets.size(); i++)
      labels[targets[i]] = "L" + std::to_string(i);

   gen_print_with_labels(devinfo, params->fp, insts, num_insts, params->flags,
                         labels, params->errors, params->num_errors,
                         params->annotations, params->was_compacted);
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

   gen_inst *insts[] = { const_cast<gen_inst *>(inst) };
   const std::map<int, std::string> labels;

   gen_print_with_labels(devinfo, fp, insts, 1, flags, labels, NULL, 0, NULL,
                         NULL);
}
