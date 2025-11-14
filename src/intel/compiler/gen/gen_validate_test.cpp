/*
 * Copyright © 2016 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "gen_private.h"

#include <gtest/gtest.h>
#include "util/ralloc.h"

static const struct intel_gfx_info {
   const char *name;
} gfx_names[] = {
   { "skl", },
   { "bxt", },
   { "kbl", },
   { "aml", },
   { "glk", },
   { "cfl", },
   { "whl", },
   { "cml", },
   { "icl", },
   { "ehl", },
   { "jsl", },
   { "tgl", },
   { "rkl", },
   { "dg1", },
   { "adl", },
   { "sg1", },
   { "rpl", },
   { "dg2", },
   { "mtl", },
   { "lnl", },
   { "bmg", },
   { "ptl", },
};

class gen_validate_test : public ::testing::TestWithParam<struct intel_gfx_info> {
   virtual void SetUp();

public:
   gen_validate_test();
   virtual ~gen_validate_test();

   void *mem_ctx;
   struct intel_device_info devinfo;
};

gen_validate_test::gen_validate_test()
{
   memset(&devinfo, 0, sizeof(devinfo));
}

gen_validate_test::~gen_validate_test()
{
}

void gen_validate_test::SetUp()
{
   struct intel_gfx_info info = GetParam();
   int devid = intel_device_name_to_pci_device_id(info.name);

   intel_get_device_info_from_pci_id(devid, &devinfo);
}

struct gfx_name {
   template <class ParamType>
   std::string
   operator()(const ::testing::TestParamInfo<ParamType>& info) const {
      return info.param.name;
   }
};

INSTANTIATE_TEST_SUITE_P(
   eu_assembly, gen_validate_test,
   ::testing::ValuesIn(gfx_names),
   gfx_name()
);

TEST_P(gen_validate_test, sanity)
{
}
