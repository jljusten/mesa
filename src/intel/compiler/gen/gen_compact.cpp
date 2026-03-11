/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <array>
#include <stdio.h>
#include <string.h>

#include "util/ralloc.h"

#include "gen_private.h"

const gen_raw_inst*
gen_as_raw_inst(const struct intel_device_info *devinfo, const void *p)
{
   const gen_raw_inst *r = (const gen_raw_inst *)p;
   return r && !gen_as_raw_compact_inst(devinfo, p) ? r : NULL;
}

const gen_raw_compact_inst*
gen_as_raw_compact_inst(const struct intel_device_info *devinfo, const void *p)
{
   const gen_raw_compact_inst *rc = (const gen_raw_compact_inst *)p;
   return rc && (rc->data & (1 << 29)) ? rc : NULL;
}
