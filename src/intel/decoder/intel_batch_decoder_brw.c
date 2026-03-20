/*
 * Copyright © 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "intel_decoder.h"
#include "intel_decoder_private.h"

#include "intel/compiler/gen/gen.h"

#include "util/ralloc.h"

static void
gen_disassemble_program(struct intel_batch_decode_ctx *ctx,
                        const void *assembly,
                        uint32_t size)
{
   void *mem_ctx = ralloc_context(NULL);
   gen_decode_params decode = {
      .devinfo = &ctx->devinfo,
      .raw_bytes = assembly,
      .raw_bytes_size = size,
      .mem_ctx = mem_ctx,
   };

   gen_decode(&decode);

   gen_print_params print = {
      .devinfo = &ctx->devinfo,
      .fp = ctx->fp,
      .flags = GEN_PRINT_RAW_SENDS,
      .insts = decode.insts,
      .num_insts = decode.num_insts,
      .errors = decode.errors,
      .num_errors = decode.num_errors,
      .was_compacted = decode.was_compacted,
   };

   gen_print(&print);
   ralloc_free(mem_ctx);
}

static void
ctx_disassemble_program_brw(struct intel_batch_decode_ctx *ctx,
                            uint32_t ksp,
                            const char *short_name,
                            const char *name)
{
   uint64_t addr = ctx->instruction_base + ksp;
   struct intel_batch_decode_bo bo = ctx_get_bo(ctx, true, addr);
   if (!bo.map)
      return;

   const int size = gen_find_shader_size(&ctx->devinfo, bo.map, 0, bo.size);
   if (size == 0)
      return;

   fprintf(ctx->fp, "\nReferenced %s:\n", name);
   gen_disassemble_program(ctx, bo.map, size);

   if (ctx->shader_binary) {
      ctx->shader_binary(ctx->user_data, short_name, addr,
                         bo.map, size);
   }
}

void
intel_batch_decode_ctx_init_brw(struct intel_batch_decode_ctx *ctx,
                                const struct brw_isa_info *isa,
                                const struct intel_device_info *devinfo,
                                FILE *fp, enum intel_batch_decode_flags flags,
                                const char *xml_path,
                                struct intel_batch_decode_bo (*get_bo)(void *,
                                                                       bool,
                                                                       uint64_t),
                                unsigned (*get_state_size)(void *, uint64_t,
                                                           uint64_t),
                                void *user_data)
{
   intel_batch_decode_ctx_init(ctx, devinfo, fp, flags, xml_path,
                               get_bo, get_state_size, user_data);
   ctx->brw = isa;
   ctx->disassemble_program = ctx_disassemble_program_brw;
}
