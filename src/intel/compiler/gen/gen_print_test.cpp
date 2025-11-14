/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "dev/intel_device_info.h"
#include "gen.h"

namespace {

constexpr unsigned
swizzle4(unsigned x, unsigned y, unsigned z, unsigned w)
{
   return (x << 0) | (y << 2) | (z << 4) | (w << 6);
}

static uint32_t
gen_message_desc(const intel_device_info *devinfo,
                 unsigned msg_length,
                 unsigned response_length,
                 bool header_present)
{
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

static uint32_t
make_lsc_desc(const intel_device_info *devinfo,
              unsigned mlen, unsigned rlen,
              const gen_lsc_desc *desc)
{
   return gen_message_desc(devinfo, mlen, rlen, false) |
          gen_lsc_desc_encode(devinfo, desc);
}

} /* namespace */

struct gen_print_inst_vector {
   std::vector<gen_inst *> v;

   ~gen_print_inst_vector() {
      for (gen_inst *inst : v)
         delete inst;
   }

   gen_inst *append(gen_opcode op) {
      gen_inst *inst = new gen_inst;
      memset(inst, 0, sizeof(*inst));
      inst->opcode = op;
      v.push_back(inst);
      return inst;
   }

   gen_inst **data() { return v.data(); }
   int size() const { return v.size(); }
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
print_program(const intel_device_info *devinfo, gen_print_inst_vector &insts,
              gen_print_params params = {})
{
   char *str = NULL;
   size_t size = 0;
   FILE *fp = open_memstream(&str, &size);

   params.devinfo = devinfo;
   params.flags = (gen_print_flags)(params.flags | GEN_PRINT_VERBOSE);
   params.insts = insts.data();
   params.num_insts = insts.size();
   params.fp = fp;
   gen_print(&params);
   fclose(fp);

   std::string out(str ? str : "");
   free(str);

   return out;
}

static std::string
print_program_default(const intel_device_info *devinfo, gen_print_inst_vector &insts,
                      gen_print_params params = {})
{
   char *str = NULL;
   size_t size = 0;
   FILE *fp = open_memstream(&str, &size);

   params.devinfo = devinfo;
   params.insts = insts.data();
   params.num_insts = insts.size();
   params.fp = fp;
   gen_print(&params);
   fclose(fp);

   std::string out(str ? str : "");
   free(str);

   return out;
}

static std::string
print_inst(const intel_device_info *devinfo, const gen_inst *inst,
           gen_print_flags flags = GEN_PRINT_NONE)
{
   char *str = NULL;
   size_t size = 0;
   FILE *fp = open_memstream(&str, &size);

   gen_print_inst(devinfo, fp, inst,
                  (gen_print_flags)(flags | GEN_PRINT_VERBOSE));
   fclose(fp);

   std::string out(str ? str : "");
   free(str);

   return out;
}

static std::string
print_inst_default(const intel_device_info *devinfo, const gen_inst *inst,
                   gen_print_flags flags = GEN_PRINT_NONE)
{
   char *str = NULL;
   size_t size = 0;
   FILE *fp = open_memstream(&str, &size);

   gen_print_inst(devinfo, fp, inst, flags);
   fclose(fp);

   std::string out(str ? str : "");
   free(str);

   return out;
}

static gen_inst *
append_float_mov(gen_print_inst_vector &insts)
{
   gen_inst *mov = insts.append(GEN_OP_MOV);
   mov->exec_size = 8;
   mov->dst.file = GEN_GRF;
   mov->dst.nr = 1;
   mov->dst.type = GEN_TYPE_F;
   mov->dst.region.hstride = 1;
   mov->src[0].file = GEN_GRF;
   mov->src[0].nr = 2;
   mov->src[0].type = GEN_TYPE_F;
   mov->src[0].region = { 8, 8, 1 };
   return mov;
}

TEST(GenPrint, PrefixAndTypeSyntax)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *add = insts.append(GEN_OP_ADD);
   add->exec_size = 8;
   add->no_mask = true;
   add->pred_control = GEN_PREDICATE_NORMAL;
   add->pred_inv = true;
   add->dst.file = GEN_GRF;
   add->dst.nr = 1;
   add->dst.type = GEN_TYPE_F;
   add->dst.region.hstride = 1;
   add->src[0].file = GEN_GRF;
   add->src[0].nr = 2;
   add->src[0].type = GEN_TYPE_F;
   add->src[0].region = { 8, 8, 1 };
   add->src[1].file = GEN_IMM;
   add->src[1].type = GEN_TYPE_F;
   add->src[1].imm = 0x3f800000;

   EXPECT_EQ(print_program(&devinfo, insts),
             "(W&~f0.0) add (8|M0)              r1.0<1>:f     r2.0<8;8,1>:f     0x3f800000:f\n");
}

TEST(GenPrint, ConditionalModifierPlacement)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *cmp = insts.append(GEN_OP_CMP);
   cmp->exec_size = 16;
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

   EXPECT_EQ(print_program(&devinfo, insts),
             "        cmp (16|M0)    (ge)f3.1   r5.0<1>:ud    r6.0<8;8,1>:ud    r7.0<8;8,1>:ud\n");
}

TEST(GenPrint, LtGtAndAccumulatorSubregSyntax)
{
   intel_device_info devinfo = get_devinfo("ptl");
   gen_print_inst_vector insts;

   gen_inst *cmp = insts.append(GEN_OP_CMP);
   cmp->exec_size = 16;
   cmp->cmod = GEN_CONDITION_L;
   cmp->flag_nr = 2;
   cmp->flag_subnr = 0;
   cmp->dst.file = GEN_ARF;
   cmp->dst.nr = GEN_ARF_NULL;
   cmp->dst.type = GEN_TYPE_F;
   cmp->dst.region.hstride = 1;
   cmp->src[0].file = GEN_GRF;
   cmp->src[0].nr = 63;
   cmp->src[0].type = GEN_TYPE_F;
   cmp->src[0].region = { 1, 1, 0 };
   cmp->src[1].file = GEN_ARF;
   cmp->src[1].nr = GEN_ARF_ACCUMULATOR;
   cmp->src[1].subnr = 0;
   cmp->src[1].type = GEN_TYPE_F;
   cmp->src[1].region = { 1, 1, 0 };

   gen_inst *sel = insts.append(GEN_OP_SEL);
   sel->exec_size = 8;
   sel->cmod = GEN_CONDITION_G;
   sel->flag_nr = 1;
   sel->flag_subnr = 1;
   sel->dst.file = GEN_GRF;
   sel->dst.nr = 4;
   sel->dst.type = GEN_TYPE_F;
   sel->dst.region.hstride = 1;
   sel->src[0].file = GEN_GRF;
   sel->src[0].nr = 5;
   sel->src[0].type = GEN_TYPE_F;
   sel->src[0].region = { 1, 1, 0 };
   sel->src[1].file = GEN_GRF;
   sel->src[1].nr = 6;
   sel->src[1].type = GEN_TYPE_F;
   sel->src[1].region = { 1, 1, 0 };

   EXPECT_EQ(print_program(&devinfo, insts),
             "        cmp (16|M0)    (lt)f2.0   null<1>:f     r63.0<1;1,0>:f    acc0.0<1;1,0>:f\n"
             "        sel (8|M0)     (gt)f1.1   r4.0<1>:f     r5.0<1;1,0>:f     r6.0<1;1,0>:f\n");
}

TEST(GenPrint, MathSqtSyntax)
{
   intel_device_info devinfo = get_devinfo("ptl");
   gen_print_inst_vector insts;

   gen_inst *math = insts.append(GEN_OP_MATH);
   math->exec_size = 16;
   math->math.func = GEN_MATH_SQRT;
   math->dst.file = GEN_GRF;
   math->dst.nr = 14;
   math->dst.type = GEN_TYPE_F;
   math->dst.region.hstride = 1;
   math->src[0].file = GEN_GRF;
   math->src[0].nr = 13;
   math->src[0].type = GEN_TYPE_F;
   math->src[0].region = { 1, 1, 0 };
   math->src[1].file = GEN_ARF;
   math->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(&devinfo, insts),
             "        math.sqt (16|M0)          r14.0<1>:f    r13.0<1;1,0>:f    null\n");
}

TEST(GenPrint, SyntheticLabels)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *go = insts.append(GEN_OP_GOTO);
   go->exec_size = 16;
   go->src[0].file = GEN_IMM;
   go->src[0].type = GEN_TYPE_D;
   go->src[0].imm = 32;
   go->src[1].file = GEN_IMM;
   go->src[1].type = GEN_TYPE_D;
   go->src[1].imm = 32;

   insts.append(GEN_OP_NOP);
   insts.append(GEN_OP_NOP);

   EXPECT_EQ(print_program(&devinfo, insts),
             "        goto (16|M0)                          jip:L0              uip:L0\n"
             "        nop\n"
             "\n"
             "L0:\n"
             "        nop\n");
}

TEST(GenPrint, SingleInstructionDefaultUsesRawOffsets)
{
   intel_device_info devinfo = get_devinfo("tgl");

   gen_inst go = {};
   go.opcode = GEN_OP_GOTO;
   go.exec_size = 16;
   go.src[0].file = GEN_IMM;
   go.src[0].type = GEN_TYPE_D;
   go.src[0].imm = 32;
   go.src[1].file = GEN_IMM;
   go.src[1].type = GEN_TYPE_D;
   go.src[1].imm = 32;

   EXPECT_EQ(print_inst_default(&devinfo, &go),
             "        goto (16)                             jip:0x20            uip:0x20\n");
}

TEST(GenPrint, ExecControlUsesRawChannelOffset)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *mad = insts.append(GEN_OP_MAD);
   mad->exec_size = 8;
   mad->chan_offset = 24;
   mad->dst.file = GEN_GRF;
   mad->dst.nr = 10;
   mad->dst.subnr = 16;
   mad->dst.type = GEN_TYPE_F;
   mad->dst.region.hstride = 1;

   mad->src[0].file = GEN_GRF;
   mad->src[0].nr = 11;
   mad->src[0].subnr = 0;
   mad->src[0].type = GEN_TYPE_F;
   mad->src[0].region = { 0, 1, 0 };

   mad->src[1].file = GEN_GRF;
   mad->src[1].nr = 12;
   mad->src[1].subnr = 16;
   mad->src[1].type = GEN_TYPE_F;
   mad->src[1].region = { 1, 1, 0 };

   mad->src[2].file = GEN_GRF;
   mad->src[2].nr = 13;
   mad->src[2].subnr = 32;
   mad->src[2].type = GEN_TYPE_F;
   mad->src[2].region = { 8, 8, 1 };

   EXPECT_EQ(print_program(&devinfo, insts),
             "        mad (8|M24)               r10.4<1>:f    r11.0<0;0>:f      r12.4<1;0>:f      r13.8<1>:f\n");
}

TEST(GenPrint, ThreeSourceRegionShorthandMad)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *mad = insts.append(GEN_OP_MAD);
   mad->exec_size = 32;
   mad->dst.file = GEN_GRF;
   mad->dst.nr = 70;
   mad->dst.type = GEN_TYPE_F;
   mad->dst.region.hstride = 1;

   mad->src[0].file = GEN_GRF;
   mad->src[0].nr = 2;
   mad->src[0].subnr = 32;
   mad->src[0].type = GEN_TYPE_F;
   mad->src[0].region = { 0, 1, 0 };

   mad->src[1].file = GEN_GRF;
   mad->src[1].nr = 24;
   mad->src[1].type = GEN_TYPE_F;
   mad->src[1].region = { 1, 1, 0 };

   mad->src[2].file = GEN_GRF;
   mad->src[2].nr = 2;
   mad->src[2].subnr = 40;
   mad->src[2].type = GEN_TYPE_F;
   mad->src[2].region = { 0, 1, 0 };

   EXPECT_EQ(print_program(&devinfo, insts),
             "        mad (32|M0)               r70.0<1>:f    r2.8<0;0>:f       r24.0<1;0>:f      r2.10<0>:f\n");
}

TEST(GenPrint, ThreeSourceRegionShorthandAdd3)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

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

   EXPECT_EQ(print_program(&devinfo, insts),
             "        add3 (16|M0)              r3.0<1>:d     r19.1<0;0>:d      r21.0<1;0>:d      r2.0<1>:d\n");
}

TEST(GenPrint, DefaultModeOmitsDefaultPieces)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

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

   EXPECT_EQ(print_program_default(&devinfo, insts),
             "        add (8)                   r5            r6                0x0000002a\n");
}

TEST(GenPrint, DefaultModeThreeSourceDefaults)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

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

   EXPECT_EQ(print_program_default(&devinfo, insts),
             "        add3 (16)                 r3:d          r19.1<0>:d        r21:d             r2<1>:d\n");
}

TEST(GenPrint, DefaultModeUsesUniformRegionShorthand)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *mov = insts.append(GEN_OP_MOV);
   mov->exec_size = 8;
   mov->dst.file = GEN_GRF;
   mov->dst.nr = 1;
   mov->dst.type = GEN_TYPE_F;
   mov->dst.region.hstride = 1;

   mov->src[0].file = GEN_GRF;
   mov->src[0].nr = 2;
   mov->src[0].type = GEN_TYPE_F;
   mov->src[0].region = { 0, 1, 0 };

   EXPECT_EQ(print_program_default(&devinfo, insts),
             "        mov (8)                   r1:f          r2<0>:f\n");
}

TEST(GenPrint, DefaultModeKeepsNonZeroChannelOffset)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

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

   EXPECT_EQ(print_program_default(&devinfo, insts),
             "        mov (8|M24)               r1            r2\n");
}

TEST(GenPrint, RawSendFormatting)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *send = insts.append(GEN_OP_SEND);
   send->exec_size = 1;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 4;
   send->send.desc_imm = 0x2229e500;
   send->dst.file = GEN_GRF;
   send->dst.nr = 40;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 35;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;
   send->swsb = { 2, GEN_PIPE_INT, 5, GEN_SBID_SET };

   EXPECT_EQ(print_program(&devinfo, insts),
             "        send.ugm (1|M0)           r40     r35:1  null    a0.2        0x2229E500   {I@2,$5}\n");
}

TEST(GenPrint, RawSendSfidNamingByPlatform)
{
   gen_print_inst_vector insts;

   gen_inst *send = insts.append(GEN_OP_SEND);
   send->exec_size = 8;
   send->no_mask = true;
   send->send.ex_desc_imm = 0;
   send->send.desc_imm = 0x02000010;
   send->send.eot = true;
   send->dst.file = GEN_ARF;
   send->dst.nr = GEN_ARF_NULL;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 127;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   {
      intel_device_info devinfo = get_devinfo("skl");
      send->send.sfid = GEN_SFID_THREAD_SPAWNER;
      EXPECT_EQ(print_program(&devinfo, insts),
                "(W)     send.ts (8|M0)            null    r127:1         0x00000000  0x02000010   {EOT}\n");
   }

   {
      intel_device_info devinfo = get_devinfo("dg2");
      send->send.sfid = GEN_SFID_BINDLESS_THREAD_DISPATCH;
      EXPECT_EQ(print_program(&devinfo, insts),
                "(W)     send.btd (8|M0)           null    r127:1 null    0x00000000  0x02000010   {EOT}\n");
   }

   {
      intel_device_info devinfo = get_devinfo("dg2");
      send->send.sfid = GEN_SFID_MESSAGE_GATEWAY;
      EXPECT_EQ(print_program(&devinfo, insts),
                "(W)     send.gtwy (8|M0)          null    r127:1 null    0x00000000  0x02000010   {EOT}\n");
   }
}

TEST(GenPrint, DpasFunctionControlFormatting)
{
   intel_device_info devinfo = get_devinfo("mtl");
   gen_print_inst_vector insts;

   gen_inst *dpas = insts.append(GEN_OP_DPAS);
   dpas->exec_size = 8;
   dpas->dpas.sdepth = 8;
   dpas->dpas.rcount = 4;
   dpas->dst.file = GEN_GRF;
   dpas->dst.nr = 1;
   dpas->dst.type = GEN_TYPE_D;
   dpas->dst.region.hstride = 1;
   dpas->src[0].file = GEN_GRF;
   dpas->src[0].nr = 2;
   dpas->src[0].type = GEN_TYPE_D;
   dpas->src[0].region = { 8, 8, 1 };
   dpas->src[1].file = GEN_GRF;
   dpas->src[1].nr = 3;
   dpas->src[1].type = GEN_TYPE_HF;
   dpas->src[1].region = { 8, 8, 1 };
   dpas->src[2].file = GEN_GRF;
   dpas->src[2].nr = 4;
   dpas->src[2].type = GEN_TYPE_HF;
   dpas->src[2].region = { 8, 8, 1 };

   EXPECT_EQ(print_program(&devinfo, insts),
             "        dpas.8x4 (8|M0)           r1.0<1>:d     r2.0<8;8,1>:d     r3.0<8;8,1>:hf    r4.0<8;8,1>:hf\n");
}

TEST(GenPrint, BfnFunctionControlFormatting)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *bfn = insts.append(GEN_OP_BFN);
   bfn->exec_size = 8;
   bfn->boolean_func_ctrl = 0x9a;
   bfn->dst.file = GEN_GRF;
   bfn->dst.nr = 1;
   bfn->dst.type = GEN_TYPE_UD;
   bfn->dst.region.hstride = 1;
   bfn->src[0].file = GEN_GRF;
   bfn->src[0].nr = 2;
   bfn->src[0].type = GEN_TYPE_UD;
   bfn->src[0].region = { 1, 1, 0 };
   bfn->src[1].file = GEN_GRF;
   bfn->src[1].nr = 3;
   bfn->src[1].type = GEN_TYPE_UD;
   bfn->src[1].region = { 1, 1, 0 };
   bfn->src[2].file = GEN_GRF;
   bfn->src[2].nr = 4;
   bfn->src[2].type = GEN_TYPE_UD;
   bfn->src[2].region = { 0, 1, 1 };

   EXPECT_EQ(print_program(&devinfo, insts),
             "        bfn.0x9a (8|M0)           r1.0<1>:ud    r2.0<1;0>:ud      r3.0<1;0>:ud      r4.0<1>:ud\n");
}

TEST(GenPrint, BranchControlSuffixFormatting)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *go = insts.append(GEN_OP_GOTO);
   go->exec_size = 16;
   go->branch_control = true;
   go->src[0].file = GEN_IMM;
   go->src[0].type = GEN_TYPE_D;
   go->src[0].imm = 16;
   go->src[1].file = GEN_IMM;
   go->src[1].type = GEN_TYPE_D;
   go->src[1].imm = 16;

   EXPECT_EQ(print_program(&devinfo, insts),
             "        goto.b (16|M0)                        jip:L0              uip:L0\n"
             "L0:\n");
}

TEST(GenPrint, InstructionOptionFormatting)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *mov = insts.append(GEN_OP_MOV);
   mov->exec_size = 8;
   mov->acc_wr_control = true;
   mov->debug_control = true;
   mov->no_dd_check = true;
   mov->no_dd_clear = true;
   mov->dst.file = GEN_GRF;
   mov->dst.nr = 1;
   mov->dst.type = GEN_TYPE_F;
   mov->dst.region.hstride = 1;
   mov->src[0].file = GEN_GRF;
   mov->src[0].nr = 2;
   mov->src[0].type = GEN_TYPE_F;
   mov->src[0].region = { 8, 8, 1 };

   EXPECT_EQ(print_program(&devinfo, insts),
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f                     {AccWrEn,Breakpoint,NoDDChk,NoDDClr}\n");
}

TEST(GenPrint, CompactedOptionFormatting)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *mov = insts.append(GEN_OP_MOV);
   mov->exec_size = 8;
   mov->dst.file = GEN_GRF;
   mov->dst.nr = 1;
   mov->dst.type = GEN_TYPE_F;
   mov->dst.region.hstride = 1;
   mov->src[0].file = GEN_GRF;
   mov->src[0].nr = 2;
   mov->src[0].type = GEN_TYPE_F;
   mov->src[0].region = { 8, 8, 1 };

   const bool was_compacted[] = { true };

   EXPECT_EQ(print_program(&devinfo, insts,
                           { .was_compacted = was_compacted }),
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f                     {Compacted}\n");
}

TEST(GenPrint, Align16OptionFormatting)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *mov = insts.append(GEN_OP_MOV);
   mov->exec_size = 8;
   mov->align16 = true;
   mov->dst.file = GEN_GRF;
   mov->dst.nr = 1;
   mov->dst.type = GEN_TYPE_F;
   mov->dst.region.hstride = 1;
   mov->dst.writemask = 0xf;
   mov->src[0].file = GEN_GRF;
   mov->src[0].nr = 2;
   mov->src[0].type = GEN_TYPE_F;
   mov->src[0].region = { 4, 4, 1 };
   mov->src[0].swizzle = swizzle4(0, 1, 2, 3);

   EXPECT_EQ(print_program(&devinfo, insts),
             "        mov (8|M0)                r1.0<1>:f     r2.0<4;4,1>:f                     {Align16}\n");
}

TEST(GenPrint, Align16RepCtrlFormatting)
{
   intel_device_info devinfo = get_devinfo("skl");
   gen_print_inst_vector insts;

   gen_inst *mad = insts.append(GEN_OP_MAD);
   mad->exec_size = 8;
   mad->align16 = true;
   mad->dst.file = GEN_GRF;
   mad->dst.nr = 1;
   mad->dst.type = GEN_TYPE_F;
   mad->dst.region.hstride = 1;
   mad->dst.writemask = 0xf;

   mad->src[0].file = GEN_GRF;
   mad->src[0].nr = 2;
   mad->src[0].type = GEN_TYPE_F;
   mad->src[0].region = { 0, 1, 0 };
   mad->src[0].rep_ctrl = true;

   mad->src[1].file = GEN_GRF;
   mad->src[1].nr = 3;
   mad->src[1].type = GEN_TYPE_F;
   mad->src[1].region = { 0, 1, 0 };
   mad->src[1].rep_ctrl = true;

   mad->src[2].file = GEN_GRF;
   mad->src[2].nr = 4;
   mad->src[2].type = GEN_TYPE_F;
   mad->src[2].region = { 0, 1, 0 };
   mad->src[2].rep_ctrl = true;

   EXPECT_EQ(print_program(&devinfo, insts),
             "        mad (8|M0)                r1.0<1>:f     r2.0<0;0>.r:f     r3.0<0;0>.r:f     r4.0<0>.r:f {Align16}\n");
}

TEST(GenPrint, AtomicOptionFormatting)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *mov = insts.append(GEN_OP_MOV);
   mov->exec_size = 8;
   mov->atomic_control = true;
   mov->dst.file = GEN_GRF;
   mov->dst.nr = 1;
   mov->dst.type = GEN_TYPE_F;
   mov->dst.region.hstride = 1;
   mov->src[0].file = GEN_GRF;
   mov->src[0].nr = 2;
   mov->src[0].type = GEN_TYPE_F;
   mov->src[0].region = { 8, 8, 1 };

   EXPECT_EQ(print_program(&devinfo, insts),
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f                     {Atomic}\n");
}

TEST(GenPrint, SwitchOptionFormatting)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *mov = insts.append(GEN_OP_MOV);
   mov->exec_size = 8;
   mov->thread_control = GEN_THREAD_SWITCH;
   mov->dst.file = GEN_GRF;
   mov->dst.nr = 1;
   mov->dst.type = GEN_TYPE_F;
   mov->dst.region.hstride = 1;
   mov->src[0].file = GEN_GRF;
   mov->src[0].nr = 2;
   mov->src[0].type = GEN_TYPE_F;
   mov->src[0].region = { 8, 8, 1 };

   EXPECT_EQ(print_program(&devinfo, insts),
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f                     {Switch}\n");
}

TEST(GenPrint, SerializeOptionFormatting)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *send = insts.append(GEN_OP_SEND);
   send->exec_size = 8;
   send->fusion_control = true;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 4;
   send->send.desc_imm = 0x02100000;
   send->dst.file = GEN_GRF;
   send->dst.nr = 40;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 35;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(&devinfo, insts),
             "        send.ugm (8|M0)           r40     r35:1  null    a0.2        0x02100000   {Serialize}\n");
}

TEST(GenPrint, SendSrc1LengthAndExBsoFormatting)
{
   intel_device_info devinfo = get_devinfo("mtl");
   gen_print_inst_vector insts;

   gen_inst *send = insts.append(GEN_OP_SEND);
   send->exec_size = 8;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 4;
   send->send.desc_imm = 0x04100000;
   send->send.src1_len = 3;
   send->send.ex_bso = true;
   send->dst.file = GEN_GRF;
   send->dst.nr = 40;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 35;
   send->src[1].file = GEN_GRF;
   send->src[1].nr = 8;

   EXPECT_EQ(print_program(&devinfo, insts),
             "        send.ugm (8|M0)           r40     r35:2  r8:3    a0.2        0x04100000   {ExBSO}\n");
}

TEST(GenPrint, SendExDescImmediateOffsetFormatting)
{
   intel_device_info devinfo = get_devinfo("bmg");
   gen_print_inst_vector insts;

   gen_inst *send = insts.append(GEN_OP_SEND);
   send->exec_size = 8;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 4;
   send->send.ex_desc_imm_extra = 0x100;
   send->send.desc_imm = 0x02100000;
   send->dst.file = GEN_GRF;
   send->dst.nr = 40;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 35;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(&devinfo, insts),
             "        send.ugm (8|M0)           r40     r35:1  null    0x00000100:a0.2 0x02100000\n");
}

TEST(GenPrint, LscUgmSourceSyntaxFormatting)
{
   intel_device_info devinfo = get_devinfo("bmg");
   gen_print_inst_vector insts;

   gen_inst *send = insts.append(GEN_OP_SEND);
   send->exec_size = 1;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 0;
   send->send.desc_imm = 0x2229e500;
   send->dst.file = GEN_GRF;
   send->dst.nr = 6;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 5;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(&devinfo, insts,
                           { .flags = GEN_PRINT_TRANSLATED_SENDS }),
             "        load.ugm.d32x32t.a32.ca.cc.bss[a0.0] (1|M0) r6 r5:1\n");
}

TEST(GenPrint, LscSlmSourceSyntaxFormatting)
{
   intel_device_info devinfo = get_devinfo("mtl");
   gen_print_inst_vector insts;

   gen_inst *send = insts.append(GEN_OP_SEND);
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_SLM;
   send->send.ex_desc_imm = 0;
   send->send.desc_imm = 0x02403500;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 8;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(&devinfo, insts,
                           { .flags = GEN_PRINT_TRANSLATED_SENDS }),
             "        load.slm.d32x4.a32 (16|M0) r10    r8:1\n");
}

TEST(GenPrint, LscTypedTgmSourceSyntaxFormatting)
{
   intel_device_info devinfo = get_devinfo("mtl");
   gen_print_inst_vector insts;
   gen_lsc_desc desc = {};
   gen_lsc_ex_desc ex_desc = {};
   desc.op = LSC_OP_LOAD_CMASK;
   desc.addr_type = LSC_ADDR_SURFTYPE_BTI;
   desc.addr_size = LSC_ADDR_SIZE_A32;
   desc.data_size = LSC_DATA_SIZE_D32;
   desc.cache_ctrl = LSC_CACHE_LOAD_L1C_L3C;
   desc.cmask = LSC_CMASK_XY;
   ex_desc.addr_type = LSC_ADDR_SURFTYPE_BTI;
   ex_desc.bti.index = 3;
   ex_desc.bti.base_offset = 0;

   gen_inst *send = insts.append(GEN_OP_SEND);
   send->exec_size = 8;
   send->send.sfid = GEN_SFID_TGM;
   send->send.ex_desc_imm = gen_lsc_ex_desc_encode(&devinfo, &ex_desc);
   send->send.desc_imm =
      make_lsc_desc(&devinfo,
                    gen_lsc_msg_addr_len(&devinfo, LSC_ADDR_SIZE_A32, 8 * 4),
                    gen_lsc_msg_dest_len(&devinfo, LSC_DATA_SIZE_D32, 8 * 2),
                    &desc);
   send->dst.file = GEN_GRF;
   send->dst.nr = 20;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 12;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(&devinfo, insts,
                           { .flags = GEN_PRINT_TRANSLATED_SENDS }),
             "        load_cmask.tgm.d32.xy.a32.ca.ca.bti[3] (8|M0) r20 r12:4\n");
}

TEST(GenPrint, LscAtomicSourceSyntaxFormatting)
{
   intel_device_info devinfo = get_devinfo("mtl");
   gen_print_inst_vector insts;
   gen_lsc_desc desc = {};
   desc.op = LSC_OP_ATOMIC_ADD;
   desc.addr_type = LSC_ADDR_SURFTYPE_BSS;
   desc.addr_size = LSC_ADDR_SIZE_A32;
   desc.data_size = LSC_DATA_SIZE_D32;
   desc.cache_ctrl = LSC_CACHE_STORE_L1WT_L3WB;
   desc.vect_size = LSC_VECT_SIZE_V1;

   gen_inst *send = insts.append(GEN_OP_SEND);
   send->exec_size = 8;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 0;
   send->send.src1_len =
      gen_lsc_msg_dest_len(&devinfo, LSC_DATA_SIZE_D32, 8);
   send->send.desc_imm =
      make_lsc_desc(&devinfo,
                    gen_lsc_msg_addr_len(&devinfo, LSC_ADDR_SIZE_A32, 8),
                    gen_lsc_msg_dest_len(&devinfo, LSC_DATA_SIZE_D32, 8),
                    &desc);
   send->dst.file = GEN_GRF;
   send->dst.nr = 40;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 35;
   send->src[1].file = GEN_GRF;
   send->src[1].nr = 8;

   EXPECT_EQ(print_program(&devinfo, insts,
                           { .flags = GEN_PRINT_TRANSLATED_SENDS }),
             "        atomic_add.ugm.d32.a32.wt.wb.bss[a0.0] (8|M0) r40 r35:1 r8\n");
}

TEST(GenPrint, LscFenceSourceSyntaxFormatting)
{
   intel_device_info devinfo = get_devinfo("mtl");
   gen_print_inst_vector insts;
   gen_lsc_desc desc = {};
   desc.op = LSC_OP_FENCE;
   desc.addr_type = LSC_ADDR_SURFTYPE_FLAT;
   desc.addr_size = LSC_ADDR_SIZE_A32;
   desc.fence.scope = LSC_FENCE_GPU;
   desc.fence.flush_type = LSC_FLUSH_TYPE_EVICT;
   desc.fence.route_to_lsc = true;

   gen_inst *send = insts.append(GEN_OP_SEND);
   send->exec_size = 8;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_imm = 0;
   send->send.desc_imm =
      gen_message_desc(&devinfo, 1, 1, false) |
      gen_lsc_desc_encode(&devinfo, &desc);
   send->dst.file = GEN_GRF;
   send->dst.nr = 2;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 0;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(&devinfo, insts,
                           { .flags = GEN_PRINT_TRANSLATED_SENDS }),
             "        fence.ugm.gpu.evict.route_to_lsc (8|M0) r2 r0:1\n");
}

TEST(GenPrint, LscDescDecodeEncode)
{
   intel_device_info devinfo = get_devinfo("mtl");
   gen_lsc_desc expected = {};
   expected.op = LSC_OP_LOAD_CMASK;
   expected.addr_type = LSC_ADDR_SURFTYPE_BTI;
   expected.addr_size = LSC_ADDR_SIZE_A32;
   expected.data_size = LSC_DATA_SIZE_D32;
   expected.cache_ctrl = LSC_CACHE_LOAD_L1C_L3C;
   expected.cmask = LSC_CMASK_XY;

   const uint32_t raw_desc = gen_lsc_desc_encode(&devinfo, &expected);

   const gen_lsc_desc desc = gen_lsc_desc_decode(&devinfo, raw_desc);

   EXPECT_EQ(desc.op, LSC_OP_LOAD_CMASK);
   EXPECT_EQ(desc.addr_type, LSC_ADDR_SURFTYPE_BTI);
   EXPECT_EQ(desc.addr_size, LSC_ADDR_SIZE_A32);
   EXPECT_EQ(desc.data_size, LSC_DATA_SIZE_D32);
   EXPECT_EQ(desc.cache_ctrl, LSC_CACHE_LOAD_L1C_L3C);
   EXPECT_EQ(desc.cmask, LSC_CMASK_XY);

   EXPECT_EQ(gen_lsc_desc_encode(&devinfo, &desc), raw_desc);
}

TEST(GenPrint, LscFenceDescDecodeEncode)
{
   intel_device_info devinfo = get_devinfo("mtl");
   gen_lsc_desc expected = {};
   expected.op = LSC_OP_FENCE;
   expected.addr_type = LSC_ADDR_SURFTYPE_FLAT;
   expected.addr_size = LSC_ADDR_SIZE_A32;
   expected.fence.scope = LSC_FENCE_GPU;
   expected.fence.flush_type = LSC_FLUSH_TYPE_EVICT;
   expected.fence.route_to_lsc = true;

   const uint32_t raw_desc = gen_lsc_desc_encode(&devinfo, &expected);

   const gen_lsc_desc desc = gen_lsc_desc_decode(&devinfo, raw_desc);

   EXPECT_EQ(desc.op, LSC_OP_FENCE);
   EXPECT_EQ(desc.addr_type, LSC_ADDR_SURFTYPE_FLAT);
   EXPECT_EQ(desc.addr_size, LSC_ADDR_SIZE_A32);
   EXPECT_EQ(desc.fence.scope, LSC_FENCE_GPU);
   EXPECT_EQ(desc.fence.flush_type, LSC_FLUSH_TYPE_EVICT);
   EXPECT_TRUE(desc.fence.route_to_lsc);

   EXPECT_EQ(gen_lsc_desc_encode(&devinfo, &desc), raw_desc);
}

TEST(GenPrint, LscRawSyntaxFormatting)
{
   intel_device_info devinfo = get_devinfo("bmg");
   gen_print_inst_vector insts;

   gen_inst *send = insts.append(GEN_OP_SEND);
   send->exec_size = 1;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 0;
   send->send.desc_imm = 0x2229e500;
   send->dst.file = GEN_GRF;
   send->dst.nr = 6;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 5;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(&devinfo, insts),
             "        send.ugm (1|M0)           r6      r5:1   null    a0.0        0x2229E500  // load.ugm.d32x32t.a32.ca.cc.bss[a0.0]\n");

   EXPECT_EQ(print_inst(&devinfo, send),
             "        send.ugm (1|M0)           r6      r5:1   null    a0.0        0x2229E500  // load.ugm.d32x32t.a32.ca.cc.bss[a0.0]\n");
}

TEST(GenPrint, AnnotationPrintedBeforeInstruction)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *mov = insts.append(GEN_OP_MOV);
   mov->exec_size = 8;
   mov->dst.file = GEN_GRF;
   mov->dst.nr = 1;
   mov->dst.type = GEN_TYPE_F;
   mov->dst.region.hstride = 1;
   mov->src[0].file = GEN_GRF;
   mov->src[0].nr = 2;
   mov->src[0].type = GEN_TYPE_F;
   mov->src[0].region = { 8, 8, 1 };

   const char *annotations[] = {
      "annotate me",
   };

   EXPECT_EQ(print_program(&devinfo, insts,
                           { .annotations = annotations }),
             "\n// annotate me\n"
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n");
}

TEST(GenPrint, AnnotationPreservesEmbeddedNewlines)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   gen_inst *mov = insts.append(GEN_OP_MOV);
   mov->exec_size = 8;
   mov->dst.file = GEN_GRF;
   mov->dst.nr = 1;
   mov->dst.type = GEN_TYPE_F;
   mov->dst.region.hstride = 1;
   mov->src[0].file = GEN_GRF;
   mov->src[0].nr = 2;
   mov->src[0].type = GEN_TYPE_F;
   mov->src[0].region = { 8, 8, 1 };

   const char *annotations[] = {
      "line 1\nline 2",
   };

   EXPECT_EQ(print_program(&devinfo, insts,
                           { .annotations = annotations }),
             "\n// line 1\n"
             "line 2\n"
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n");
}

TEST(GenPrint, AnnotationDeduplicatesConsecutiveEqualText)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   append_float_mov(insts);
   append_float_mov(insts);

   std::string first = "annotate me";
   std::string second = "annotate me";
   const char *annotations[] = {
      first.c_str(),
      second.c_str(),
   };

   EXPECT_EQ(print_program(&devinfo, insts,
                           { .annotations = annotations }),
             "\n// annotate me\n"
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n"
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n");
}

TEST(GenPrint, AnnotationPrintedAgainAfterGap)
{
   intel_device_info devinfo = get_devinfo("tgl");
   gen_print_inst_vector insts;

   append_float_mov(insts);
   append_float_mov(insts);
   append_float_mov(insts);

   const char *annotations[] = {
      "annotate me",
      "",
      "annotate me",
   };

   EXPECT_EQ(print_program(&devinfo, insts,
                           { .annotations = annotations }),
             "\n// annotate me\n"
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n"
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n"
             "\n// annotate me\n"
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n");
}
