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
