/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "dev/intel_device_info.h"

#include "gen_enums.h"
#include "gen_types.h"

#ifdef __cplusplus
extern "C" {
#endif

unsigned gen_inst_num_sources(const struct intel_device_info *devinfo,
                              const gen_inst *inst);
bool gen_has_uip(gen_opcode op);
bool gen_has_jip(gen_opcode op);

/**
 * Return the source index for JIP (Jump Index Pointer) in a branch
 * instruction, or -1 if the instruction has no JIP.
 *
 * For all branch instructions, JIP is always src[0].
 * In the hardware encoding, JIP occupies the Src1 bit region
 * (IMM_LO_32 / BRANCH_JIP at bits [127:96]).
 */
static inline int
gen_inst_jip_src_index(gen_opcode op)
{
   return gen_has_jip(op) ? 0 : -1;
}

/**
 * Return the source index for UIP (Update Index Pointer) in a branch
 * instruction, or -1 if the instruction has no UIP.
 *
 * For all branch instructions with UIP, it is always src[1].
 * In the hardware encoding, UIP occupies the Src0 bit region
 * (IMM_HI_32 / BRANCH_UIP at bits [95:64]).
 */
static inline int
gen_inst_uip_src_index(gen_opcode op)
{
   return gen_has_uip(op) ? 1 : -1;
}
bool gen_has_branch_ctrl(gen_opcode opcode);

static inline bool
gen_type_is_float(gen_reg_type t)
{
   return (t & GEN_TYPE_BASE_MASK) == GEN_TYPE_BASE_FLOAT;
}

static inline bool
gen_type_is_bfloat(gen_reg_type t)
{
   return (t & GEN_TYPE_BASE_MASK) == GEN_TYPE_BASE_BFLOAT;
}

static inline bool
gen_type_is_float_or_bfloat(gen_reg_type t)
{
   return gen_type_is_float(t) || gen_type_is_bfloat(t);
}

static inline bool
gen_type_is_uint(gen_reg_type t)
{
   return (t & GEN_TYPE_BASE_MASK) == GEN_TYPE_BASE_UINT;
}

static inline bool
gen_type_is_sint(gen_reg_type t)
{
   return (t & GEN_TYPE_BASE_MASK) == GEN_TYPE_BASE_SINT;
}

static inline bool
gen_type_is_int(gen_reg_type t)
{
   return gen_type_is_uint(t) || gen_type_is_sint(t);
}

static inline bool
gen_type_is_vector_imm(gen_reg_type t)
{
   return t & GEN_TYPE_VECTOR;
}

static inline unsigned
gen_type_size_bits(gen_reg_type t)
{
   /* [U]V components are 4-bit, but HW unpacks them to 16-bit.
    * Similarly, VF is expanded to 32-bit.
    */
   return 8 << (t & GEN_TYPE_SIZE_MASK);
}

static inline unsigned
gen_type_size_bytes(gen_reg_type t)
{
   return gen_type_size_bits(t) / 8;
}

#ifndef INTEL_MASK
#define INTEL_MASK(high, low) (((1u<<((high)-(low)+1))-1)<<(low))
#define SET_BITS(value, high, low)                                      \
   ({                                                                   \
      const uint32_t fieldval = (uint32_t)(value) << (low);             \
      assert((fieldval & ~INTEL_MASK(high, low)) == 0);                 \
      fieldval & INTEL_MASK(high, low);                                 \
   })

#define GET_BITS(data, high, low) ((data & INTEL_MASK((high), (low))) >> (low))
#endif

static inline bool
lsc_opcode_has_cmask(enum lsc_opcode opcode)
{
   return opcode == LSC_OP_LOAD_CMASK || opcode == LSC_OP_STORE_CMASK ||
          opcode == LSC_OP_LOAD_CMASK_MSRT ||
          opcode == LSC_OP_STORE_CMASK_MSRT;
}

static inline bool
lsc_opcode_has_transpose(enum lsc_opcode opcode)
{
   return opcode == LSC_OP_LOAD || opcode == LSC_OP_STORE;
}

static inline bool
lsc_opcode_is_store(enum lsc_opcode opcode)
{
   return opcode == LSC_OP_STORE ||
          opcode == LSC_OP_STORE_CMASK ||
          opcode == LSC_OP_STORE_CMASK_MSRT;
}

static inline bool
lsc_opcode_is_atomic(enum lsc_opcode opcode)
{
   switch (opcode) {
   case LSC_OP_ATOMIC_INC:
   case LSC_OP_ATOMIC_DEC:
   case LSC_OP_ATOMIC_LOAD:
   case LSC_OP_ATOMIC_STORE:
   case LSC_OP_ATOMIC_ADD:
   case LSC_OP_ATOMIC_SUB:
   case LSC_OP_ATOMIC_MIN:
   case LSC_OP_ATOMIC_MAX:
   case LSC_OP_ATOMIC_UMIN:
   case LSC_OP_ATOMIC_UMAX:
   case LSC_OP_ATOMIC_CMPXCHG:
   case LSC_OP_ATOMIC_FADD:
   case LSC_OP_ATOMIC_FSUB:
   case LSC_OP_ATOMIC_FMIN:
   case LSC_OP_ATOMIC_FMAX:
   case LSC_OP_ATOMIC_FCMPXCHG:
   case LSC_OP_ATOMIC_AND:
   case LSC_OP_ATOMIC_OR:
   case LSC_OP_ATOMIC_XOR:
      return true;
   default:
      return false;
   }
}

static inline bool
lsc_opcode_is_atomic_float(enum lsc_opcode opcode)
{
   switch (opcode) {
   case LSC_OP_ATOMIC_FADD:
   case LSC_OP_ATOMIC_FSUB:
   case LSC_OP_ATOMIC_FMIN:
   case LSC_OP_ATOMIC_FMAX:
   case LSC_OP_ATOMIC_FCMPXCHG:
      return true;
   default:
      return false;
   }
}

static inline unsigned
lsc_op_num_data_values(unsigned _op)
{
   const enum lsc_opcode op = (enum lsc_opcode)_op;

   switch (op) {
   case LSC_OP_ATOMIC_CMPXCHG:
   case LSC_OP_ATOMIC_FCMPXCHG:
      return 2;
   case LSC_OP_ATOMIC_INC:
   case LSC_OP_ATOMIC_DEC:
   case LSC_OP_ATOMIC_LOAD:
   case LSC_OP_LOAD:
   case LSC_OP_LOAD_CMASK:
   case LSC_OP_FENCE:
   case LSC_OP_LOAD_CMASK_MSRT:
      return 0;
   default:
      return 1;
   }
}

static inline uint32_t
lsc_data_size_bytes(enum lsc_data_size data_size)
{
   switch (data_size) {
   case LSC_DATA_SIZE_D8:      return 1;
   case LSC_DATA_SIZE_D16:     return 2;
   case LSC_DATA_SIZE_D32:
   case LSC_DATA_SIZE_D8U32:
   case LSC_DATA_SIZE_D16U32:
   case LSC_DATA_SIZE_D16BF32: return 4;
   case LSC_DATA_SIZE_D64:     return 8;
   default:
      assert(!"Unsupported LSC data size");
      return 0;
   }
}

static inline uint32_t
lsc_addr_size_bytes(enum lsc_addr_size addr_size)
{
   switch (addr_size) {
   case LSC_ADDR_SIZE_A16: return 2;
   case LSC_ADDR_SIZE_A32: return 4;
   case LSC_ADDR_SIZE_A64: return 8;
   default:
      assert(!"Unsupported LSC address size");
      return 0;
   }
}

static inline uint32_t
lsc_vector_length(enum lsc_vect_size vect_size)
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
   default:
      assert(!"Unsupported LSC vector size");
      return 0;
   }
}

static inline enum lsc_vect_size
lsc_vect_size(unsigned vect_size)
{
   switch (vect_size) {
   case 1:  return LSC_VECT_SIZE_V1;
   case 2:  return LSC_VECT_SIZE_V2;
   case 3:  return LSC_VECT_SIZE_V3;
   case 4:  return LSC_VECT_SIZE_V4;
   case 8:  return LSC_VECT_SIZE_V8;
   case 16: return LSC_VECT_SIZE_V16;
   case 32: return LSC_VECT_SIZE_V32;
   case 64: return LSC_VECT_SIZE_V64;
   default:
      assert(!"Unsupported LSC vector size");
      return LSC_VECT_SIZE_V1;
   }
}

static inline enum lsc_opcode
lsc_msg_desc_opcode(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   return (enum lsc_opcode)GET_BITS(desc, 5, 0);
}

static inline enum lsc_addr_size
lsc_msg_desc_addr_size(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   return (enum lsc_addr_size)GET_BITS(desc, 8, 7);
}

static inline enum lsc_data_size
lsc_msg_desc_data_size(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   return (enum lsc_data_size)GET_BITS(desc, 11, 9);
}

static inline enum lsc_vect_size
lsc_msg_desc_vect_size(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   assert(!lsc_opcode_has_cmask(lsc_msg_desc_opcode(devinfo, desc)));
   return (enum lsc_vect_size)GET_BITS(desc, 14, 12);
}

static inline enum lsc_cmask
lsc_msg_desc_cmask(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   assert(lsc_opcode_has_cmask(lsc_msg_desc_opcode(devinfo, desc)));
   return (enum lsc_cmask)GET_BITS(desc, 15, 12);
}

static inline bool
lsc_msg_desc_transpose(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   return GET_BITS(desc, 15, 15);
}

static inline unsigned
lsc_msg_desc_cache_ctrl(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   return devinfo->ver >= 20 ? GET_BITS(desc, 19, 16) : GET_BITS(desc, 19, 17);
}

static inline enum lsc_addr_surface_type
lsc_msg_desc_addr_type(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   return (enum lsc_addr_surface_type)GET_BITS(desc, 30, 29);
}

static inline uint32_t
lsc_fence_msg_desc(const struct intel_device_info *devinfo,
                   enum lsc_fence_scope scope,
                   enum lsc_flush_type flush_type,
                   bool route_to_lsc)
{
   assert(devinfo->has_lsc);

   return SET_BITS(LSC_OP_FENCE, 5, 0) |
          SET_BITS(LSC_ADDR_SIZE_A32, 8, 7) |
          SET_BITS(scope, 11, 9) |
          SET_BITS(flush_type, 14, 12) |
          SET_BITS(route_to_lsc, 18, 18) |
          SET_BITS(LSC_ADDR_SURFTYPE_FLAT, 30, 29);
}

static inline enum lsc_fence_scope
lsc_fence_msg_desc_scope(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   return (enum lsc_fence_scope)GET_BITS(desc, 11, 9);
}

static inline enum lsc_flush_type
lsc_fence_msg_desc_flush_type(const struct intel_device_info *devinfo,
                              uint32_t desc)
{
   assert(devinfo->has_lsc);
   return (enum lsc_flush_type)GET_BITS(desc, 14, 12);
}

static inline enum lsc_backup_fence_routing
lsc_fence_msg_desc_backup_routing(const struct intel_device_info *devinfo,
                                  uint32_t desc)
{
   assert(devinfo->has_lsc);
   return (enum lsc_backup_fence_routing)GET_BITS(desc, 18, 18);
}

static inline int
gen_inst_send_src0_len(const gen_inst *inst)
{
   if (inst->send.desc_is_reg)
      return -1;
   return (inst->send.desc_imm >> 25) & 0xF;
}

static inline int
gen_inst_send_src1_len(const struct intel_device_info *devinfo,
                       const gen_inst *inst)
{
   if (inst->send.src1_len)
      return inst->send.src1_len;
   if (inst->send.ex_desc_is_reg)
      return -1;
   return (inst->send.ex_desc_imm >> 6) & (devinfo->ver >= 20 ? 0x1F : 0xF);
}

#ifdef __cplusplus
} /* extern "C" */
#endif
