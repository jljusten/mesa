/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "brw_fs.h"
#include "brw_fs_cfg.h"

namespace brw {

namespace { /* avoid conflict with opt_copy_propagation_elements */
struct acp_entry : public exec_node {
   fs_reg dst;
   fs_reg src;
};
}

/** @file brw_fs_copy_propagation.cpp
 *
 * Support for local copy propagation by walking the list of instructions
 * and maintaining the ACP table of available copies for propagation.
 *
 * See Muchnik's Advanced Compiler Design and Implementation, section
 * 12.5 (p356).
 */

/* Walks a basic block and does copy propagation on it using the acp
 * list.
 */
bool
fs_visitor::opt_copy_propagate_local(void *mem_ctx,
				     fs_bblock *block, exec_list *acp)
{
   bool progress = false;

   for (fs_inst *inst = block->start;
	inst != block->end->next;
	inst = (fs_inst *)inst->next) {

      /* Try propagating into this instruction. */
      foreach_list(entry_node, acp) {
	 acp_entry *entry = (acp_entry *)entry_node;

	 for (int i = 0; i < 3; i++) {
	    if (inst->src[i].file == entry->dst.file &&
		inst->src[i].reg == entry->dst.reg &&
		inst->src[i].reg_offset == entry->dst.reg_offset) {
	       inst->src[i].reg = entry->src.reg;
	       inst->src[i].reg_offset = entry->src.reg_offset;
	       progress = true;
	    }
	 }
      }

      /* kill the destination from the ACP */
      if (inst->dst.file == GRF) {
	 int start_offset = inst->dst.reg_offset;
	 int end_offset = start_offset + inst->regs_written();

	 foreach_list_safe(entry_node, acp) {
	    acp_entry *entry = (acp_entry *)entry_node;

	    if (entry->dst.file == GRF &&
		entry->dst.reg == inst->dst.reg &&
		entry->dst.reg_offset >= start_offset &&
		entry->dst.reg_offset < end_offset) {
	       entry->remove();
	       continue;
	    }
	    if (entry->src.file == GRF &&
		entry->src.reg == inst->dst.reg &&
		entry->src.reg_offset >= start_offset &&
		entry->src.reg_offset < end_offset) {
	       entry->remove();
	    }
	 }
      }

      /* If this instruction is a raw copy, add it to the ACP. */
      if (inst->opcode == BRW_OPCODE_MOV &&
	  inst->dst.file == GRF &&
	  inst->src[0].file == GRF &&
	  (inst->src[0].reg != inst->dst.reg ||
	   inst->src[0].reg_offset != inst->dst.reg_offset) &&
	  inst->src[0].type == inst->dst.type &&
	  !inst->saturate &&
	  !inst->predicated &&
	  !inst->force_uncompressed &&
	  !inst->force_sechalf &&
	  inst->src[0].smear == -1 &&
	  !inst->src[0].abs &&
	  !inst->src[0].negate) {
	 acp_entry *entry = ralloc(mem_ctx, acp_entry);
	 entry->dst = inst->dst;
	 entry->src = inst->src[0];
	 acp->push_tail(entry);
      }
   }

   return progress;
}

bool
fs_visitor::opt_copy_propagate()
{
   bool progress = false;
   void *mem_ctx = ralloc_context(this->mem_ctx);

   fs_cfg cfg(this);

   for (int b = 0; b < cfg.num_blocks; b++) {
      fs_bblock *block = cfg.blocks[b];
      exec_list acp;

      progress = opt_copy_propagate_local(mem_ctx, block, &acp) || progress;
   }

   ralloc_free(mem_ctx);

   return progress;
}

} /* namespace brw */
