/*
 * Copyright 2016 Intel Corporation
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice (including the next
 *  paragraph) shall be included in all copies or substantial portions of the
 *  Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "isl.h"
#include "isl_priv.h"
#include "dev/gen_device_info.h"

#include "util/format_srgb.h"
#include "util/ralloc.h"
#include "util/lodepng.h"

#include "main/macros.h"

/**
 * @brief Determine if a surface should be dumped.
 *
 * Since dumping a surface can produce a lot of data and be time consuming,
 * this function allows you to filter whether a surface should actually be
 * dumped. If the application is deterministic, then you can use the sequence
 * id number to filter output. Other examples are shown commented out.
 *
 * Return true when the surface should be dumped.
 */
static inline bool
filter_surface_dumping(uint64_t sequence_id,
                       const struct isl_surf *surf,
                       const void *map_addr,
                       unsigned int map_size,
                       const struct isl_surf *aux_surf,
                       const void *aux_map_addr,
                       unsigned int aux_map_size,
                       const char *basename)
{
   const uint64_t single_id = 0;
   const uint64_t min_id = 0;
   const uint64_t max_id = 0;
   return
      (min_id == 0 || sequence_id >= min_id) &&
      (max_id == 0 || sequence_id <= max_id) &&
      (single_id == 0 || sequence_id == single_id) &&
      /* surf->format == ISL_FORMAT_R8_UINT && */
      /* surf->msaa_layout == ISL_MSAA_LAYOUT_NONE && */
      true;
}

static bool
unpack_mt_rgba_pixel(uint8_t *pixel,
                     const void *map_addr, const struct isl_surf *surf,
                     bool bit6, struct isl_tile_info *info,
                     uint32_t x, uint32_t y, uint32_t s)
{
   uint8_t *r = pixel, *g = pixel + 1, *b = pixel + 2, *a = pixel + 3;
   uint32_t tile_w, tile_h;

   assert(surf->msaa_layout == ISL_MSAA_LAYOUT_NONE ||
          surf->msaa_layout == ISL_MSAA_LAYOUT_INTERLEAVED ||
          surf->msaa_layout == ISL_MSAA_LAYOUT_ARRAY);
   if (surf->samples > 1) {
      assert(surf->samples <= 4);
      if (surf->format == ISL_FORMAT_R8_UINT) {
         assert(surf->msaa_layout == ISL_MSAA_LAYOUT_INTERLEAVED);
         switch(surf->samples) {
         case 16:
         case 8:
            y = surf->samples == 8 ?
               ((y << 1) & ~2) | (s & 2) | (y & 1) :
               ((y << 2) & ~0xc) | (s & 0xc) | (y & 1);
            x = ((x << 2) & ~0xc) | ((s & 3) << 1) | (x & 1);
            break;
         case 4:
            y = ((y << 1) & ~2) | (s & 2) | (y & 1);
         case 2:
            x = ((x << 1) & ~2) | ((s & 1) << 1) | (x & 1);
         case 1:
         case 0:
            break;
         default:
            unreachable("unsupported number of samples");
         }
      } else {
         switch (surf->msaa_layout) {
         case ISL_MSAA_LAYOUT_INTERLEAVED: {
            switch(surf->samples) {
            case 4:
               y = (y << 1) | ((s & 2) >> 1);
            case 2:
               x = (x << 1) | (s & 1);
            case 1:
            case 0:
               break;
            default:
               unreachable("unsupported number of samples");
            }
            break;
         }
         case ISL_MSAA_LAYOUT_ARRAY:
            y += s * (surf->array_pitch_el_rows / surf->samples);
            break;
         case ISL_MSAA_LAYOUT_NONE:
         default:
            if (surf->samples > 1)
               unreachable("unsupported msaa layout");
            break;
         }
      }
   }

   unsigned cpp = isl_format_get_layout(surf->format)->bpb / 8;
   assert(cpp != 0);
   if (surf->tiling == ISL_TILING_W) {
      tile_w = 64;
      tile_h = 64;
   } else {
      tile_w = info->logical_extent_el.w;
      tile_h = info->logical_extent_el.h;
      //intel_get_tile_dims(surf->tiling, mt->tr_mode, cpp, &tile_w, &tile_h);
      //tile_w = surf->logical_level0_px.w;
      //tile_h = surf->logical_level0_px.h;
      //unreachable("todo");
   }

   const uint8_t *tile_base;
   if (surf->tiling == ISL_TILING_LINEAR) {
      tile_base = map_addr;
   } else {
      unsigned w_shift = surf->tiling == ISL_TILING_W ? 1 : 0;
      intptr_t offset =
         (surf->row_pitch >> w_shift) * ROUND_DOWN_TO(y, tile_h) +
         ((x / tile_w) << 12);
      tile_base = ((uint8_t*)map_addr) + offset;
   }

   uint32_t tile_x = x % tile_w, tile_y = y % tile_h;
   uint32_t tile_offset = 0;

   switch (surf->format) {
   case ISL_FORMAT_R8G8B8A8_UNORM:
   case ISL_FORMAT_B8G8R8A8_UNORM:
      tile_x <<= 2;
      break;
   default:
      break;
   }

   switch (surf->tiling) {
   case ISL_TILING_LINEAR:
      tile_offset = y * surf->row_pitch + x * cpp;
      break;
   case ISL_TILING_W:
      tile_offset +=
         ((tile_x & 0x01) << 0) | ((tile_y & 0x01) << 1) |
         ((tile_x & 0x02) << 1) | ((tile_y & 0x02) << 2) |
         ((tile_x & 0x04) << 2) | ((tile_y & 0x04) << 3) |
         ((tile_x & 0x38) << 6) | ((tile_y & 0x38) << 3);
      break;
   case ISL_TILING_X:
      tile_offset +=
         ((tile_x & 0x1ff) << 0) | ((tile_y & 0x07) << 9);
      break;
   case ISL_TILING_Y0:
      tile_offset +=
         ((tile_x & 0x0f) << 0) | ((tile_y & 0x1f) << 4) |
         ((tile_x & 0x70) << 5);
      break;
   default:
      unreachable("unknown tiling");
      return false;
   }

   if (bit6) {
      switch (surf->tiling) {
      case ISL_TILING_LINEAR:
         break;
      case ISL_TILING_X:
         tile_offset ^= (tile_offset >> 4) & 0x40;
         /* fall-through */
      case ISL_TILING_W:
      case ISL_TILING_Y0:
         tile_offset ^= (tile_offset >> 3) & 0x40;
         break;
      default:
         unreachable("unknown tiling");
         return false;
      }
   }

   //printf("offset=0x%x\n", tile_offset);
   switch (surf->format) {
   case ISL_FORMAT_R8_UINT:
      *r = *g = *b = MIN(0xff, tile_base[tile_offset] << 4);
      *a = 0xff;
      break;
   case ISL_FORMAT_R8G8B8A8_UNORM:
      *r = tile_base[tile_offset + 0];
      *g = tile_base[tile_offset + 1];
      *b = tile_base[tile_offset + 2];
      *a = tile_base[tile_offset + 3];
      break;
   case ISL_FORMAT_B8G8R8A8_UNORM:
      *b = tile_base[tile_offset + 0];
      *g = tile_base[tile_offset + 1];
      *r = tile_base[tile_offset + 2];
      *a = tile_base[tile_offset + 3];
      break;
   case ISL_FORMAT_R24_UNORM_X8_TYPELESS:
      *r = *g = *b =
         MIN(0xff, (*(uint32_t*)(&tile_base[tile_offset]) >> 16) & 0xff);
      *a = 0xff;
      break;
   case ISL_FORMAT_B8G8R8A8_UNORM_SRGB:
      *b = util_format_srgb_to_linear_8unorm(tile_base[tile_offset + 0]);
      *g = util_format_srgb_to_linear_8unorm(tile_base[tile_offset + 1]);
      *r = util_format_srgb_to_linear_8unorm(tile_base[tile_offset + 2]);
      *a = tile_base[tile_offset + 3];
      break;
   default:
      return false;
   }

   return true;
}

static const char *
tiling_name(enum isl_tiling tiling)
{
#define TILENAME(t) case ISL_TILING_##t: return #t
   switch(tiling) {
   TILENAME(LINEAR);
   TILENAME(W);
   TILENAME(X);
   TILENAME(Y0);
   TILENAME(Yf);
   TILENAME(Ys);
   TILENAME(HIZ);
   TILENAME(CCS);
   default:
      return NULL;
   }
}

static const char *
msaa_name(enum isl_msaa_layout layout)
{
#define MSAA_NAME(l) case ISL_MSAA_LAYOUT_##l: return #l
   switch(layout) {
   MSAA_NAME(NONE);
   MSAA_NAME(INTERLEAVED);
   MSAA_NAME(ARRAY);
   default:
      return NULL;
   }
}

static bool
dump_surf_info(const struct isl_device *dev,
               const struct isl_surf *surf,
               const char *filename)
{
   FILE *f = fopen(filename, "w");
   if (!f)
      goto fail_open;

   fprintf(f, "Format: %s\n", isl_format_get_name(surf->format));
   fprintf(f, "Tiling: %s\n", tiling_name(surf->tiling));
   fprintf(f, "Row pitch (B): %d\n", surf->row_pitch_B);
   fprintf(f, "Array pitch (q-pitch): %d\n", surf->array_pitch_el_rows);
   fprintf(f, "Bit6 swizzle: %sabled\n", dev->has_bit6_swizzling ? "En" : "Dis");
   fprintf(f, "Samples: %d\n", surf->samples);
   fprintf(f, "MSAA layout: %s\n", msaa_name(surf->msaa_layout));
   fprintf(f, "Logical size LOD0 (px): %d x %d\n",
           surf->logical_level0_px.w, surf->logical_level0_px.h);
   fprintf(f, "Physical size LOD0 (sa): %d x %d\n",
           surf->phys_level0_sa.w, surf->phys_level0_sa.h);

   fclose(f);
   return true;

 fail_open:
   fprintf(stderr, "%s:%s: couldn't open file: %s\n", __FILE__, __func__, filename);
   return false;
}

static bool
dump_surf_binary(const void *map_addr,
                 unsigned int map_size,
                 const char *filename)
{
   FILE *f = fopen(filename, "w");
   if (!f)
      goto fail_open;

   fwrite(map_addr, 1, map_size, f);

   fclose(f);
   return true;

 fail_open:
   fprintf(stderr, "%s:%s: couldn't open file: %s\n", __FILE__, __func__, filename);
   return false;
}

static void
isl_dump_png(const struct isl_device *dev,
             const struct isl_surf *surf,
             const void *map_addr,
             unsigned int map_size,
             const struct isl_surf *aux_surf,
             const void *aux_map_addr,
             unsigned int aux_map_size,
             const char *filename)
{
   FILE *f;
   bool bit6 = dev->has_bit6_swizzling;

   if (surf->samples > 4 && surf->tiling != ISL_TILING_W)
      return;

   switch (surf->format) {
   case ISL_FORMAT_R8_UINT:
   case ISL_FORMAT_R8G8B8A8_UNORM:
   case ISL_FORMAT_B8G8R8A8_UNORM:
   case ISL_FORMAT_R24_UNORM_X8_TYPELESS:
   case ISL_FORMAT_B8G8R8A8_UNORM_SRGB:
      break;
   default:
      fprintf(stderr, "%s:%s: unsupported format %s\n", __FILE__, __func__,
              isl_format_get_name(surf->format));
      return;
   }

   switch (surf->msaa_layout) {
   case ISL_MSAA_LAYOUT_NONE:
   case ISL_MSAA_LAYOUT_INTERLEAVED:
   case ISL_MSAA_LAYOUT_ARRAY:
      break;
   default:
      fprintf(stderr, "%s:%s: unsupported msaa format %d\n", __FILE__, __func__,
              surf->msaa_layout);
      return;
   }

   uint32_t samples = MAX(surf->samples, 1);
   uint32_t png_w = surf->logical_level0_px.w * (samples > 1 ? 2 : 1);
   uint32_t png_h = surf->logical_level0_px.h * (samples > 2 ? 2 : 1);
   uint32_t png_px = png_w * png_h;
   uint8_t *rgba = (uint8_t*)rzalloc_size(NULL, 4 * png_px);
   if (!rgba)
      goto fail_alloc;

   struct isl_tile_info tile_info;
   isl_surf_get_tile_info(dev, surf, &tile_info);

   for (uint32_t y = 0; y < surf->logical_level0_px.h; y++) {
      for (uint32_t x = 0; x < surf->logical_level0_px.w; x++) {
         for (uint32_t s = 0; s < samples; s++) {
            const uint32_t sx = samples > 1 ? ((x << 1) + (s & 1)) : x;
            const uint32_t sy = samples > 2 ? ((y << 1) + ((s & 2) >> 1)) : y;
            uint32_t offset = 4 * ((sy * png_w) + sx);
            uint8_t *px = rgba + offset;
            if (!unpack_mt_rgba_pixel(px, map_addr, surf, bit6, &tile_info,
                                      x, y, s))
               goto fail_unsupported_format;
         }
      }
   }

   unsigned char *png;
   size_t png_size;
   unsigned enc_result =
      lodepng_encode32(&png, &png_size, (unsigned char*)rgba, png_w, png_h);
   if (enc_result != 0)
      goto fail_encode;

   f = fopen(filename, "w");
   if (!f) {
      if (errno == EEXIST)
         goto fail_file_exists;
      else
         goto fail_write;
   }

   if (fwrite(png, 1, png_size, f) != png_size)
      goto fail_write;

   free(png);

   if (fclose(f) == -1)
      goto fail_close;

   printf("saved %s\n", filename);

   return;

 fail_file_exists:
   fprintf(stderr, "%s:%s: file already exists: %s\n", __FILE__, __func__, filename);
   abort();

 fail_write:
   fprintf(stderr, "%s:%s: failed to write file: %s\n", __FILE__, __func__, filename);
   abort();

 fail_close:
   fprintf(stderr, "%s:%s: failed to close file: %s\n", __FILE__, __func__, filename);
   abort();

 fail_alloc:
   fprintf(stderr, "%s:%s: failed to alloc rgba buffer\n", __FILE__, __func__);
   abort();

 fail_unsupported_format:
   fprintf(stderr, "%s:%s: unsupported format %s\n", __FILE__, __func__,
           isl_format_get_name(surf->format));
   abort();

 fail_encode:
   fprintf(stderr, "%s:%s: png encode failed\n", __FILE__, __func__);
   abort();
}

void
isl_surf_dump(const struct isl_device *dev,
              const struct isl_surf *surf,
              const void *map_addr,
              unsigned int map_size,
              const struct isl_surf *aux_surf,
              const void *aux_map_addr,
              unsigned int aux_map_size,
              const char *basename)
{
   int ret;

   char filename[1024];
   static uint64_t sequence_id = 0;

   sequence_id++;

   if (!filter_surface_dumping(sequence_id, surf, map_addr, map_size, aux_surf,
                               aux_map_addr, aux_map_size, basename))
      return;

   ret = snprintf(filename, sizeof(filename), "%04" PRIu64 "-%s.txt",
                  sequence_id, basename);
   if (ret < -1 || ret >= sizeof(filename))
      goto fail_basename_too_big;

   if (!dump_surf_info(dev, surf, filename))
      goto fail_dump_info;

   if (map_addr != NULL && map_size > 0) {
      ret = snprintf(filename, sizeof(filename), "%04" PRIu64 "-%s.bin",
                     sequence_id, basename);
      if (ret < -1 || ret >= sizeof(filename))
         goto fail_basename_too_big;

      if (!dump_surf_binary(map_addr, map_size, filename))
         goto fail_dump_info;
   }

   if (aux_surf) {
      ret = snprintf(filename, sizeof(filename), "%04" PRIu64 "-%s-aux.txt",
                     sequence_id, basename);
      if (ret < -1 || ret >= sizeof(filename))
         goto fail_basename_too_big;

      if (!dump_surf_info(dev, aux_surf, filename))
         goto fail_dump_info;

      if (aux_map_addr != NULL && aux_map_size > 0) {
         ret = snprintf(filename, sizeof(filename), "%04" PRIu64 "-%s-aux.bin",
                        sequence_id, basename);
         if (ret < -1 || ret >= sizeof(filename))
            goto fail_basename_too_big;

         if (!dump_surf_binary(aux_map_addr, aux_map_size, filename))
            goto fail_dump_info;
      }
   }

   ret = snprintf(filename, sizeof(filename), "%04lu-%s.png",
                  sequence_id, basename);
   if (ret < -1 || ret >= sizeof(filename))
      goto fail_basename_too_big;

   isl_dump_png(dev, surf, map_addr, map_size, aux_surf,
                aux_map_addr, aux_map_size, filename);

   return;

 fail_basename_too_big:
   fprintf(stderr, "%s:%s: basename too big\n", __FILE__, __func__);
   abort();

 fail_dump_info:
   fprintf(stderr, "%s:%s: failed to dump surface info\n", __FILE__, __func__);
   abort();
}
