/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "gen_private.h"

#include <algorithm>
#include <string>
#include <vector>

// TODO: Avoid duplicate errors (consider offset!).
// TODO: Make this a local function.
// TODO: Add mem_ctx so we can have "formatted" errors.

#define ERROR(msg) ERROR_IF(true, msg)
#define ERROR_IF(cond, msg)                             \
   do {                                                 \
      if ((cond))                                       \
         errors.push_back({offset, msg});               \
   } while(0)

struct gen_validator {
   const intel_device_info *devinfo;
   std::vector<gen_error> &errors;

   const gen_inst *inst;
   unsigned offset;
   gen_format format;
   unsigned num_sources;

   gen_validator(const intel_device_info *devinfo, std::vector<gen_error> &errors)
      : devinfo(devinfo),
        errors(errors)
   {}

   bool
   validate(const gen_inst *inst, unsigned offset)
   {
      // TODO: Do better.
      unsigned old_errors = errors.size();

      this->inst = inst;
      this->offset = offset;
      this->format = gen_inst_format(inst->opcode);
      this->num_sources = gen_inst_num_sources(devinfo, inst);

      opcodes();
      if (old_errors != errors.size())
         goto end;

      invalid_values();
      branch_restrictions();
      sources_not_null();
      send_restrictions();

   // TODO(general_restrictions_based_on_operand_types);
   // TODO(general_restrictions_on_region_parameters);
   // TODO(special_restrictions_for_mixed_float_mode);
   // TODO(region_alignment_rules);
   // TODO(vector_immediate_restrictions);
   // TODO(special_requirements_for_handling_double_precision_data_types);
   // TODO(instruction_restrictions);
   // TODO(send_descriptor_restrictions);
   // TODO(register_region_special_restrictions);
   // TODO(scalar_register_restrictions);

   end:
      return old_errors == errors.size();
   }

private:
   static bool
   is_register_file(gen_file file)
   {
      return file == GEN_GRF || file == GEN_ARF;
   }

   static bool
   is_32bit_integer_operand(const gen_operand &op)
   {
      return gen_type_is_int(op.type) && gen_type_size_bytes(op.type) == 4;
   }

   bool
   xe2_swsb_is_encodable() const
   {
      const gen_swsb &swsb = inst->swsb;

      if (!swsb.mode || !swsb.regdist)
         return true;

      if (inst->opcode == GEN_OP_DPAS)
         return swsb.pipe == GEN_PIPE_NONE;

      if (swsb.mode & GEN_SBID_SET)
         return (inst->opcode == GEN_OP_SEND || inst->opcode == GEN_OP_SENDC) &&
                (swsb.pipe == GEN_PIPE_ALL ||
                 swsb.pipe == GEN_PIPE_INT ||
                 swsb.pipe == GEN_PIPE_FLOAT);

      if (inst->opcode == GEN_OP_SEND || inst->opcode == GEN_OP_SENDC)
         return false;

      return swsb.pipe == GEN_PIPE_NONE ||
             (swsb.pipe == GEN_PIPE_ALL && swsb.mode == GEN_SBID_DST);
   }

   void
   opcodes()
   {
      if (devinfo->ver != 9) {
         ERROR_IF(inst->opcode == GEN_OP_DP2,  "DP2 is Gfx9 only.");
         ERROR_IF(inst->opcode == GEN_OP_DP3,  "DP3 is Gfx9 only.");
         ERROR_IF(inst->opcode == GEN_OP_DP4,  "DP4 is Gfx9 only.");
         ERROR_IF(inst->opcode == GEN_OP_DPH,  "DPH is Gfx9 only.");
         ERROR_IF(inst->opcode == GEN_OP_LINE, "LINE is Gfx9 only.");
         ERROR_IF(inst->opcode == GEN_OP_LRP,  "LRP is Gfx9 only.");
         ERROR_IF(inst->opcode == GEN_OP_PLN,  "PLN is Gfx9 only.");
      }

      if (devinfo->ver > 12) {
         ERROR_IF(inst->opcode == GEN_OP_SENDS,  "SENDS is Gfx9 and Gfx11 only.");
         ERROR_IF(inst->opcode == GEN_OP_SENDSC, "SENDSC is Gfx9 and Gfx11 only.");
         ERROR_IF(inst->opcode == GEN_OP_WAIT,   "WAIT is Gfx9 and Gfx11 only.");
      }

      if (devinfo->ver < 11) {
         ERROR_IF(inst->opcode == GEN_OP_ROL, "ROL is Gfx11+ only.");
         ERROR_IF(inst->opcode == GEN_OP_ROR, "ROR is Gfx11+ only.");
      }

      if (devinfo->ver < 12) {
         ERROR_IF(inst->opcode == GEN_OP_ADD3, "ADD3 is Gfx12+ only.");
         ERROR_IF(inst->opcode == GEN_OP_BFN,  "BFN is Gfx12+ only.");
         ERROR_IF(inst->opcode == GEN_OP_DP4A, "DP4A is Gfx12+ only.");
         ERROR_IF(inst->opcode == GEN_OP_SYNC, "SYNC is Gfx12+ only.");
      }

      if (devinfo->verx10 < 125) {
         ERROR_IF(inst->opcode == GEN_OP_DPAS, "DPAS is Gfx12.5+ only.");
      }

      if (devinfo->ver < 20) {
         ERROR_IF(inst->opcode == GEN_OP_SRND, "SRND is Gfx20+ only.");
      }
   }

   void
   invalid_values()
   {
      if (devinfo->ver >= 20) {
         ERROR_IF(inst->chan_offset % 8 != 0,
                  "Channel offset must be a multiple of 8 for Gfx20+");
         ERROR_IF(!xe2_swsb_is_encodable(),
                  "Invalid Xe2+ tuple SWSB encoding for this opcode");
      } else {
         ERROR_IF(inst->chan_offset % 4 != 0,
                  "Channel offset must be a multiple of 4");
      }

      if (devinfo->ver >= 12) {
         ERROR_IF(inst->exec_size == 0 ||
                  inst->chan_offset % inst->exec_size != 0,
                  "The execution size must be a factor of the chosen offset");
      }

      ERROR_IF(devinfo->ver != 12 && inst->fusion_control,
               "Fusion control bit only used for Gfx12.");

      // TODO: Check for fusion control on non-sends.

      // TODO: Find a better place.
      if (inst->opcode == GEN_OP_DPAS) {
         ERROR_IF(inst->src[0].file != GEN_GRF && inst->src[0].file != GEN_ARF,
                  "DPAS currently only supports GRF or GEN_ARF for Source 0.");
         ERROR_IF(inst->src[1].file != GEN_GRF,
                  "DPAS currently only supports GRF for Source 1");
         ERROR_IF(inst->src[2].file != GEN_GRF,
                  "DPAS currently only supports GRF for Source 2");
      }


      // TODO: Either generator or test producing this..

      // ERROR_IF(devinfo->ver >= 20 && inst->acc_wr_control,
      //          "AccWrControl not present on Gfx20+");

      // TODO: Move to a better place.
      if (inst->opcode == GEN_OP_BFN) {
         ERROR_IF(inst->cmod != GEN_CONDITION_NONE &&
                  inst->cmod != GEN_CONDITION_Z &&
                  inst->cmod != GEN_CONDITION_G &&
                  inst->cmod != GEN_CONDITION_L,
                  "BFN supports only Z, G, L or none conditional modifiers.");
      }

   }

   void
   branch_restrictions()
   {
      switch (inst->opcode) {
      case GEN_OP_JMPI:
      case GEN_OP_BRD:
         ERROR_IF(num_sources != 1,
                  "JMPI and BRD require exactly one source operand.");
         ERROR_IF(!is_register_file(inst->src[0].file) &&
                  inst->src[0].file != GEN_IMM,
                  "JMPI and BRD source must be a register or immediate.");
         ERROR_IF(inst->src[1].file != GEN_BAD_FILE,
                  "JMPI and BRD use only src0 in the gen instruction model.");
         break;

      case GEN_OP_BRC:
         ERROR_IF(num_sources != 1 && num_sources != 2,
                  "BRC requires either one register source or two immediate sources.");

         if (num_sources == 1) {
            ERROR_IF(!is_register_file(inst->src[0].file),
                     "BRC register form requires src0 to be a register.");
            ERROR_IF(inst->src[1].file != GEN_BAD_FILE,
                     "BRC register form uses only src0 in the gen instruction model.");
            ERROR_IF(!is_32bit_integer_operand(inst->src[0]),
                     "BRC register form requires a paired DWord integer source.");
            ERROR_IF(inst->src[0].region.vstride != 2 ||
                     inst->src[0].region.width != 2 ||
                     inst->src[0].region.hstride != 1,
                     "BRC register form requires src0 region <2;2,1>.");
         } else {
            ERROR_IF(inst->src[0].file != GEN_IMM || inst->src[1].file != GEN_IMM,
                     "BRC immediate form requires src0 and src1 to be immediates.");
            ERROR_IF(!is_32bit_integer_operand(inst->src[0]) ||
                     !is_32bit_integer_operand(inst->src[1]),
                     "BRC immediate form requires 32-bit integer immediates.");
         }
         break;

      default:
         break;
      }
   }

   void
   sources_not_null()
   {
      /* Nothing to test. 3-src instructions can only have GRF sources, and
       * there's no bit to control the file.
       */
      if (num_sources == 3)
         return;

      /* Nothing to test.  Split sends can only encode a file in sources that are
       * allowed to be NULL.
       */
      if (gen_inst_is_split_send(devinfo, inst))
         return;

      if (num_sources >= 1 && inst->opcode != GEN_OP_SYNC)
         ERROR_IF(is_null(inst->src[0]), "src0 is null");

      if (num_sources == 2 && inst->opcode != GEN_OP_MATH)
         ERROR_IF(is_null(inst->src[1]), "src1 is null");
   }

   void
   send_restrictions()
   {
      if (gen_inst_is_split_send(devinfo, inst)) {
         ERROR_IF(inst->src[1].file == GEN_ARF &&
                  inst->src[1].nr != GEN_ARF_NULL,
                  "src1 of split send must be a GRF or NULL");

         if (devinfo->ver < 30) {
            ERROR_IF(inst->send.eot &&
                     inst->src[0].nr < 112,
                     "send with EOT must use g112-g127");
            ERROR_IF(inst->send.eot &&
                     inst->src[1].file == GEN_GRF &&
                     inst->src[1].nr < 112,
                     "send with EOT must use g112-g127");
         }

         // if (inst->src[0].file == GEN_GRF && inst->src[1].file == GEN_GRF) {
         //    /* Assume minimums if we don't know */
         //    unsigned mlen = 1;
         //    if (!inst->send.desc_is_reg) {
         //       const uint32_t desc = inst->send.desc_imm;
         //       mlen = brw_message_desc_mlen(devinfo, desc) / reg_unit(devinfo);
         //    }
         //
         //    unsigned ex_mlen = 1;
         //    if (!inst->send.ex_desc_is_reg) {
         //       const uint32_t ex_desc = inst->send.ex_desc_imm;
         //       ex_mlen = brw_message_ex_desc_ex_mlen(devinfo, ex_desc) /
         //                 reg_unit(devinfo);
         //    }
         //    const unsigned src0_reg_nr = inst->src[0].nr;
         //    const unsigned src1_reg_nr = inst->src[1].nr;
         //    ERROR_IF((src0_reg_nr <= src1_reg_nr &&
         //              src1_reg_nr < src0_reg_nr + mlen) ||
         //             (src1_reg_nr <= src0_reg_nr &&
         //              src0_reg_nr < src1_reg_nr + ex_mlen),
         //              "split send payloads must not overlap");
         // }
      } else if (gen_inst_is_send(inst)) {
         const bool scalar_gather =
            devinfo->ver >= 30 &&
            inst->src[0].file == GEN_ARF &&
            (inst->src[0].nr & 0xf0) == GEN_ARF_SCALAR;

         ERROR_IF(inst->src[0].indirect,
                  "send must use direct addressing");

         ERROR_IF(!scalar_gather && inst->src[0].file != GEN_GRF,
                  "send from non-GRF");
         ERROR_IF(scalar_gather && (inst->src[0].subnr & 1),
                  "scalar gather send requires an even scalar subregister");
         ERROR_IF(inst->send.eot &&
                  inst->src[0].nr < 112,
                  "send with EOT must use g112-g127");

         if (devinfo->ver == 9) {
            const unsigned rlen = (inst->send.desc_imm >> 20) & 0x1F;
            const unsigned mlen = (inst->send.desc_imm >> 25) & 0xF;
            ERROR_IF(!is_null(inst->dst) &&
                     (inst->dst.nr + rlen > 127) &&
                     (inst->src[0].nr + mlen > inst->dst.nr),
                     "r127 must not be used for return address when there is "
                     "a src and dest overlap");
         }
      }
   }
};

bool
gen_validate(gen_validate_params *params)
{
   assert(params->devinfo);
   assert(params->mem_ctx);
   assert(params->errors == NULL);
   assert(params->insts);

   const intel_device_info *devinfo = params->devinfo;

   if (params->num_insts == 0)
      return true;

   std::vector<std::string> local_errors;

   // TODO: We can do better.
   std::vector<gen_error> errors;

   auto v = gen_validator(devinfo, errors);

   for (int i = 0; i < params->num_insts; i++) {
      const gen_inst *inst = params->insts[i];
      v.validate(inst, i);
   }

   const bool valid = errors.empty();

   if (!valid) {
      // TODO: Do better by already building up errors with ralloc array in
      // the first place...
      params->errors = ralloc_array(params->mem_ctx, gen_error, errors.size());
      params->num_errors = errors.size();

      std::copy_n(errors.begin(), errors.size(), params->errors);

      for (unsigned i = 0; i < errors.size(); i++) {
         gen_error *err = &params->errors[i];
         err->offset = errors[i].offset;
         err->msg    = ralloc_strdup(params->mem_ctx, errors[i].msg);
      }
   }

   return valid;
}
