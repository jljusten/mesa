/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

// TODO
//
// - Finish importing symbols from brw.
// - Import compact code.
// - Handle the "// TODO:" comments spread around the code.

#include "brw/brw_eu_defines.h"
#include "brw/brw_reg_type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gen_opcode {
   GEN_OP_ILLEGAL,

   GEN_OP_ADD,
   GEN_OP_ADD3,
   GEN_OP_ADDC,
   GEN_OP_AND,
   GEN_OP_ASR,
   GEN_OP_AVG,
   GEN_OP_BFE,
   GEN_OP_BFI1,
   GEN_OP_BFI2,
   GEN_OP_BFN,
   GEN_OP_BFREV,
   GEN_OP_BRC,
   GEN_OP_BRD,
   GEN_OP_BREAK,
   GEN_OP_CALL,
   GEN_OP_CALLA,
   GEN_OP_CBIT,
   GEN_OP_CMP,
   GEN_OP_CMPN,
   GEN_OP_CONTINUE,
   GEN_OP_CSEL,
   GEN_OP_DP2,
   GEN_OP_DP3,
   GEN_OP_DP4,
   GEN_OP_DP4A,
   GEN_OP_DPAS,
   GEN_OP_DPH,
   GEN_OP_ELSE,
   GEN_OP_ENDIF,
   GEN_OP_FBH,
   GEN_OP_FBL,
   GEN_OP_FRC,
   GEN_OP_GOTO,
   GEN_OP_HALT,
   GEN_OP_IF,
   GEN_OP_JMPI,
   GEN_OP_JOIN,
   GEN_OP_LINE,
   GEN_OP_LRP,
   GEN_OP_LZD,
   GEN_OP_MAC,
   GEN_OP_MACH,
   GEN_OP_MAD,
   GEN_OP_MADM,
   GEN_OP_MATH,
   GEN_OP_MOV,
   GEN_OP_MOVI,
   GEN_OP_MUL,
   GEN_OP_NOP,
   GEN_OP_NOT,
   GEN_OP_OR,
   GEN_OP_PLN,
   GEN_OP_RET,
   GEN_OP_RNDD,
   GEN_OP_RNDE,
   GEN_OP_RNDU,
   GEN_OP_RNDZ,
   GEN_OP_ROL,
   GEN_OP_ROR,
   GEN_OP_SEL,
   GEN_OP_SEND,
   GEN_OP_SENDC,
   GEN_OP_SENDS,
   GEN_OP_SENDSC,
   GEN_OP_SHL,
   GEN_OP_SHR,
   GEN_OP_SMOV,
   GEN_OP_SRND,
   GEN_OP_SUBB,
   GEN_OP_SYNC,
   GEN_OP_WAIT,
   GEN_OP_WHILE,
   GEN_OP_XOR,
} gen_opcode;

typedef enum gen_file {
   GEN_BAD_FILE,
   GEN_GRF,
   GEN_ARF,
   GEN_IMM,
} gen_file;

typedef struct gen_region {
   uint8_t vstride;
   uint8_t width;
   uint8_t hstride;
} gen_region;

typedef struct gen_operand {
   enum gen_file file;
   enum brw_reg_type type;
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

// TODO: Proper packing.

typedef struct gen_inst {
   gen_opcode opcode;

   uint8_t exec_size;

   enum brw_conditional_mod cond_modifier;
   enum brw_predicate pred_control;
   uint8_t chan_offset;
   uint8_t flag_subnr;
   uint8_t flag_nr;
   uint8_t boolean_func_ctrl;  /* For BFN instruction */
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

   // TODO: Document Gfx12 only.
   bool fusion_control;

   // TODO: Document Gfx < 20 only.
   bool acc_wr_control;

   struct tgl_swsb swsb;

   struct {
      int32_t jip;
      int32_t uip;
   } branch;

   struct {
      // TODO: Make enum.
      uint8_t func;
   } math;

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
      uint8_t sfid;
      uint8_t src1_len;

      uint32_t desc_imm;
      uint32_t ex_desc_imm;

      // TODO: HERE: Reuse ex_desc_imm for this one.

      /* Gen20+ only: immediate offset bits encoded in instruction when
       * using indirect extended descriptor (has_ex_desc_imm == false) */
      uint32_t ex_desc_imm_extra;

   } send;

   gen_operand dst;
   gen_operand src[3];
} gen_inst;

// TODO: Make array variant of all those.


typedef struct gen_raw_inst {
   uint64_t data[2];
} gen_raw_inst;


typedef struct gen_error {
   unsigned offset;
   const char *msg;
} gen_error;

typedef struct gen_encode_params {
   const struct intel_device_info *devinfo;

   /* Must be non-NULL, used for allocating `raw_bytes` (if not pre-allocated)
    * and `errors`.
    */
   void *mem_ctx;

   const gen_inst **insts;
   int              num_insts;

   /* If NULL, it is allocated using mem_ctx.  If not NULL, it represents
    * a buffer of size raw_bytes_size that will be filled in.
    *
    * The size will be updated to the actual size used.
    */
   void *raw_bytes;
   int   raw_bytes_size;

   gen_error *errors;
   int        num_errors;

   // TODO: Add compact option!
} gen_encode_params;

bool gen_encode(gen_encode_params *params);


typedef struct gen_decode_params {
   const struct intel_device_info *devinfo;

   const void *raw_bytes;
   int         raw_bytes_size;

   /* Must be non-NULL, used for allocating `insts` and `errors`. */
   void *mem_ctx;

   gen_inst **insts;
   int        num_insts;

   gen_error *errors;
   int        num_errors;


   // TODO: Add "was compacted" information. Should this be in a bitset
   // separatedly?
} gen_decode_params;

bool gen_decode(gen_decode_params *params);

typedef struct gen_print_options {
   /* Default is to use stderr. */
   FILE *fp;

   const gen_error *errors;
   int              num_errors;
} gen_print_options;

void gen_print(const struct intel_device_info *devinfo,
               gen_inst **insts, int num_insts,
               gen_print_options opts);

// TODO: Print single instruction.
// TODO: "to string" for single instruction?

// TODO: Do we need validate public here or just in private?

typedef struct gen_validate_params {
   const struct intel_device_info *devinfo;

   /* Must be non-NULL, used for allocating `errors`. */
   void *mem_ctx;

   const gen_inst **insts;
   int              num_insts;

   gen_error *errors;
   int        num_errors;
} gen_validate_params;

bool gen_validate(gen_validate_params *params);

bool gen_finish_structured_cf(gen_inst **insts, int num_insts, int final_halt_idx);

#ifdef __cplusplus
} /* extern "C" */
#endif
