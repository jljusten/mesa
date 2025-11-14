/*
 * Copyright © 2008 Keith Packard
 * Copyright © 2014-2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>
#include <vector>

#include "util/half_float.h"

#include "gen_private.h"

static const char *const writemask[16] = {
   [0x0] = ".",
   [0x1] = ".x",
   [0x2] = ".y",
   [0x3] = ".xy",
   [0x4] = ".z",
   [0x5] = ".xz",
   [0x6] = ".yz",
   [0x7] = ".xyz",
   [0x8] = ".w",
   [0x9] = ".xw",
   [0xa] = ".yw",
   [0xb] = ".xyw",
   [0xc] = ".zw",
   [0xd] = ".xzw",
   [0xe] = ".yzw",
   [0xf] = "",
};

static const char *const chan_sel[4] = {
   [0] = "x",
   [1] = "y",
   [2] = "z",
   [3] = "w",
};

struct gen_printer {
   const intel_device_info *devinfo;
   const gen_inst *inst;
   FILE *fp;
   int column;

   int x = 0;


   gen_printer(const intel_device_info *devinfo,
               const gen_print_options *opts)
      : devinfo(devinfo),
        fp(opts->fp),
        column(0) {}

   void
   print(const gen_inst *inst)
   {
      if (inst->pred_control) {
         string("(");
         string(inst->pred_inv ? "-" : "+");
         string("f");
         uint(inst->flag_nr);
         string(".");
         uint(inst->flag_subnr);

         // TODO: have a proper gen enum that's not decoding?
         if (devinfo->ver >= 20) {
         } else if (inst->align16) {
            assert(devinfo->ver == 9);
         } else {
         }
         string(") ");
      }

      string(gen_opcode_to_string(inst->opcode));

      if (inst->saturate)
         string(".sat");

      if (inst->opcode == GEN_OP_SYNC) {
         // TODO: HACK.
         string(" ");
         // TODO: Make table.
         switch ((tgl_sync_function)inst->cond_modifier) {
         case TGL_SYNC_NOP: string("nop"); break;
         case TGL_SYNC_ALLRD: string("allrd"); break;
         case TGL_SYNC_ALLWR: string("allwr"); break;
         case TGL_SYNC_FENCE: string("fence"); break;
         case TGL_SYNC_BAR: string("bar"); break;
         case TGL_SYNC_HOST: string("host"); break;
         default:
            UNREACHABLE("invalid sync function");
         }
      }

      const gen_format format = gen_inst_format(inst->opcode);
      const unsigned num_sources = gen_inst_num_sources(devinfo, inst);
      const bool has_dst = gen_inst_has_dst(format, inst->opcode);

      if (inst->cond_modifier) {
         static const char *const conditional_modifier[16] = {
            [BRW_CONDITIONAL_NONE] = "",
            [BRW_CONDITIONAL_Z]    = ".z",
            [BRW_CONDITIONAL_NZ]   = ".nz",
            [BRW_CONDITIONAL_G]    = ".g",
            [BRW_CONDITIONAL_GE]   = ".ge",
            [BRW_CONDITIONAL_L]    = ".l",
            [BRW_CONDITIONAL_LE]   = ".le",
            [BRW_CONDITIONAL_R]    = ".r",
            [BRW_CONDITIONAL_O]    = ".o",
            [BRW_CONDITIONAL_U]    = ".u",
         };
         string(conditional_modifier[inst->cond_modifier]);

         if (inst->opcode != GEN_OP_SEL &&
             inst->opcode != GEN_OP_CSEL &&
             inst->opcode != GEN_OP_IF &&
             inst->opcode != GEN_OP_WHILE) {
            string(".f");
            uint(inst->flag_nr);
            string(".");
            uint(inst->flag_subnr);
         }
      }

      if (inst->opcode != GEN_OP_NOP) {
         string("(");
         uint(inst->exec_size);
         string(")");
      }

      switch (format) {
      case GEN_FORMAT_BRANCH: {
         // TODO: Jumps with actual sources. JMPI.

         pad(16);
         string("JIP: ");
         // sint(inst->branch.jip);
         string("(");
         hex(inst->branch.jip);
         string(" "); sint(inst->branch.jip/16);
         string(")");
         // TODO: Writelabel

         if (gen_has_uip(inst->opcode)) {
            pad(38);
            string("UIP: ");
            // sint(inst->branch.uip);
            // TODO: writelable
            string("(");
            hex(inst->branch.uip);
            string(" "); sint(inst->branch.uip/16);
            string(")");
         }
         break;
      }

      case GEN_FORMAT_SEND: {
         pad(16);
         reg(inst->dst.file, inst->dst.nr);
         string("UD");

         pad(32);
         reg(inst->src[0].file, inst->src[0].nr);
         string("UD");

         if (num_sources > 1) {
            pad(48);
            reg(inst->src[1].file, inst->src[1].nr);
            string("UD");
         }

         pad(64);
         if (!inst->send.desc_is_reg) {
            hex_pad0(inst->send.desc_imm);
         } else {
            // TODO
         }

         pad(80);
         if (inst->send.ex_desc_is_reg) {
            // TODO
         } else {
            hex_pad0(inst->send.ex_desc_imm);
         }

         newline();

         pad(16);
         switch (inst->send.sfid) {
         case BRW_SFID_NULL:                  string("null");     break;
         case BRW_SFID_SAMPLER:               string("sampler");  break;
         case BRW_SFID_MESSAGE_GATEWAY:       string("gateway");  break;
         case BRW_SFID_HDC2:                  string("hdc2");     break;
         case BRW_SFID_RENDER_CACHE:          string("render");   break;
         case BRW_SFID_URB:                   string("urb");      break;
         case BRW_SFID_THREAD_SPAWNER:        string("ts/btd");   break;
         case BRW_SFID_RAY_TRACE_ACCELERATOR: string("rt accel"); break;
         case BRW_SFID_HDC_READ_ONLY:         string("hdc:ro");   break;
         case BRW_SFID_HDC0:                  string("hdc0");     break;
         case BRW_SFID_PIXEL_INTERPOLATOR:    string("pi");       break;
         case BRW_SFID_HDC1:                  string("hdc1");     break;
         case BRW_SFID_SLM:                   string("slm");      break;
         case BRW_SFID_TGM:                   string("tgm");      break;
         case BRW_SFID_UGM:                   string("ugm");      break;
         default:
            UNREACHABLE("invalid");
         }

         string(" MsgDesc:");

         break;
      }

      case GEN_FORMAT_BASIC_ONE_SRC:
      case GEN_FORMAT_BASIC_TWO_SRC:
      case GEN_FORMAT_BASIC_THREE_SRC:
      case GEN_FORMAT_DPAS_THREE_SRC: {
         if (has_dst) {
            pad(16);
            reg(inst->dst.file, inst->dst.nr);

            if (inst->dst.subnr) {
               string(".");
               uint(inst->dst.subnr / brw_type_size_bytes(inst->dst.type));
            }
            string("<");
            uint(inst->dst.region.hstride);
            string(">");
            if (inst->align16) {
               string(writemask[inst->dst.writemask]);
            }
            string(brw_reg_type_to_letters(inst->dst.type));
         }

         for (unsigned i = 0; i < num_sources; i++) {
            pad(32 + 16 * i);
            if (inst->src[i].file == GEN_IMM) {
               imm(inst->src[i]);
            } else {
               // TODO: print ~ for logical instructions?
               if (inst->src[i].negate)
                  string("-");

               if (inst->src[i].indirect) {
                  string("g[a0");
                  if (inst->src[i].subnr) {
                     string(".");
                     uint(inst->src[i].subnr);
                  }
                  if (inst->src[i].addr_imm) {
                     string(" ");
                     uint(inst->src[i].addr_imm);
                  }
                  string("]");
               } else {
                  reg(inst->src[i].file, inst->src[i].nr);

                  if (inst->src[i].subnr) {
                     string(".");
                     uint(inst->src[i].subnr / brw_type_size_bytes(inst->src[i].type));
                  }
               }

               string("<");
               // TODO: Check.
               if (inst->src[i].region.vstride == BRW_VERTICAL_STRIDE_ONE_DIMENSIONAL)
                  string("VxH");
               else
                  uint(inst->src[i].region.vstride);
               string(",");
               uint(inst->src[i].region.width);
               string(",");
               uint(inst->src[i].region.hstride);
               string(">");

               if (inst->align16) {
                  // TODO: Do better.
#define BRW_SWIZZLE4(a,b,c,d) (((a)<<0) | ((b)<<2) | ((c)<<4) | ((d)<<6))
#define BRW_GET_SWZ(swz, idx) (((swz) >> ((idx)*2)) & 0x3)
#define BRW_SWIZZLE_XYZW      BRW_SWIZZLE4(0,1,2,3)

                  const unsigned swiz = inst->src[i].swizzle;
                  const unsigned x = BRW_GET_SWZ(swiz, BRW_CHANNEL_X);
                  const unsigned y = BRW_GET_SWZ(swiz, BRW_CHANNEL_Y);
                  const unsigned z = BRW_GET_SWZ(swiz, BRW_CHANNEL_Z);
                  const unsigned w = BRW_GET_SWZ(swiz, BRW_CHANNEL_W);

                  if (x == y && x == z && x == w) {
                     string(".");
                     string(chan_sel[x]);
                  } else if (swiz != BRW_SWIZZLE_XYZW) {
                     string(".");
                     string(chan_sel[x]);
                     string(chan_sel[y]);
                     string(chan_sel[z]);
                     string(chan_sel[w]);
                  }
               }

               string(brw_reg_type_to_letters(inst->src[i].type));
            }
         }

         break;
      }

      default:
         break;
      }

      pad(64);

      if (inst->opcode != GEN_OP_NOP) {
         string("{ ");
         string(inst->align16 ? "align16 " : "align1 ");
         string(inst->no_mask ? "WE_all " : "");

         // TODO: dd check/clear

         // TODO: Use Mxx notation.

         if (inst->exec_size < 8 || (inst->chan_offset % 8) != 0) {
            uint((inst->chan_offset / 4) + 1);
            string("N ");
         } else if (inst->exec_size == 8) {
            uint((inst->chan_offset / 8) + 1);
            string("Q ");
         } else if (inst->exec_size == 16) {
            uint((inst->chan_offset / 16) + 1);
            string("H ");
         }

         if (devinfo->ver >= 12) {
            if (inst->swsb.regdist) {
               tgl_pipe p = inst->swsb.pipe;
               string(p == TGL_PIPE_FLOAT  ? "F@" :
                      p == TGL_PIPE_INT    ? "I@" :
                      p == TGL_PIPE_LONG   ? "L@" :
                      p == TGL_PIPE_ALL    ? "A@" :
                      p == TGL_PIPE_MATH   ? "M@" :
                      p == TGL_PIPE_SCALAR ? "S@" :
                      "@");
               uint(inst->swsb.regdist);
               string(" ");
            }
            if (inst->swsb.mode) {
               string("$");
               uint(inst->swsb.sbid);
               string(inst->swsb.mode & TGL_SBID_SET ? " " :
                      inst->swsb.mode & TGL_SBID_DST ? ".dst " : ".src ");
            }
         }

         // TODO: should we have a "was compacted field"?

         if (format == GEN_FORMAT_SEND && inst->send.eot)
            string("EOT ");


         string("};");
      }

      // pad(128);
      // hex(x++ * 16);
      // string(" ");

      newline();
   }

private:
   // TODO: Take per-instruction annotations (for validation errors).

   void
   imm(const gen_operand &src)
   {
      switch (src.type) {
      case BRW_TYPE_UQ:
         column += fprintf(fp, "0x%016" PRIx64 "UQ", src.imm);
         break;
      case BRW_TYPE_Q:
         column += fprintf(fp, "0x%016" PRIx64 "Q", src.imm);
         break;
      case BRW_TYPE_UD:
         column += fprintf(fp, "0x%08xUD", (uint32_t)src.imm);
         break;
      case BRW_TYPE_D:
         column += fprintf(fp, "%dD", (int32_t)src.imm);
         break;
      case BRW_TYPE_UW:
         column += fprintf(fp, "0x%04xUW", (uint16_t)src.imm);
         break;
      case BRW_TYPE_W:
         column += fprintf(fp, "%dW", (int16_t)src.imm);
         break;
      case BRW_TYPE_UV:
         column += fprintf(fp, "0x%08xUV", (uint32_t)src.imm);
         break;
      case BRW_TYPE_VF:
         assert(!"TODO: Print TYPE VF");
         break;
      case BRW_TYPE_V:
         column += fprintf(fp, "0x%08xV", (uint32_t)src.imm);
         break;
      case BRW_TYPE_F: {
         union {
            float f;
            uint32_t u;
         } ft;
         ft.u = src.imm;
         column += fprintf(fp, "0x%08xF /* %-gF */", ft.u, ft.f);
         break;
      }
      case BRW_TYPE_DF: {
         union {
            double f;
            uint64_t u;
         } ft;
         ft.u = src.imm;
         column += fprintf(fp, "0x%016" PRIx64 "DF /* %-gDF */", ft.u, ft.f);
         break;
      }
      case BRW_TYPE_HF: {
         column += fprintf(fp, "0x%04xHF /* %-gHF */",
                           (uint16_t)src.imm, _mesa_half_to_float((uint16_t)src.imm));
         break;
      }
      case BRW_TYPE_UB:
      case BRW_TYPE_B:
      default:
         UNREACHABLE("invalid");
      }
   }

   void
   reg(gen_file file, unsigned nr)
   {
      if (file == GEN_ARF) {
         switch (nr & 0xf0) {
         case BRW_ARF_NULL:               string("null"); return;
         case BRW_ARF_IP:                 string("ip");   return;
         case BRW_ARF_TDR:                string("tdr0"); return;

         case BRW_ARF_ADDRESS:            string("a");    break;
         case BRW_ARF_ACCUMULATOR:        string("acc");  break;
         case BRW_ARF_FLAG:               string("f");    break;
         case BRW_ARF_MASK:               string("mask"); break;
         case BRW_ARF_STATE:              string("sr");   break;
         case BRW_ARF_SCALAR:             string("s");    break;
         case BRW_ARF_CONTROL:            string("cr");   break;
         case BRW_ARF_NOTIFICATION_COUNT: string("n");    break;
         case BRW_ARF_TIMESTAMP:          string("tm");   break;
         default:
            string("ARF");
            uint(nr);
            return;
         };

         uint(nr & 0x0f);

      } else {
         assert(file == GEN_GRF);
         // TODO: r!!!
         string("g");
         uint(nr);
      }
   }

   void
   uint(unsigned n)
   {
      column += fprintf(fp, "%u", n);
   }

   void
   sint(int n)
   {
      column += fprintf(fp, "%d", n);
   }

   void
   hex_pad0(unsigned n)
   {
      column += fprintf(fp, "0x%08x", n);
   }

   void
   hex(unsigned n)
   {
      column += fprintf(fp, "0x%x", n);
   }

   void
   string(const char *s)
   {
      fputs(s, fp);
      column += strlen(s);
   }

   void
   pad(int c)
   {
      do
         string(" ");
      while (column < c);
   }

   void
   newline() {
      fprintf(fp, "\n");
      column = 0;
   }
};

const char *
gen_opcode_to_string(gen_opcode op)
{
   // TODO: Double check table will be created.
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
   };
}

void
gen_print(const intel_device_info *devinfo,
          gen_inst **insts, int num_insts,
          gen_print_options opts)
{
   if (!opts.fp) opts.fp = stderr;

   auto p = gen_printer(devinfo, &opts);

   int next_error = 0;

   // TODO: Make a proper function that identify the blocks, labels, CFG.

   std::vector<int> labels;
   for (int i = 0; i < num_insts; i++) {
      // TODO: Check inst format coolllect IPs and sort.
   }



   std::vector<std::pair<int, int>> blocks;
   blocks.push_back({0, 0});

   for (int i = 0; i < num_insts; i++) {
      auto inst = insts[i];
      if (inst->opcode == GEN_OP_IF ||
          inst->opcode == GEN_OP_ELSE ||
          inst->opcode == GEN_OP_BREAK ||
          inst->opcode == GEN_OP_WHILE ||
          inst->opcode == GEN_OP_CONTINUE ||
          inst->opcode == GEN_OP_HALT) {
         /* End of a block. */
         blocks.back().second = i+1;
         blocks.push_back({i+1, 0});
      }
      if (inst->opcode == GEN_OP_ENDIF) {
         blocks.back().second = i;
         blocks.push_back({i, 0});
      }
   }
   blocks.back().second = num_insts;

   int cur_block = 0;

   for (int i = 0; i < num_insts; i++) {
      // if (blocks[cur_block].first == i) {
      //    fprintf(opts.fp, "   START B%d\n", cur_block);
      // }

      p.print(insts[i]);
      while (next_error < opts.num_errors &&
             opts.errors[next_error].offset == i) {
         fprintf(opts.fp, "    ERROR: %s\n", opts.errors[next_error].msg);
         next_error++;
      }

      // if (blocks[cur_block].second == i+1) {
      //    fprintf(opts.fp, "   END B%d\n", cur_block);
      //    cur_block++;
      // }
   }
}

