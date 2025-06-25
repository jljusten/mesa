/*
 * Copyright © 2021 Intel Corporation
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

#include <stdio.h>
#include <stdlib.h>

#include "intel_device_info.h"
#include "intel_hwconfig.h"
#include "intel_hwconfig_gen.h"
#include "intel/common/intel_gem.h"
#include "i915/intel_device_info.h"
#include "xe/intel_device_info.h"

#include "util/log.h"

struct hwconfig {
   uint32_t key;
   uint32_t len;
   uint32_t val[];
};

static const char *
key_to_name(uint32_t key)
{
   const char *s = intel_hwconfig_type_to_name(key);
   return s ? s : "UNKNOWN_INTEL_HWCONFIG";
}

typedef void (*hwconfig_item_cb)(struct intel_device_info *devinfo,
                                 const struct hwconfig *item);

static void
process_hwconfig_table(struct intel_device_info *devinfo,
                       const struct hwconfig *hwconfig,
                       int32_t hwconfig_len,
                       hwconfig_item_cb item_callback_func)
{
   assert(hwconfig);
   assert(hwconfig_len % 4 == 0);
   const struct hwconfig *current = hwconfig;
   const struct hwconfig *end =
      (struct hwconfig*)(((uint32_t*)hwconfig) + (hwconfig_len / 4));
   while (current < end) {
      assert(current + 1 < end);
      struct hwconfig *next =
         (struct hwconfig*)((uint32_t*)current + 2 + current->len);
      assert(next <= end);
      item_callback_func(devinfo, current);
      current = next;
   }
   assert(current == end);
}

bool
intel_hwconfig_is_required(const struct intel_device_info *devinfo)
{
   /* returns is true when the platform should apply hwconfig values */
   return devinfo->verx10 >= 125;
}

static struct intel_mesa_hwconfig
fill_intel_mesa_hwconfig_from_blob(void *data, int32_t len)
{
   struct intel_mesa_hwconfig blob_hwconfig = { 0 };
   assert((len % 4) == 0);
   int32_t len_dwords = len / 4;
   uint32_t *blob_dwords = data;
   for (int32_t i = 0; i < len_dwords; ) {
      const enum intel_hwconfig type = blob_dwords[i];
      const int32_t item_len = blob_dwords[i + 1];
      const uint32_t *values = &blob_dwords[i + 2];
      intel_mesa_hwconfig_use_item(&blob_hwconfig, type, item_len, values);
      i += 2 + item_len;
      assert(i <= len);
   }
   return blob_hwconfig;
}

bool
intel_hwconfig_process_table(struct intel_device_info *devinfo,
                             void *data, int32_t len)
{
   struct intel_mesa_hwconfig blob_hwconfig =
      fill_intel_mesa_hwconfig_from_blob(data, len);

#define FILL_DEVINFO(dst_field, src_type)                       \
   if (blob_hwconfig.is_valid.src_type)                         \
      devinfo->dst_field = blob_hwconfig.value.src_type

   if (blob_hwconfig.is_valid.TOTAL_PS_THREADS) {
      unsigned threads = blob_hwconfig.value.TOTAL_PS_THREADS;
      if (devinfo->ver == 12)
         threads /= 2;
      devinfo->max_threads_per_psd = threads;
   }

   FILL_DEVINFO(max_eus_per_subslice, MAX_NUM_EU_PER_DSS);
   FILL_DEVINFO(num_thread_per_eu, NUM_THREADS_PER_EU);
   FILL_DEVINFO(max_vs_threads, TOTAL_VS_THREADS);
   FILL_DEVINFO(max_gs_threads, TOTAL_GS_THREADS);
   FILL_DEVINFO(max_tcs_threads, TOTAL_HS_THREADS);
   FILL_DEVINFO(max_tes_threads, TOTAL_DS_THREADS);

   FILL_DEVINFO(urb.size, URB_SIZE_PER_SLICE_IN_KB);
   FILL_DEVINFO(urb.max_entries[MESA_SHADER_VERTEX], MAX_VS_URB_ENTRIES);
   FILL_DEVINFO(urb.max_entries[MESA_SHADER_TESS_CTRL], MAX_HS_URB_ENTRIES);
   FILL_DEVINFO(urb.max_entries[MESA_SHADER_GEOMETRY], MAX_GS_URB_ENTRIES);
   FILL_DEVINFO(urb.max_entries[MESA_SHADER_TESS_EVAL], MAX_DS_URB_ENTRIES);

   FILL_DEVINFO(max_slices, MAX_SLICES_SUPPORTED);

   if (devinfo->verx10 >= 200) {
      FILL_DEVINFO(num_color_pipes, NUM_PIXEL_PIPES);
      devinfo->num_color_pipes *= devinfo->max_slices;
      devinfo->num_depth_pipes =  devinfo->num_color_pipes;
      FILL_DEVINFO(num_geom_pipes, GEOMETRY_PIPES_PER_SLICE);
      devinfo->num_geom_pipes  *= devinfo->max_slices;
   }

   if (devinfo->verx10 >= 300) {
      if (blob_hwconfig.is_valid.MAX_SLICES_SUPPORTED &&
          blob_hwconfig.is_valid.MAX_SUBSLICE) {
         assert((blob_hwconfig.value.MAX_SUBSLICE %
                 blob_hwconfig.value.MAX_SLICES_SUPPORTED) == 0);
         devinfo->max_subslices_per_slice =
            blob_hwconfig.value.MAX_SUBSLICE /
            blob_hwconfig.value.MAX_SLICES_SUPPORTED;
      } else {
         mesa_logw("Unable to calculate max_subslices_per_slice.");
      }
   }
#undef FILL_DEVINFO

   return true;
}
