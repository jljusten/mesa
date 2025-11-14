/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "util/ralloc.h"

#include "dev/intel_device_info.h"
#include "gen.h"

enum gen_print_mode {
   GEN_PRINT_MODE_DEFAULT = 0,
   GEN_PRINT_MODE_CONCISE,
};

enum gen_print_send_syntax {
   GEN_PRINT_SEND_SYNTAX_RAW = 0,
   GEN_PRINT_SEND_SYNTAX_TRANSLATED,
};

struct gen_print_options {
   FILE *fp = nullptr;
   gen_print_flags flags = GEN_PRINT_NONE;
   gen_print_mode mode = GEN_PRINT_MODE_DEFAULT;
   gen_print_send_syntax send_syntax = GEN_PRINT_SEND_SYNTAX_RAW;
};

namespace {

enum {
   GEN_CHANNEL_X = 0,
   GEN_CHANNEL_Y = 1,
   GEN_CHANNEL_Z = 2,
   GEN_CHANNEL_W = 3,
};

static inline uint32_t
gen_message_desc(const intel_device_info *devinfo,
                 unsigned msg_length,
                 unsigned response_length,
                 bool header_present)
{
   (void)devinfo;
   return SET_BITS(msg_length, 28, 25) |
          SET_BITS(response_length, 24, 20) |
          SET_BITS(header_present, 19, 19);
}

static inline unsigned
gen_lsc_msg_dest_len(const struct intel_device_info *devinfo,
                     enum lsc_data_size data_sz,
                     unsigned n)
{
   return DIV_ROUND_UP(lsc_data_size_bytes(data_sz) * n,
                       devinfo->grf_size);
}

static inline unsigned
gen_lsc_msg_addr_len(const struct intel_device_info *devinfo,
                     enum lsc_addr_size addr_sz,
                     unsigned n)
{
   return DIV_ROUND_UP(lsc_addr_size_bytes(addr_sz) * n,
                       devinfo->grf_size);
}

static inline uint32_t
gen_lsc_msg_desc(const intel_device_info *devinfo,
                 enum lsc_opcode op,
                 enum lsc_addr_surface_type addr_type,
                 enum lsc_addr_size addr_size,
                 enum lsc_data_size data_size,
                 unsigned num_values_or_cmask,
                 bool transpose,
                 unsigned cache_ctrl)
{
   return lsc_msg_desc(devinfo, op, addr_type, addr_size, data_size,
                       num_values_or_cmask, transpose, cache_ctrl);
}

static inline uint32_t
gen_lsc_fence_msg_desc(const intel_device_info *devinfo,
                       enum lsc_fence_scope scope,
                       enum lsc_flush_type flush_type,
                       bool route_to_lsc)
{
   return lsc_fence_msg_desc(devinfo, scope, flush_type, route_to_lsc);
}

static inline uint32_t
gen_lsc_bti_ex_desc(const struct intel_device_info *devinfo,
                    unsigned index,
                    unsigned base_offset)
{
   gen_lsc_ex_desc ex_desc = {};
   ex_desc.addr_type = LSC_ADDR_SURFTYPE_BTI;
   ex_desc.bti.index = index;
   ex_desc.bti.base_offset = base_offset;
   return gen_lsc_ex_desc_encode(devinfo, &ex_desc);
}

static void
gen_print_compat(const intel_device_info *devinfo,
                 gen_inst **insts,
                 int num_insts,
                 gen_print_options opts = {})
{
   gen_print_params params = {};
   params.devinfo = devinfo;
   params.fp = opts.fp;
   params.flags = opts.flags;
   if (opts.send_syntax == GEN_PRINT_SEND_SYNTAX_TRANSLATED) {
      params.flags = (gen_print_flags)(params.flags | GEN_PRINT_TRANSLATED_SENDS);
   }
   params.insts = insts;
   params.num_insts = num_insts;
   gen_print(&params);
}

constexpr unsigned
swizzle4(unsigned x, unsigned y, unsigned z, unsigned w)
{
   return (x << 0) | (y << 2) | (z << 4) | (w << 6);
}

static uint32_t
make_lsc_desc(const intel_device_info *devinfo,
              unsigned mlen, unsigned rlen,
              enum lsc_opcode op,
              enum lsc_addr_surface_type addr_type,
              enum lsc_addr_size addr_size,
              enum lsc_data_size data_size,
              unsigned num_values_or_cmask,
              bool transpose,
              unsigned cache_ctrl)
{
   return gen_message_desc(devinfo, mlen, rlen, false) |
          gen_lsc_msg_desc(devinfo, op, addr_type, addr_size, data_size,
                       num_values_or_cmask, transpose, cache_ctrl);
}

struct gen_parse_inst_vector {
   std::vector<gen_inst *> v;

   ~gen_parse_inst_vector()
   {
      for (gen_inst *inst : v)
         delete inst;
   }

   gen_inst *append(gen_opcode op)
   {
      gen_inst *inst = new gen_inst;
      memset(inst, 0, sizeof(*inst));
      inst->opcode = op;
      v.push_back(inst);
      return inst;
   }

   gen_inst **data() { return v.data(); }
   int size() const { return v.size(); }
};

struct parsed_program {
   void *mem_ctx = NULL;
   gen_inst **insts = NULL;
   int num_insts = 0;
   gen_error *errors = NULL;
   int num_errors = 0;

   ~parsed_program()
   {
      if (mem_ctx)
         ralloc_free(mem_ctx);
   }

   parsed_program() = default;
   parsed_program(const parsed_program &) = delete;
   parsed_program &operator=(const parsed_program &) = delete;
};

static intel_device_info
get_devinfo(const char *name)
{
   intel_device_info devinfo;
   memset(&devinfo, 0, sizeof(devinfo));

   const int devid = intel_device_name_to_pci_device_id(name);
   EXPECT_NE(devid, -1);
   EXPECT_TRUE(intel_get_device_info_from_pci_id(devid, &devinfo));
   return devinfo;
}

static std::string
print_program(const intel_device_info *devinfo, gen_inst **insts, int num_insts,
              gen_print_options opts = {})
{
   char *str = NULL;
   size_t size = 0;
   FILE *fp = open_memstream(&str, &size);

   opts.fp = fp;
   gen_print_compat(devinfo, insts, num_insts, opts);
   fclose(fp);

   std::string out(str ? str : "");
   free(str);
   return out;
}

static std::string
print_program(const intel_device_info *devinfo, gen_parse_inst_vector &insts,
              gen_print_options opts = {})
{
   return print_program(devinfo, insts.data(), insts.size(), opts);
}

static bool
parse_program(const intel_device_info *devinfo, const std::string &text,
              parsed_program &out)
{
   out.mem_ctx = ralloc_context(NULL);

   gen_parse_params params = {};
   params.devinfo = devinfo;
   params.text = text.c_str();
   params.text_size = text.size();
   params.mem_ctx = out.mem_ctx;

   const bool ok = gen_parse(&params);
   out.insts = params.insts;
   out.num_insts = params.num_insts;
   out.errors = params.errors;
   out.num_errors = params.num_errors;
   return ok;
}

static bool
parse_program(const intel_device_info *devinfo, const char *text, int text_size,
              parsed_program &out)
{
   out.mem_ctx = ralloc_context(NULL);

   gen_parse_params params = {};
   params.devinfo = devinfo;
   params.text = text;
   params.text_size = text_size;
   params.mem_ctx = out.mem_ctx;

   const bool ok = gen_parse(&params);
   out.insts = params.insts;
   out.num_insts = params.num_insts;
   out.errors = params.errors;
   out.num_errors = params.num_errors;
   return ok;
}

static std::string
first_error(const parsed_program &p)
{
   if (!p.num_errors)
      return "";

   return std::to_string(p.errors[0].index) + ": " + p.errors[0].msg;
}

static bool
encode_program(const intel_device_info *devinfo, const parsed_program &parsed,
               gen_encode_params &params)
{
   std::vector<const gen_inst *> insts(parsed.insts,
                                       parsed.insts + parsed.num_insts);

   const int raw_bytes_size = parsed.num_insts > 0 ?
      parsed.num_insts * (int)sizeof(gen_raw_inst) : 1;

   params = {};
   params.devinfo = devinfo;
   params.insts = insts.data();
   params.num_insts = parsed.num_insts;
   params.mem_ctx = parsed.mem_ctx;
   params.raw_bytes = ralloc_size(parsed.mem_ctx, raw_bytes_size);
   params.raw_bytes_size = raw_bytes_size;
   return gen_encode(&params);
}

TEST(GenParse, RoundTripsCanonicalInstructionStrings)
{
   struct test_case {
      const char *name;
      const char *devinfo_name;
      const char *text;
      gen_print_options print_opts;
      int expected_num_insts = 1;
   };

   const test_case cases[] = {
      {
         "prefix_and_types",
         "tgl",
         "(W&~f0.0) add (8|M0)              r1.0<1>:f     r2.0<8;8,1>:f     0x3f800000:f\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "conditional_modifier",
         "tgl",
         "        cmp (16|M0)    (ge)f3.1   r5.0<1>:ud    r6.0<8;8,1>:ud    r7.0<8;8,1>:ud\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "exec_control_channel_offset",
         "tgl",
         "        mad (8|M24)               r10.4<1>:f    r11.0<0;0>:f      r12.4<1;0>:f      r13.8<1>:f\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "three_source_mad",
         "tgl",
         "        mad (32|M0)               r70.0<1>:f    r2.8<0;0>:f       r24.0<1;0>:f      r2.10<0>:f\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "default_binary",
         "tgl",
         "        add (8)                   r5            r6                0x0000002a\n",
         {},
      },
      {
         "default_three_source",
         "tgl",
         "        add3 (16)                 r3:d          r19.1<0>:d        r21:d             r2<1>:d\n",
         {},
      },
      {
         "default_uniform_region",
         "tgl",
         "        mov (8)                   r1:f          r2<0>:f\n",
         {},
      },
      {
         "default_nonzero_channel_offset",
         "tgl",
         "        mov (8|M24)               r1            r2\n",
         {},
      },
      {
         "default_branch_offsets",
         "tgl",
         "        goto (16)                             jip:0x20            uip:0x20\n",
         {},
      },
      {
         "branch_control_with_label",
         "tgl",
         "        goto.b (16|M0)                        jip:L0              uip:L0\n"
         "L0:\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "instruction_options",
         "tgl",
         "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f                     {AccWrEn,Breakpoint,NoDDChk,NoDDClr}\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "align16",
         "tgl",
         "        mov (8|M0)                r1.0<1>:f     r2.0<4;4,1>:f                     {Align16}\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "align16_rep_ctrl",
         "skl",
         "        mad (8|M0)                r1.0<1>:f     r2.0<0;0>.r:f     r3.0<0;0>.r:f     r4.0<0>.r:f {Align16}\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "atomic_option",
         "tgl",
         "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f                     {Atomic}\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "switch_option",
         "tgl",
         "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f                     {Switch}\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "raw_send_with_swsb",
         "tgl",
         "        send.ugm (1|M0)           r40     r35:1  null    a0.2        0x2229E500   {I@2,$5}\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "raw_send_exbso",
         "mtl",
         "        send.ugm (8|M0)           r40     r35:2  r8:3    a0.2        0x04100000   {ExBSO}\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "raw_send_ex_desc_imm_offset",
         "bmg",
         "        send.ugm (8|M0)           r40     r35:1  null    0x00000100:a0.2 0x02100000\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "serialize_option",
         "tgl",
         "        send.ugm (8|M0)           r40     r35:1  null    a0.2        0x02100000   {Serialize}\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "canonical_send_eot",
         "skl",
         "(W)     send.ts (8|M0)            null    r127:1         0x00000000  0x02000010   {EOT}\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "lsc_ugm_symbolic",
         "bmg",
         "        load.ugm.d32x32t.a32.ca.cc.bss[a0.0] (1|M0) r6 r5:1\n",
         { .flags = (gen_print_flags)(GEN_PRINT_VERBOSE | GEN_PRINT_TRANSLATED_SENDS) },
      },
      {
         "lsc_ugm_raw_with_annotation",
         "bmg",
         "        send.ugm (1|M0)           r6      r5:1   null    a0.0        0x2229E500  // load.ugm.d32x32t.a32.ca.cc.bss[a0.0]\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "lsc_slm_symbolic",
         "mtl",
         "        load.slm.d32x4.a32 (16|M0) r10    r8:1\n",
         { .flags = (gen_print_flags)(GEN_PRINT_VERBOSE | GEN_PRINT_TRANSLATED_SENDS) },
      },
      {
         "lsc_tgm_symbolic",
         "mtl",
         "        load_cmask.tgm.d32.xy.a32.ca.ca.bti[3] (8|M0) r20 r12:4\n",
         { .flags = (gen_print_flags)(GEN_PRINT_VERBOSE | GEN_PRINT_TRANSLATED_SENDS) },
      },
      {
         "lsc_atomic_symbolic",
         "mtl",
         "        atomic_add.ugm.d32.a32.wt.wb.bss[a0.0] (8|M0) r40 r35:1 r8\n",
         { .flags = (gen_print_flags)(GEN_PRINT_VERBOSE | GEN_PRINT_TRANSLATED_SENDS) },
      },
      {
         "lsc_fence_symbolic",
         "mtl",
         "        fence.ugm.gpu.evict.route_to_lsc (8|M0) r2 r0:1\n",
         { .flags = (gen_print_flags)(GEN_PRINT_VERBOSE | GEN_PRINT_TRANSLATED_SENDS) },
      },
      {
         "dpas_function_control",
         "mtl",
         "        dpas.8x4 (8|M0)           r1.0<1>:d     r2.0<8;8,1>:d     r3.0<8;8,1>:hf    r4.0<8;8,1>:hf\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "bfn_function_control",
         "tgl",
         "        bfn.0x9a (8|M0)           r1.0<1>:ud    r2.0<1;0>:ud      r3.0<1;0>:ud      r4.0<1>:ud\n",
         { .flags = GEN_PRINT_VERBOSE },
      },
      {
         "synthetic_labels_program",
         "tgl",
         "        goto (16|M0)                          jip:L0              uip:L0\n"
         "        nop\n"
         "\n"
         "L0:\n"
         "        nop\n",
         { .flags = GEN_PRINT_VERBOSE },
         3,
      },
   };

   for (const test_case &tc : cases) {
      SCOPED_TRACE(::testing::Message()
                   << "case=" << tc.name
                   << " devinfo=" << tc.devinfo_name
                   << " text=" << tc.text);

      intel_device_info devinfo = get_devinfo(tc.devinfo_name);
      parsed_program parsed;

      const bool ok = parse_program(&devinfo, tc.text, parsed);
      EXPECT_TRUE(ok)
         << "case=" << tc.name
         << " devinfo=" << tc.devinfo_name
         << "\noriginal:\n" << tc.text
         << "\nparse error:\n" << first_error(parsed);
      if (!ok)
         continue;

      EXPECT_EQ(parsed.num_insts, tc.expected_num_insts)
         << "case=" << tc.name
         << " devinfo=" << tc.devinfo_name
         << "\noriginal:\n" << tc.text
         << "\nexpected_num_insts=" << tc.expected_num_insts
         << " actual_num_insts=" << parsed.num_insts;
      const std::string reprinted =
         print_program(&devinfo, parsed.insts, parsed.num_insts, tc.print_opts);
      EXPECT_EQ(reprinted, tc.text)
         << "case=" << tc.name
         << " devinfo=" << tc.devinfo_name
         << "\noriginal:\n" << tc.text
         << "\nreprinted:\n" << reprinted;
   }
}

TEST(GenParse, RoundTripDefaultPrinterOutput)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_parse_inst_vector insts;

   gen_inst *cmp = insts.append(GEN_OP_CMP);
   cmp->exec_size = 16;
   cmp->no_mask = true;
   cmp->pred_control = GEN_PREDICATE_NORMAL;
   cmp->pred_inv = true;
   cmp->cmod = GEN_CONDITION_GE;
   cmp->flag_nr = 3;
   cmp->flag_subnr = 1;
   cmp->dst.file = GEN_GRF;
   cmp->dst.nr = 5;
   cmp->dst.type = GEN_TYPE_UD;
   cmp->dst.region.hstride = 1;
   cmp->src[0].file = GEN_GRF;
   cmp->src[0].nr = 6;
   cmp->src[0].type = GEN_TYPE_UD;
   cmp->src[0].region = { 8, 8, 1 };
   cmp->src[1].file = GEN_GRF;
   cmp->src[1].nr = 7;
   cmp->src[1].type = GEN_TYPE_UD;
   cmp->src[1].region = { 8, 8, 1 };

   gen_inst *mov = insts.append(GEN_OP_MOV);
   mov->exec_size = 8;
   mov->align16 = true;
   mov->dst.file = GEN_GRF;
   mov->dst.nr = 1;
   mov->dst.type = GEN_TYPE_F;
   mov->dst.region.hstride = 1;
   mov->dst.writemask = 0x3;
   mov->src[0].file = GEN_GRF;
   mov->src[0].nr = 2;
   mov->src[0].type = GEN_TYPE_F;
   mov->src[0].region = { 4, 4, 1 };
   mov->src[0].swizzle = swizzle4(GEN_CHANNEL_Z, GEN_CHANNEL_Z,
                                  GEN_CHANNEL_Z, GEN_CHANNEL_Z);

   gen_inst *add3 = insts.append(GEN_OP_ADD3);
   add3->exec_size = 16;
   add3->dst.file = GEN_GRF;
   add3->dst.nr = 3;
   add3->dst.type = GEN_TYPE_D;
   add3->dst.region.hstride = 1;
   add3->src[0].file = GEN_GRF;
   add3->src[0].nr = 19;
   add3->src[0].subnr = 4;
   add3->src[0].type = GEN_TYPE_D;
   add3->src[0].region = { 0, 1, 0 };
   add3->src[1].file = GEN_GRF;
   add3->src[1].nr = 21;
   add3->src[1].type = GEN_TYPE_D;
   add3->src[1].region = { 1, 1, 0 };
   add3->src[2].file = GEN_GRF;
   add3->src[2].nr = 2;
   add3->src[2].type = GEN_TYPE_D;
   add3->src[2].region = { 0, 1, 1 };

   gen_inst *go = insts.append(GEN_OP_GOTO);
   go->exec_size = 16;
   go->src[0].file = GEN_IMM;
   go->src[0].type = GEN_TYPE_D;
   go->src[0].imm = 32;
   go->src[1].file = GEN_IMM;
   go->src[1].type = GEN_TYPE_D;
   go->src[1].imm = 32;

   gen_inst *send = insts.append(GEN_OP_SEND);
   send->exec_size = 1;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 4;
   send->send.desc_imm = 0x2229e500;
   send->send.eot = true;
   send->dst.file = GEN_GRF;
   send->dst.nr = 40;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 35;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;
   send->swsb = { 2, GEN_PIPE_INT, 5, GEN_SBID_SET };

   insts.append(GEN_OP_NOP);

   const std::string printed = print_program(&devinfo, insts);

   parsed_program parsed;
   ASSERT_TRUE(parse_program(&devinfo, printed, parsed)) << first_error(parsed);
   EXPECT_EQ(print_program(&devinfo, parsed.insts, parsed.num_insts), printed);
}

TEST(GenParse, RoundTripConcisePrinterOutput)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_parse_inst_vector insts;

   gen_inst *add = insts.append(GEN_OP_ADD);
   add->exec_size = 8;
   add->dst.file = GEN_GRF;
   add->dst.nr = 5;
   add->dst.type = GEN_TYPE_UD;
   add->dst.region.hstride = 1;
   add->src[0].file = GEN_GRF;
   add->src[0].nr = 6;
   add->src[0].type = GEN_TYPE_UD;
   add->src[0].region = { 1, 1, 0 };
   add->src[1].file = GEN_IMM;
   add->src[1].type = GEN_TYPE_UD;
   add->src[1].imm = 0x2a;

   gen_inst *add3 = insts.append(GEN_OP_ADD3);
   add3->exec_size = 16;
   add3->dst.file = GEN_GRF;
   add3->dst.nr = 3;
   add3->dst.type = GEN_TYPE_D;
   add3->dst.region.hstride = 1;
   add3->src[0].file = GEN_GRF;
   add3->src[0].nr = 19;
   add3->src[0].subnr = 4;
   add3->src[0].type = GEN_TYPE_D;
   add3->src[0].region = { 0, 1, 0 };
   add3->src[1].file = GEN_GRF;
   add3->src[1].nr = 21;
   add3->src[1].type = GEN_TYPE_D;
   add3->src[1].region = { 1, 1, 0 };
   add3->src[2].file = GEN_GRF;
   add3->src[2].nr = 2;
   add3->src[2].type = GEN_TYPE_D;
   add3->src[2].region = { 0, 1, 1 };

   gen_inst *mov = insts.append(GEN_OP_MOV);
   mov->exec_size = 8;
   mov->chan_offset = 24;
   mov->dst.file = GEN_GRF;
   mov->dst.nr = 1;
   mov->dst.type = GEN_TYPE_UD;
   mov->dst.region.hstride = 1;
   mov->src[0].file = GEN_GRF;
   mov->src[0].nr = 2;
   mov->src[0].type = GEN_TYPE_UD;
   mov->src[0].region = { 1, 1, 0 };

   gen_inst *go = insts.append(GEN_OP_GOTO);
   go->exec_size = 16;
   go->src[0].file = GEN_IMM;
   go->src[0].type = GEN_TYPE_D;
   go->src[0].imm = 32;
   go->src[1].file = GEN_IMM;
   go->src[1].type = GEN_TYPE_D;
   go->src[1].imm = 32;

   insts.append(GEN_OP_NOP);

   const gen_print_options opts = { .mode = GEN_PRINT_MODE_CONCISE };
   const std::string printed = print_program(&devinfo, insts, opts);

   parsed_program parsed;
   ASSERT_TRUE(parse_program(&devinfo, printed, parsed)) << first_error(parsed);
   EXPECT_EQ(print_program(&devinfo, parsed.insts, parsed.num_insts, opts), printed);
}

TEST(GenParse, ParsesDpasFunctionControl)
{
   intel_device_info devinfo = get_devinfo("mtl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "dpas.8x4 (8|M0) r1.0<1>:d r2.0<8;8,1>:d r3.0<8;8,1>:hf r4.0<8;8,1>:hf\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_EQ(parsed.insts[0]->opcode, GEN_OP_DPAS);
   EXPECT_EQ(parsed.insts[0]->dpas.sdepth, 8);
   EXPECT_EQ(parsed.insts[0]->dpas.rcount, 4);
}

TEST(GenParse, ParsesBfnFunctionControl)
{
   intel_device_info devinfo = get_devinfo("tgl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "bfn.0x9a (8|M0) r1.0<1>:ud r2.0<0;0>:ud r3.0<1;0>:ud r4.0<1>:ud\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_EQ(parsed.insts[0]->opcode, GEN_OP_BFN);
   EXPECT_EQ(parsed.insts[0]->boolean_func_ctrl, 0x9a);
}

TEST(GenParse, ParsesLtGtAndSqtSyntax)
{
   intel_device_info devinfo = get_devinfo("ptl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "(~f2.0) cmp (16|M0) (lt)f2.0 null<1>:f r63.0<1;1,0>:f acc0.0<1;1,0>:f\n"
      "sel (8|M0) (gt)f1.1 r4.0<1>:f r5.0<1;1,0>:f r6.0<1;1,0>:f\n"
      "math.sqt (16|M0) r14.0<1>:f r13.0<1;1,0>:f null\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 3);
   EXPECT_EQ(parsed.insts[0]->cmod, GEN_CONDITION_L);
   EXPECT_EQ(parsed.insts[1]->cmod, GEN_CONDITION_G);
   EXPECT_EQ(parsed.insts[2]->opcode, GEN_OP_MATH);
   EXPECT_EQ(parsed.insts[2]->math.func, GEN_MATH_SQRT);
   EXPECT_EQ(parsed.insts[0]->src[1].nr, GEN_ARF_ACCUMULATOR);
   EXPECT_EQ(parsed.insts[0]->src[1].subnr, 0u);
}

TEST(GenParse, ParsesBranchControlSuffix)
{
   intel_device_info devinfo = get_devinfo("tgl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "goto.b (16|M0) jip:L0 uip:L0\nL0:\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_TRUE(parsed.insts[0]->branch_control);
   EXPECT_EQ(parsed.insts[0]->src[0].imm, 16);
   EXPECT_EQ(parsed.insts[0]->src[1].imm, 16);
}

TEST(GenParse, ParsesExecControlWithoutSpaceAfterOpcode)
{
   intel_device_info devinfo = get_devinfo("tgl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "add(8) r5 r6 0x0000002a\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_EQ(parsed.insts[0]->opcode, GEN_OP_ADD);
   EXPECT_EQ(parsed.insts[0]->exec_size, 8u);
   EXPECT_EQ(parsed.insts[0]->dst.nr, 5u);
   EXPECT_EQ(parsed.insts[0]->src[0].nr, 6u);
   EXPECT_EQ(parsed.insts[0]->src[1].imm, 0x2au);
}

TEST(GenParse, ParsesExecControlWithoutSpaceAfterSendOpcode)
{
   intel_device_info devinfo = get_devinfo("tgl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "send.ugm(8|M0) r40 r35:1 null:0 a0.2 0x02100000\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_EQ(parsed.insts[0]->opcode, GEN_OP_SEND);
   EXPECT_EQ(parsed.insts[0]->exec_size, 8u);
   EXPECT_EQ(parsed.insts[0]->send.sfid, GEN_SFID_UGM);
   EXPECT_EQ(parsed.insts[0]->src[0].nr, 35u);
}

TEST(GenParse, ParsesExecControlWithoutSpaceAfterLscOpcode)
{
   intel_device_info devinfo = get_devinfo("bmg");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "load.ugm.d32x32t.a32.ca.cc.bss[a0.0](1|M0) r6 r5\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_EQ(parsed.insts[0]->opcode, GEN_OP_SEND);
   EXPECT_EQ(parsed.insts[0]->exec_size, 1u);
   EXPECT_EQ(parsed.insts[0]->send.sfid, GEN_SFID_UGM);
   EXPECT_EQ(parsed.insts[0]->send.desc_imm, 0x2229e500u);
}

TEST(GenParse, ParsesInstructionOptions)
{
   intel_device_info devinfo = get_devinfo("tgl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "mov (8|M0) r1.0<1>:f r2.0<8;8,1>:f {AccWrEn,Breakpoint,NoDDChk,NoDDClr}\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_TRUE(parsed.insts[0]->acc_wr_control);
   EXPECT_TRUE(parsed.insts[0]->debug_control);
   EXPECT_TRUE(parsed.insts[0]->no_dd_check);
   EXPECT_TRUE(parsed.insts[0]->no_dd_clear);
}

TEST(GenParse, ParsesAlign16Option)
{
   intel_device_info devinfo = get_devinfo("tgl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "mov (8|M0) r1.0<1>:f r2.0<4;4,1>:f {Align16}\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_TRUE(parsed.insts[0]->align16);
   EXPECT_EQ(parsed.insts[0]->dst.writemask, 0xfu);
   EXPECT_EQ(parsed.insts[0]->src[0].swizzle,
             swizzle4(GEN_CHANNEL_X, GEN_CHANNEL_Y,
                      GEN_CHANNEL_Z, GEN_CHANNEL_W));
}

TEST(GenParse, ParsesAlign16RepCtrl)
{
   intel_device_info devinfo = get_devinfo("skl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "mad (8|M0) r1.0<1>:f r2.0<0;0>.r:f r3.0<0;0>.r:f r4.0<0>.r:f {Align16}\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_TRUE(parsed.insts[0]->align16);
   EXPECT_TRUE(parsed.insts[0]->src[0].rep_ctrl);
   EXPECT_TRUE(parsed.insts[0]->src[1].rep_ctrl);
   EXPECT_TRUE(parsed.insts[0]->src[2].rep_ctrl);
   EXPECT_EQ(parsed.insts[0]->src[0].region.vstride, 0u);
   EXPECT_EQ(parsed.insts[0]->src[0].region.width, 1u);
   EXPECT_EQ(parsed.insts[0]->src[0].region.hstride, 0u);
}

TEST(GenParse, ParsesAtomicOption)
{
   intel_device_info devinfo = get_devinfo("tgl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "mov (8|M0) r1.0<1>:f r2.0<8;8,1>:f {Atomic}\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_TRUE(parsed.insts[0]->atomic_control);
}

TEST(GenParse, ParsesSwitchOption)
{
   intel_device_info devinfo = get_devinfo("tgl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "mov (8|M0) r1.0<1>:f r2.0<8;8,1>:f {Switch}\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_EQ(parsed.insts[0]->thread_control, GEN_THREAD_SWITCH);
}

TEST(GenParse, ParsesSerializeOption)
{
   intel_device_info devinfo = get_devinfo("tgl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "send.ugm (8|M0) r40 r35:1 null:0 a0.2 0x02100000 {Serialize}\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_TRUE(parsed.insts[0]->fusion_control);
}

TEST(GenParse, ParsesSendLengthsAndExBso)
{
   intel_device_info devinfo = get_devinfo("mtl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "send.ugm (8|M0) r40 r35:2 r8:3 a0.2 0x04100000 {ExBSO}\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_EQ(parsed.insts[0]->src[0].nr, 35u);
   EXPECT_EQ(parsed.insts[0]->src[1].nr, 8u);
   EXPECT_EQ(parsed.insts[0]->send.src1_len, 3u);
   EXPECT_TRUE(parsed.insts[0]->send.ex_bso);
}

TEST(GenParse, ParsesSendExtendedDescriptorImmediateOffset)
{
   intel_device_info devinfo = get_devinfo("bmg");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "send.ugm (8|M0) r40 r35:1 null:0 0x00000100:a0.2 0x02100000\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_TRUE(parsed.insts[0]->send.ex_desc_is_reg);
   EXPECT_EQ(parsed.insts[0]->send.ex_desc_subnr, 4u);
   EXPECT_EQ(parsed.insts[0]->send.ex_desc_imm_extra, 0x100u);
}

TEST(GenParse, NormalizesLegacySendEotDescriptor)
{
   intel_device_info devinfo = get_devinfo("skl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "(W) send.ts (8) null r127:1 null:0 0x00000000 0x82000010 {EOT}\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_TRUE(parsed.insts[0]->send.eot);
   EXPECT_EQ(parsed.insts[0]->send.desc_imm, 0x02000010u);
}

TEST(GenParse, ParsesSfidAliasesByPlatform)
{
   parsed_program parsed;

   {
      intel_device_info devinfo = get_devinfo("skl");
      ASSERT_TRUE(parse_program(
         &devinfo,
         "send.ts (8) null r127:1 0x00000000 0x02000010 {EOT}\n",
         parsed)) << first_error(parsed);
      ASSERT_EQ(parsed.num_insts, 1);
      EXPECT_EQ(parsed.insts[0]->send.sfid, GEN_SFID_THREAD_SPAWNER);
   }

   {
      intel_device_info devinfo = get_devinfo("dg2");
      ASSERT_TRUE(parse_program(
         &devinfo,
         "send.btd (8) null r127:1 null:0 0x00000000 0x02000010 {EOT}\n",
         parsed)) << first_error(parsed);
      ASSERT_EQ(parsed.num_insts, 1);
      EXPECT_EQ(parsed.insts[0]->send.sfid, GEN_SFID_BINDLESS_THREAD_DISPATCH);
   }

   {
      intel_device_info devinfo = get_devinfo("dg2");
      ASSERT_TRUE(parse_program(
         &devinfo,
         "send.gtwy (8) null r127:1 null:0 0x00000000 0x02000010 {EOT}\n",
         parsed)) << first_error(parsed);
      ASSERT_EQ(parsed.num_insts, 1);
      EXPECT_EQ(parsed.insts[0]->send.sfid, GEN_SFID_MESSAGE_GATEWAY);
   }
}

TEST(GenParse, ParsesLscUgmSourceSyntax)
{
   intel_device_info devinfo = get_devinfo("bmg");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "load.ugm.d32x32t.a32.ca.cc.bss[a0.0] (1|M0) r6 r5\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_EQ(parsed.insts[0]->opcode, GEN_OP_SEND);
   EXPECT_EQ(parsed.insts[0]->send.sfid, GEN_SFID_UGM);
   EXPECT_TRUE(parsed.insts[0]->send.ex_desc_is_reg);
   EXPECT_EQ(parsed.insts[0]->send.ex_desc_subnr, 0u);
   EXPECT_EQ(parsed.insts[0]->send.desc_imm, 0x2229e500u);
}

TEST(GenParse, ParsesLscSlmSourceSyntax)
{
   intel_device_info devinfo = get_devinfo("mtl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "load.slm.d32x4.a32 (16|M0) r10 r8\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_EQ(parsed.insts[0]->send.sfid, GEN_SFID_SLM);
}

TEST(GenParse, ParsesLscTypedTgmSourceSyntax)
{
   intel_device_info devinfo = get_devinfo("mtl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "load_cmask.tgm.d32.xy.a32.ca.ca.bti[3] (8|M0) r20 r12\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_EQ(parsed.insts[0]->send.sfid, GEN_SFID_TGM);
   EXPECT_FALSE(parsed.insts[0]->send.ex_desc_is_reg);
   EXPECT_EQ(parsed.insts[0]->send.ex_desc_imm, gen_lsc_bti_ex_desc(&devinfo, 3, 0));
   EXPECT_EQ(parsed.insts[0]->send.desc_imm,
             make_lsc_desc(&devinfo,
                           gen_lsc_msg_addr_len(&devinfo, LSC_ADDR_SIZE_A32, 8 * 4),
                           gen_lsc_msg_dest_len(&devinfo, LSC_DATA_SIZE_D32, 8 * 2),
                           LSC_OP_LOAD_CMASK, LSC_ADDR_SURFTYPE_BTI,
                           LSC_ADDR_SIZE_A32, LSC_DATA_SIZE_D32,
                           LSC_CMASK_XY, false, LSC_CACHE_LOAD_L1C_L3C));
}

TEST(GenParse, ParsesLscAtomicSourceSyntax)
{
   intel_device_info devinfo = get_devinfo("mtl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "atomic_add.ugm.d32.a32.wt.wb.bss[a0.0] (8|M0) r40 r35 r8\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_EQ(parsed.insts[0]->send.sfid, GEN_SFID_UGM);
   EXPECT_TRUE(parsed.insts[0]->send.ex_desc_is_reg);
   EXPECT_EQ(parsed.insts[0]->send.ex_desc_subnr, 0u);
   EXPECT_EQ(parsed.insts[0]->send.src1_len,
             gen_lsc_msg_dest_len(&devinfo, LSC_DATA_SIZE_D32, 8));
   EXPECT_EQ(parsed.insts[0]->send.desc_imm,
             make_lsc_desc(&devinfo,
                           gen_lsc_msg_addr_len(&devinfo, LSC_ADDR_SIZE_A32, 8),
                           gen_lsc_msg_dest_len(&devinfo, LSC_DATA_SIZE_D32, 8),
                           LSC_OP_ATOMIC_ADD, LSC_ADDR_SURFTYPE_BSS,
                           LSC_ADDR_SIZE_A32, LSC_DATA_SIZE_D32,
                           1, false, LSC_CACHE_STORE_L1WT_L3WB));
}

TEST(GenParse, ParsesLscFenceSourceSyntax)
{
   intel_device_info devinfo = get_devinfo("mtl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "fence.ugm.gpu.evict.route_to_lsc (8|M0) r2 r0\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_EQ(parsed.insts[0]->send.sfid, GEN_SFID_UGM);
   EXPECT_EQ(parsed.insts[0]->send.desc_imm,
             gen_message_desc(&devinfo, 1, 1, false) |
             gen_lsc_fence_msg_desc(&devinfo, LSC_FENCE_GPU, LSC_FLUSH_TYPE_EVICT, true));
}

TEST(GenParse, IgnoresTrailingLscAnnotationComment)
{
   intel_device_info devinfo = get_devinfo("mtl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "send.ugm (1|M0) r6 r5:1 null:0 a0.0 0x2229e500 // load.ugm.d32x32t.a32.ca.cc.bss[a0.0]\n",
      parsed)) << first_error(parsed);

   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_EQ(parsed.insts[0]->send.desc_imm, 0x2229e500u);
}

TEST(GenParse, ParsesNonNullTerminatedInput)
{
   intel_device_info devinfo = get_devinfo("tgl");
   parsed_program parsed;
   const char text[] = {
      'm', 'o', 'v', ' ', '(', '8', ')', ' ', 'r', '1', ' ', 'r', '2', '\n',
      't', 'r', 'a', 'i', 'l', 'i', 'n', 'g'
   };

   ASSERT_TRUE(parse_program(&devinfo, text, 14, parsed)) << first_error(parsed);
   ASSERT_EQ(parsed.num_insts, 1);
   EXPECT_EQ(parsed.insts[0]->opcode, GEN_OP_MOV);
}

TEST(GenParse, EncodesSingleSourceMathOnPtl)
{
   intel_device_info devinfo = get_devinfo("ptl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "math.inv (1|M0) r5.0<1>:f r10.2<0;1,0>:f null\n"
      "math.sqt (16|M0) r14.0<1>:f r13.0<1;1,0>:f null\n",
      parsed)) << first_error(parsed);

   gen_encode_params encode = {};
   ASSERT_TRUE(encode_program(&devinfo, parsed, encode));

   void *decode_mem_ctx = ralloc_context(NULL);
   gen_decode_params decode = {};
   decode.devinfo = &devinfo;
   decode.raw_bytes = encode.raw_bytes;
   decode.raw_bytes_size = encode.raw_bytes_size;
   decode.mem_ctx = decode_mem_ctx;

   ASSERT_TRUE(gen_decode(&decode));
   ASSERT_EQ(decode.num_insts, 2);

   ralloc_free(decode_mem_ctx);
}

TEST(GenParse, EncodesSingleSourceMathOnTgl)
{
   intel_device_info devinfo = get_devinfo("tgl");
   parsed_program parsed;

   ASSERT_TRUE(parse_program(
      &devinfo,
      "math.inv (1|M0) r5.0<1>:f r10.2<0;1,0>:f null\n"
      "math.sqt (16|M0) r14.0<1>:f r13.0<1;1,0>:f null\n",
      parsed)) << first_error(parsed);

   gen_encode_params encode = {};
   ASSERT_TRUE(encode_program(&devinfo, parsed, encode));
}

} /* namespace */
