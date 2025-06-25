#!/usr/bin/env python3
# Copyright © 2025 Intel Corporation
# SPDX-License-Identifier: MIT

import argparse
import ctypes
import enum
import fcntl
import json
import pathlib
import os
import re
import sys


class HwconfigTypes(enum.Enum):

    INTEL_HWCONFIG_MAX_SLICES_SUPPORTED = 1
    INTEL_HWCONFIG_MAX_DUAL_SUBSLICES_SUPPORTED = 2
    INTEL_HWCONFIG_MAX_NUM_EU_PER_DSS = 3
    INTEL_HWCONFIG_NUM_PIXEL_PIPES = 4
    INTEL_HWCONFIG_DEPRECATED_MAX_NUM_GEOMETRY_PIPES = 5
    INTEL_HWCONFIG_DEPRECATED_L3_CACHE_SIZE_IN_KB = 6
    INTEL_HWCONFIG_DEPRECATED_L3_BANK_COUNT = 7
    INTEL_HWCONFIG_L3_CACHE_WAYS_SIZE_IN_BYTES = 8
    INTEL_HWCONFIG_L3_CACHE_WAYS_PER_SECTOR = 9
    INTEL_HWCONFIG_MAX_MEMORY_CHANNELS = 10
    INTEL_HWCONFIG_MEMORY_TYPE = 11
    INTEL_HWCONFIG_CACHE_TYPES = 12
    INTEL_HWCONFIG_LOCAL_MEMORY_PAGE_SIZES_SUPPORTED = 13
    INTEL_HWCONFIG_DEPRECATED_SLM_SIZE_IN_KB = 14
    INTEL_HWCONFIG_NUM_THREADS_PER_EU = 15
    INTEL_HWCONFIG_TOTAL_VS_THREADS = 16
    INTEL_HWCONFIG_TOTAL_GS_THREADS = 17
    INTEL_HWCONFIG_TOTAL_HS_THREADS = 18
    INTEL_HWCONFIG_TOTAL_DS_THREADS = 19
    INTEL_HWCONFIG_TOTAL_VS_THREADS_POCS = 20
    INTEL_HWCONFIG_TOTAL_PS_THREADS = 21
    INTEL_HWCONFIG_DEPRECATED_MAX_FILL_RATE = 22
    INTEL_HWCONFIG_MAX_RCS = 23
    INTEL_HWCONFIG_MAX_CCS = 24
    INTEL_HWCONFIG_MAX_VCS = 25
    INTEL_HWCONFIG_MAX_VECS = 26
    INTEL_HWCONFIG_MAX_COPY_CS = 27
    INTEL_HWCONFIG_DEPRECATED_URB_SIZE_IN_KB = 28
    INTEL_HWCONFIG_MIN_VS_URB_ENTRIES = 29
    INTEL_HWCONFIG_MAX_VS_URB_ENTRIES = 30
    INTEL_HWCONFIG_MIN_PCS_URB_ENTRIES = 31
    INTEL_HWCONFIG_MAX_PCS_URB_ENTRIES = 32
    INTEL_HWCONFIG_MIN_HS_URB_ENTRIES = 33
    INTEL_HWCONFIG_MAX_HS_URB_ENTRIES = 34
    INTEL_HWCONFIG_MIN_GS_URB_ENTRIES = 35
    INTEL_HWCONFIG_MAX_GS_URB_ENTRIES = 36
    INTEL_HWCONFIG_MIN_DS_URB_ENTRIES = 37
    INTEL_HWCONFIG_MAX_DS_URB_ENTRIES = 38
    INTEL_HWCONFIG_PUSH_CONSTANT_URB_RESERVED_SIZE = 39
    INTEL_HWCONFIG_POCS_PUSH_CONSTANT_URB_RESERVED_SIZE = 40
    INTEL_HWCONFIG_URB_REGION_ALIGNMENT_SIZE_IN_BYTES = 41
    INTEL_HWCONFIG_URB_ALLOCATION_SIZE_UNITS_IN_BYTES = 42
    INTEL_HWCONFIG_MAX_URB_SIZE_CCS_IN_BYTES = 43
    INTEL_HWCONFIG_VS_MIN_DEREF_BLOCK_SIZE_HANDLE_COUNT = 44
    INTEL_HWCONFIG_DS_MIN_DEREF_BLOCK_SIZE_HANDLE_COUNT = 45
    INTEL_HWCONFIG_NUM_RT_STACKS_PER_DSS = 46
    INTEL_HWCONFIG_MAX_URB_STARTING_ADDRESS = 47
    INTEL_HWCONFIG_MIN_CS_URB_ENTRIES = 48
    INTEL_HWCONFIG_MAX_CS_URB_ENTRIES = 49
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_URB = 50
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_REST = 51
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_DC = 52
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_RO = 53
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_Z = 54
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_COLOR = 55
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_UNIFIED_TILE_CACHE = 56
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_COMMAND_BUFFER = 57
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_RW = 58
    INTEL_HWCONFIG_MAX_NUM_L3_CONFIGS = 59
    INTEL_HWCONFIG_BINDLESS_SURFACE_OFFSET_BIT_COUNT = 60
    INTEL_HWCONFIG_RESERVED_CCS_WAYS = 61
    INTEL_HWCONFIG_CSR_SIZE_IN_MB = 62
    INTEL_HWCONFIG_GEOMETRY_PIPES_PER_SLICE = 63
    INTEL_HWCONFIG_L3_BANK_SIZE_IN_KB = 64
    INTEL_HWCONFIG_SLM_SIZE_PER_DSS = 65
    INTEL_HWCONFIG_MAX_PIXEL_FILL_RATE_PER_SLICE = 66
    INTEL_HWCONFIG_MAX_PIXEL_FILL_RATE_PER_DSS = 67
    INTEL_HWCONFIG_URB_SIZE_PER_SLICE_IN_KB = 68
    INTEL_HWCONFIG_URB_SIZE_PER_L3_BANK_COUNT_IN_KB = 69
    INTEL_HWCONFIG_MAX_SUBSLICE = 70
    INTEL_HWCONFIG_MAX_EU_PER_SUBSLICE = 71
    INTEL_HWCONFIG_RAMBO_L3_BANK_SIZE_IN_KB = 72
    INTEL_HWCONFIG_SLM_SIZE_PER_SS_IN_KB = 73
    INTEL_HWCONFIG_NUM_HBM_STACKS_PER_TILE = 74
    INTEL_HWCONFIG_NUM_CHANNELS_PER_HBM_STACK = 75
    INTEL_HWCONFIG_HBM_CHANNEL_WIDTH_IN_BYTES = 76
    INTEL_HWCONFIG_MIN_TASK_URB_ENTRIES = 77
    INTEL_HWCONFIG_MAX_TASK_URB_ENTRIES = 78
    INTEL_HWCONFIG_MIN_MESH_URB_ENTRIES = 79
    INTEL_HWCONFIG_MAX_MESH_URB_ENTRIES = 80
    INTEL_HWCONFIG_MAX_GSC = 81
    INTEL_HWCONFIG_SYNC_NUM_RT_STACKS_PER_DSS = 82
    INTEL_HWCONFIG_NUM_XECU = 83

    @staticmethod
    def to_name(e):
        assert isinstance(e, (int, HwconfigTypes))
        if isinstance(e, HwconfigTypes):
            return e.name
        try:
            return HwconfigTypes(e).name
        except Exception:
            return e

    @staticmethod
    def to_value(e):
        if isinstance(e, int):
            return e
        elif isinstance(e, HwconfigTypes):
            return e.value
        else:
            try:
                return HwconfigTypes[e].value
            except Exception:
                return int(e)


class HwconfigMemTypes(enum.Enum):

    INTEL_HWCONFIG_MEMORY_TYPE_LPDDR4 = 0
    INTEL_HWCONFIG_MEMORY_TYPE_LPDDR5 = enum.auto()
    INTEL_HWCONFIG_MEMORY_TYPE_HBM2 = enum.auto()
    INTEL_HWCONFIG_MEMORY_TYPE_HBM2e = enum.auto()
    INTEL_HWCONFIG_MEMORY_TYPE_GDDR6 = enum.auto()


class HwconfigCacheTypes(enum.Enum):

    INTEL_HWCONFIG_CACHE_TYPE_L3 = 0
    INTEL_HWCONFIG_CACHE_TYPE_LLC = enum.auto()
    INTEL_HWCONFIG_CACHE_TYPE_EDRAM = enum.auto()


C_TEMPLATE = """\
/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 *
 * Autogenerated file, do not edit!
 */

#include <stdlib.h>
#include "intel_hwconfig_gen.h"

const char *
intel_hwconfig_type_to_name(enum intel_hwconfig hwconfig_type)
{
   switch (hwconfig_type) {
% for t in types:
   case ${t.value}: return "${t.name}";
% endfor
   default: return NULL;
   }
}

const char *
intel_hwconfig_mem_type_to_name(enum intel_hwconfig_mem_type mem_type)
{
   switch (mem_type) {
% for t in mem_types:
   case ${t.value}: return "${t.name}";
% endfor
   default: return NULL;
   }
}

const char *
intel_hwconfig_cache_type_to_name(enum intel_hwconfig_cache_type cache_type)
{
   switch (cache_type) {
% for t in cache_types:
   case ${t.value}: return "${t.name}";
% endfor
   default: return NULL;
   }
}
"""


H_TEMPLATE = """\
/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 *
 * Autogenerated file, do not edit!
 */

#ifndef _INTEL_HWCONFIG_GEN_H_
#define _INTEL_HWCONFIG_GEN_H_

#ifdef __cplusplus
extern "C" {
#endif

enum intel_hwconfig {
% for t in types:
   ${t.name} = ${t.value},
% endfor
};

enum intel_hwconfig_mem_type {
% for t in mem_types:
   ${t.name} = ${t.value},
% endfor
};

enum intel_hwconfig_cache_type {
% for t in cache_types:
   ${t.name} = ${t.value},
% endfor
};

const char *
intel_hwconfig_type_to_name(enum intel_hwconfig);

const char *
intel_hwconfig_mem_type_to_name(enum intel_hwconfig_mem_type);

const char *
intel_hwconfig_cache_type_to_name(enum intel_hwconfig_cache_type);

#ifdef __cplusplus
}
#endif

#endif /* _INTEL_HWCONFIG_GEN_H_ */
"""


class GenHwconfigSources:

    def __init__(self, args):
        self.args = args
        self.template_input = {
            "types": HwconfigTypes,
            "mem_types": HwconfigMemTypes,
            "cache_types": HwconfigCacheTypes,
        }
        import mako.template
        self.c_template = mako.template.Template(C_TEMPLATE)
        self.h_template = mako.template.Template(H_TEMPLATE)

    def gen(self):
        gen_count = 0
        if self.gen_c_source():
            gen_count += 1
        if self.gen_h_source():
            gen_count += 1
        if gen_count == 0:
            print("info: No sources specified to be generated")
        return gen_count > 0

    def gen_c_source(self):
        if self.args.c is None:
            return False
        with open(self.args.c, 'w', encoding='utf8') as c:
            c.write(self.c_template.render(**self.template_input))
        return True

    def gen_h_source(self):
        if self.args.h is None:
            return False
        with open(self.args.h, 'w', encoding='utf8') as h:
            h.write(self.h_template.render(**self.template_input))
        return True


class HwconfigApp:

    def __init__(self):
        self.parse_args()
        mode = self.args.mode
        if mode == "gen-sources":
            self.gen_sources = GenHwconfigSources(self.args)
            sys.exit(0 if self.gen_sources.gen() else 1)
        else:
            assert False, mode + " not implemented"

    def parse_args(self):
        p = argparse.ArgumentParser()
        p.add_argument("-v", "--verbose", action="count", default=0,
                       help="Enable verbose output")

        sps = p.add_subparsers(dest="mode", help="Specifies the run mode")

        sp = sps.add_parser("gen-sources")
        sp.add_argument("--h", help="Generate C header file")
        sp.add_argument("--c", help="Generate C source file")

        a = p.parse_args()

        self.args = a
        self.verbose = a.verbose


if __name__ == "__main__":
    HwconfigApp()
