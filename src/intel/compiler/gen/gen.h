/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <assert.h>
#include <stdint.h>

#include "dev/intel_device_info.h"
#include "util/macros.h"

#include "gen_enums.h"
#include "gen_types.h"
#include "gen_helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Finish structured control flow instructions JIPs and UIPs by
 * converting them from absolute indices in the array into relative
 * byte offsets.
 *
 * The caller must ensure that JIP for WHILE instructions is a
 * valid index.  It represents the "back-edge" and can't be inferred
 * since there's no DO instruction marking the start of a loop.
 *
 * Any other JIPs and UIPs set to zero will be inferred by the structure
 * of the program.
 *
 * If a final_halt_idx is provided, that will act as a final synchronization
 * point for the halts and JIPs filled in the instructions.
 */
bool gen_finish_structured_cf(gen_inst **insts, int num_insts, int final_halt_idx);


typedef struct gen_encode_params {
   const struct intel_device_info *devinfo;

   /* Use compacted encoded form for all instructions that support it. */
   bool compact_all;

   /* TODO: Add a `bool *compact` to force compacting certain instructions.
    * It is an error to force compacting an instruction that doesn't support
    * compacted form.  Useful to round-trip representations.  Most of the time
    * callers will want to use `compact_all`.
    */

   const gen_inst **insts;
   int              num_insts;

   /* Must be non-NULL, used for allocating `raw_bytes` (if not pre-allocated)
    * and `errors`.
    */
   void *mem_ctx;

   /* If NULL, it is allocated using mem_ctx.  If not NULL, it represents
    * a buffer of size raw_bytes_size that will be filled in.
    *
    * The size will be updated to the actual size used.
    */
   void *raw_bytes;
   int   raw_bytes_size;

   gen_error *errors;
   int        num_errors;
} gen_encode_params;

bool gen_encode(gen_encode_params *params);


typedef struct gen_decode_params {
   const struct intel_device_info *devinfo;

   const void *raw_bytes;
   int         raw_bytes_size;

   /* Must be non-NULL, used for allocating the arrays below. */
   void *mem_ctx;

   gen_inst **insts;
   int        num_insts;

   gen_error *errors;
   int        num_errors;

   /* Whether instruction as originally compacted, same size as `insts`. */
   bool *was_compacted;
} gen_decode_params;

bool gen_decode(gen_decode_params *params);


typedef struct gen_validate_params {
   const struct intel_device_info *devinfo;

   const gen_inst **insts;
   int              num_insts;

   /* Must be non-NULL, used for allocating `errors`. */
   void *mem_ctx;

   gen_error *errors;
   int        num_errors;
} gen_validate_params;

bool gen_validate(gen_validate_params *params);


typedef enum gen_print_flags {
   GEN_PRINT_NONE = 0,

   /* Don't omit regions, types and other values that can be inferred. */
   GEN_PRINT_VERBOSE = 1 << 0,

   /* Print SENDs instead of translated operations like LOAD and STORE. */
   GEN_PRINT_RAW_SENDS = 1 << 1,
} gen_print_flags;

typedef struct gen_print_params {
   const struct intel_device_info *devinfo;

   /* When NULL, uses stderr. */
   FILE *fp;

   gen_print_flags flags;

   gen_inst **insts;
   int        num_insts;

   /* Optional errors to print inline. */
   const gen_error *errors;
   int              num_errors;

   /* Optional per-instruction information.  When non-NULL, these
    * arrays must have `num_insts` elements.
    */
   const char *const *annotations;
   const bool        *was_compacted;
} gen_print_params;

void gen_print(gen_print_params *params);

void gen_print_inst(const struct intel_device_info *devinfo,
                    FILE *fp,
                    const gen_inst *inst,
                    gen_print_flags flags);

const char *gen_opcode_to_string(gen_opcode op);


gen_lsc_desc gen_lsc_desc_decode(const struct intel_device_info *devinfo,
                                 uint32_t desc);

uint32_t gen_lsc_desc_encode(const struct intel_device_info *devinfo,
                             const gen_lsc_desc *desc);

gen_lsc_ex_desc gen_lsc_ex_desc_decode(const struct intel_device_info *devinfo,
                                       enum lsc_addr_surface_type addr_type,
                                       uint32_t ex_desc);

uint32_t gen_lsc_ex_desc_encode(const struct intel_device_info *devinfo,
                                const gen_lsc_ex_desc *ex_desc);

static inline uint32_t
lsc_msg_desc(const struct intel_device_info *devinfo,
             enum lsc_opcode opcode,
             enum lsc_addr_surface_type addr_type,
             enum lsc_addr_size addr_sz,
             enum lsc_data_size data_sz, unsigned num_channels_or_cmask,
             bool transpose, unsigned cache_ctrl)
{
   assert(devinfo->has_lsc);
   assert(!transpose || lsc_opcode_has_transpose(opcode));

   gen_lsc_desc desc = {};
   desc.op = opcode;
   desc.addr_type = addr_type;
   desc.addr_size = addr_sz;
   desc.data_size = data_sz;
   desc.cache_ctrl = cache_ctrl;

   if (lsc_opcode_has_cmask(opcode)) {
      desc.cmask = (enum lsc_cmask)num_channels_or_cmask;
   } else {
      desc.vect_size = lsc_vect_size(num_channels_or_cmask);
      desc.transpose = transpose;
   }

   return gen_lsc_desc_encode(devinfo, &desc);
}

/* Returns the encoded shader size in bytes starting at `start`.
 *
 * If `end_bound` is non-zero, it is treated as an absolute byte offset from
 * `assembly` and scanning stops before reading past it.  Pass 0 for the old
 * unbounded behavior.
 */
int gen_find_shader_size(const struct intel_device_info *devinfo,
                         const void *assembly,
                         int start,
                         int end_bound);

#ifdef __cplusplus
} /* extern "C" */
#endif
