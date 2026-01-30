/*
 * Copyright © 2018 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_asm.h"
#include "brw_asm_internal.h"
#include "util/hash_table.h"
#include "util/u_dynarray.h"

#include <string.h>

typedef struct {
   char *name;
   int index; /* -1 for unset */
   struct util_dynarray jip_uses;
   struct util_dynarray uip_uses;
} brw_asm_label;

static brw_asm_label *
brw_asm_label_lookup(struct brw_asm_parser *parser, const char *name)
{
   uint32_t h = _mesa_hash_string(name);
   struct hash_entry *entry =
      _mesa_hash_table_search_pre_hashed(parser->labels, h, name);
   if (!entry) {
      void *mem_ctx = parser->labels;
      brw_asm_label *label = rzalloc(mem_ctx, brw_asm_label);
      label->name = ralloc_strdup(mem_ctx, name);
      label->index = -1;
      util_dynarray_init(&label->jip_uses, mem_ctx);
      util_dynarray_init(&label->uip_uses, mem_ctx);
      entry = _mesa_hash_table_insert_pre_hashed(parser->labels,
                                                 h, name, label);
   }
   assert(entry);
   return entry->data;
}

unsigned
gen_asm_inst_count(const struct brw_asm_parser *parser)
{
   return parser->insts.size / sizeof(gen_inst);
}

static gen_inst *
asm_get_inst(struct brw_asm_parser *parser, unsigned index)
{
   return util_dynarray_element(&parser->insts, gen_inst, index);
}

gen_inst *
gen_asm_next_inst(struct brw_asm_parser *parser, gen_opcode opcode)
{
   gen_inst inst = {};
   inst.opcode = opcode;
   util_dynarray_append(&parser->insts, inst);
   return asm_get_inst(parser, gen_asm_inst_count(parser) - 1);
}

static bool
gen_asm_opcode_has_branch_ctrl(gen_opcode opcode)
{
   switch (opcode) {
   case GEN_OP_IF:
   case GEN_OP_ELSE:
   case GEN_OP_GOTO:
   case GEN_OP_BREAK:
   case GEN_OP_CALL:
   case GEN_OP_CALLA:
   case GEN_OP_CONTINUE:
   case GEN_OP_ENDIF:
   case GEN_OP_HALT:
   case GEN_OP_JMPI:
   case GEN_OP_RET:
   case GEN_OP_WHILE:
   case GEN_OP_BRC:
   case GEN_OP_BRD:
      return true;
   default:
      return false;
   }
}

static bool
xe2_swsb_is_encodable(struct tgl_swsb swsb, gen_opcode opcode)
{
   if (!swsb.mode || !swsb.regdist)
      return true;

   if (opcode == GEN_OP_DPAS)
      return swsb.pipe == TGL_PIPE_NONE;

   if (swsb.mode & TGL_SBID_SET)
      return (opcode == GEN_OP_SEND || opcode == GEN_OP_SENDC) &&
             (swsb.pipe == TGL_PIPE_ALL ||
              swsb.pipe == TGL_PIPE_INT ||
              swsb.pipe == TGL_PIPE_FLOAT);

   if (opcode == GEN_OP_SEND || opcode == GEN_OP_SENDC)
      return false;

   return swsb.pipe == TGL_PIPE_NONE ||
          (swsb.pipe == TGL_PIPE_ALL && swsb.mode == TGL_SBID_DST);
}

static gen_swsb
tgl_swsb_to_gen(struct tgl_swsb swsb)
{
   STATIC_ASSERT((int)GEN_PIPE_NONE   == (int)TGL_PIPE_NONE);
   STATIC_ASSERT((int)GEN_PIPE_FLOAT  == (int)TGL_PIPE_FLOAT);
   STATIC_ASSERT((int)GEN_PIPE_INT    == (int)TGL_PIPE_INT);
   STATIC_ASSERT((int)GEN_PIPE_LONG   == (int)TGL_PIPE_LONG);
   STATIC_ASSERT((int)GEN_PIPE_MATH   == (int)TGL_PIPE_MATH);
   STATIC_ASSERT((int)GEN_PIPE_SCALAR == (int)TGL_PIPE_SCALAR);
   STATIC_ASSERT((int)GEN_PIPE_ALL    == (int)TGL_PIPE_ALL);

   STATIC_ASSERT((int)GEN_SBID_NULL == (int)TGL_SBID_NULL);
   STATIC_ASSERT((int)GEN_SBID_SRC  == (int)TGL_SBID_SRC);
   STATIC_ASSERT((int)GEN_SBID_DST  == (int)TGL_SBID_DST);
   STATIC_ASSERT((int)GEN_SBID_SET  == (int)TGL_SBID_SET);

   return (gen_swsb) {
      .regdist = swsb.regdist,
      .pipe = (gen_pipe)swsb.pipe,
      .sbid = swsb.sbid,
      .mode = (gen_sbid_mode)swsb.mode,
   };
}

void
i965_asm_set_instruction_options(struct brw_asm_parser *parser, gen_inst *inst,
                                 const struct predicate *pred,
                                 const struct condition *cond,
                                 const struct options *options)
{
   if (pred) {
      inst->pred_control = pred->pred_control;
      inst->pred_inv = pred->pred_inv;
      inst->flag_nr = pred->flag_reg_nr;
      inst->flag_subnr = pred->flag_subreg_nr;
   }

   if (cond && cond->cond_modifier) {
      inst->cmod = (gen_condition)cond->cond_modifier;

      if (inst->flag_nr == 0 && inst->flag_subnr == 0) {
         inst->flag_nr = cond->flag_reg_nr;
         inst->flag_subnr = cond->flag_subreg_nr;
      }
   }

   inst->align16 = options->access_mode == BRW_ALIGN_16;
   inst->no_mask = options->mask_control != 0;
   inst->thread_control = options->thread_control;
   inst->branch_control = options->branch_control;
   inst->no_dd_clear = options->no_dd_clear;
   inst->no_dd_check = options->no_dd_check;
   inst->debug_control = options->debug_control;
   inst->acc_wr_control = options->acc_wr_control;
   inst->chan_offset = options->chan_offset;

   if (options->depinfo.regdist || options->depinfo.mode) {
      if (parser->devinfo->ver >= 12) {
         if (parser->devinfo->ver >= 20 &&
             !xe2_swsb_is_encodable(options->depinfo, inst->opcode)) {
            fprintf(stderr,
                    "%s: error: Invalid Xe2+ tuple SWSB encoding for this opcode\n",
                    parser->input_filename);
            parser->errors++;
         } else {
            inst->swsb = tgl_swsb_to_gen(options->depinfo);
         }
      } else {
         fprintf(stderr,
                 "%s: SWSB options are only supported on gfx12+\n",
                 parser->input_filename);
      }
   }

   if (inst->branch_control && !gen_asm_opcode_has_branch_ctrl(inst->opcode))
      fprintf(stderr, "BranchCtrl not supported for opcode %u\n", inst->opcode);
}


void
brw_asm_label_set(struct brw_asm_parser *parser, const char *name)
{
   brw_asm_label *label = brw_asm_label_lookup(parser, name);
   label->index = gen_asm_inst_count(parser);
}

void
brw_asm_label_use_jip(struct brw_asm_parser *parser, const char *name)
{
   brw_asm_label *label = brw_asm_label_lookup(parser, name);
   unsigned index = gen_asm_inst_count(parser) - 1;
   util_dynarray_append(&label->jip_uses, index);
   asm_get_inst(parser, index)->branch.jip = 0;
}

void
brw_asm_label_use_uip(struct brw_asm_parser *parser, const char *name)
{
   brw_asm_label *label = brw_asm_label_lookup(parser, name);
   unsigned index = gen_asm_inst_count(parser) - 1;
   util_dynarray_append(&label->uip_uses, index);
   asm_get_inst(parser, index)->branch.uip = 0;
}

static bool
brw_postprocess_labels(struct brw_asm_parser *parser)
{
   unsigned unknown = 0;

   hash_table_foreach(parser->labels, entry) {
      brw_asm_label *label = entry->data;

      if (label->index == -1) {
         fprintf(stderr, "Unknown label '%s'\n", label->name);
         unknown++;
         continue;
      }

      util_dynarray_foreach(&label->jip_uses, unsigned, use_index) {
         gen_inst *inst = asm_get_inst(parser, *use_index);
         inst->branch.jip = 16 * (label->index - (int)*use_index);
      }

      util_dynarray_foreach(&label->uip_uses, unsigned, use_index) {
         gen_inst *inst = asm_get_inst(parser, *use_index);
         inst->branch.uip = 16 * (label->index - (int)*use_index);
      }
   }

   return unknown == 0;
}

static gen_inst **
assemble_inst_ptrs(void *mem_ctx, struct brw_asm_parser *parser)
{
   unsigned count = gen_asm_inst_count(parser);
   gen_inst **insts = ralloc_array(mem_ctx, gen_inst *, count);

   for (unsigned i = 0; i < count; i++)
      insts[i] = asm_get_inst(parser, i);

   return insts;
}

/* TODO: Would be nice to make this operate on string instead on a FILE. */

brw_assemble_result
brw_assemble(void *mem_ctx, const struct intel_device_info *devinfo,
             FILE *f, const char *filename, brw_assemble_flags flags)
{
   brw_assemble_result result = {0};

   brw_asm_parser *parser = rzalloc(mem_ctx, brw_asm_parser);
   parser->devinfo = devinfo;
   parser->mem_ctx = mem_ctx;
   parser->labels = _mesa_string_hash_table_create(parser);
   parser->input_filename = filename;
   parser->compaction_warning_given = false;
   util_dynarray_init(&parser->insts, parser);

   parser->scanner = NULL;
   brw_asm_lex_init_extra(parser, &parser->scanner);
   brw_asm_restart(f, parser->scanner);

   int err = yyparse(parser);
   brw_asm_lex_destroy(parser->scanner);
   if (err || parser->errors)
      goto end;

   if (!brw_postprocess_labels(parser))
      goto end;

   gen_inst **insts = assemble_inst_ptrs(mem_ctx, parser);
   const unsigned inst_count = gen_asm_inst_count(parser);

   gen_encode_params params = {
      .devinfo = devinfo,
      .mem_ctx = mem_ctx,
      .insts = (const gen_inst **)insts,
      .num_insts = (int)inst_count,
   };

   if (!gen_encode(&params)) {
      gen_print_params print = {
         .devinfo = devinfo,
         .fp = stderr,
         .insts = insts,
         .num_insts = inst_count,
         .errors = params.errors,
         .num_errors = params.num_errors,
      };
      gen_print(&print);
      fprintf(stderr, "Invalid instructions.\n");
      goto end;
   }

   if ((flags & BRW_ASSEMBLE_DUMP) != 0) {
      gen_print_params print = {
         .devinfo = devinfo,
         .fp = stderr,
         .insts = insts,
         .num_insts = inst_count,
      };
      gen_print(&print);
   }

   if ((flags & BRW_ASSEMBLE_COMPACT) != 0)
      fprintf(stderr, "Compaction requested but not implemented in gen_asm.\n");

   result.bin = params.raw_bytes;
   result.bin_size = params.raw_bytes_size;

end:
   ralloc_free(parser);
   return result;
}
