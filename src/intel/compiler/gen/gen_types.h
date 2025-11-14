/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gen_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gen_region {
   uint8_t vstride;
   uint8_t width;
   uint8_t hstride;
} gen_region;

typedef struct gen_operand {
   gen_file file;
   gen_reg_type type;
   bool indirect:1;
   bool negate:1;
   bool abs:1;
   bool rep_ctrl:1;

   gen_region region;

   uint8_t swizzle;
   uint8_t writemask;

   /* In bytes. */
   uint8_t subnr;

   union {
      /* Physical register number. */
      unsigned nr;
      int addr_imm;
      uint64_t imm;
   };
} gen_operand;

typedef struct gen_swsb {
   unsigned regdist : 3;
   gen_pipe pipe : 3;
   unsigned sbid : 5;
   gen_sbid_mode mode : 3;
} gen_swsb;

typedef struct gen_lsc_desc {
   enum lsc_opcode op;
   enum lsc_addr_surface_type addr_type;
   enum lsc_addr_size addr_size;

   /* Common for all non-fence ops. */
   enum lsc_data_size data_size;
   unsigned cache_ctrl;

   /* Non-CMask. */
   enum lsc_vect_size vect_size;
   bool transpose;

   /* CMask only. */
   enum lsc_cmask cmask;

   /* Fence only. */
   struct {
      enum lsc_fence_scope scope;
      enum lsc_flush_type flush_type;
      bool route_to_lsc;
   } fence;
} gen_lsc_desc;

/* LSC-specific surface bits of the extended descriptor.  The generic SEND
 * ex_mlen bits are handled separately.
 */
typedef struct gen_lsc_ex_desc {
   enum lsc_addr_surface_type addr_type;

   union {
      struct {
         unsigned base_offset;
      } flat;

      struct {
         unsigned surface_state_index;
      } surface_state;

      struct {
         unsigned index;
         unsigned base_offset;
      } bti;
   };
} gen_lsc_ex_desc;

typedef struct gen_inst {
   gen_opcode opcode;

   uint8_t exec_size;

   gen_condition cmod;
   gen_predicate pred_control;
   uint8_t chan_offset;
   uint8_t flag_subnr;
   uint8_t flag_nr;
   uint8_t boolean_func_ctrl;  /* For BFN instruction. */
   uint8_t thread_control;

   bool align16:1;
   bool pred_inv:1;
   bool saturate:1;
   bool no_mask:1;
   bool branch_control:1;
   bool no_dd_clear:1;
   bool no_dd_check:1;
   bool atomic_control:1;
   bool debug_control:1;

   bool fusion_control;  /* Gfx12 only. */
   bool acc_wr_control;  /* Gfx12 only. */

   gen_swsb swsb;

   struct {
      gen_math func;
   } math;

   struct {
      gen_sync_func func;
   } sync;

   /* DPAS. */
   struct {
      uint8_t sdepth;
      uint8_t rcount;

      uint8_t src1_subbyte;
      uint8_t src2_subbyte;
   } dpas;

   struct {
      bool desc_is_reg:1;
      bool ex_desc_is_reg:1;
      bool ex_bso:1;
      bool eot:1;

      uint8_t ex_desc_subnr;
      gen_sfid sfid;
      uint8_t src1_len;

      uint32_t desc_imm;
      uint32_t ex_desc_imm;

      uint32_t ex_desc_imm_extra;  /* Gfx20+ only. */
   } send;

   gen_operand dst;
   gen_operand src[3];
} gen_inst;

typedef struct gen_raw_inst {
   uint64_t data[2];
} gen_raw_inst;

typedef struct gen_error {
   unsigned index;
   const char *msg;
} gen_error;

#ifdef __cplusplus
} /* extern "C" */
#endif
