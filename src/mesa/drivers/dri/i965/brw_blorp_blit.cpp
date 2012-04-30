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

#include "main/teximage.h"

#include "glsl/ralloc.h"

#include "intel_fbo.h"

#include "brw_blorp.h"
#include "brw_context.h"
#include "brw_eu.h"
#include "brw_state.h"

static bool
try_blorp_blit(struct intel_context *intel,
               GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
               GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
               GLenum filter, GLbitfield buffer_bit)
{
   struct gl_context *ctx = &intel->ctx;

   /* Find buffers */
   const struct gl_framebuffer *read_fb = ctx->ReadBuffer;
   const struct gl_framebuffer *draw_fb = ctx->DrawBuffer;
   struct gl_renderbuffer *src_rb;
   struct gl_renderbuffer *dst_rb;
   switch (buffer_bit) {
   case GL_COLOR_BUFFER_BIT:
      src_rb = read_fb->_ColorReadBuffer;
      dst_rb =
         draw_fb->Attachment[
            draw_fb->_ColorDrawBufferIndexes[0]].Renderbuffer;
      break;
   case GL_DEPTH_BUFFER_BIT:
      src_rb = read_fb->Attachment[BUFFER_DEPTH].Renderbuffer;
      dst_rb = draw_fb->Attachment[BUFFER_DEPTH].Renderbuffer;
      break;
   case GL_STENCIL_BUFFER_BIT:
      src_rb = read_fb->Attachment[BUFFER_STENCIL].Renderbuffer;
      dst_rb = draw_fb->Attachment[BUFFER_STENCIL].Renderbuffer;
      break;
   default:
      assert(false);
   }

   /* Validate source */
   if (!src_rb) return false;
   struct intel_renderbuffer *src_irb = intel_renderbuffer(src_rb);
   struct intel_mipmap_tree *src_mt = src_irb->mt;
   if (!src_mt) return false; /* TODO: or is this guaranteed non-NULL? */
   if (buffer_bit == GL_STENCIL_BUFFER_BIT && src_mt->stencil_mt)
      src_mt = src_mt->stencil_mt; /* TODO: verify that this line is needed */

   /* Validate destination */
   if (!dst_rb) return false;
   struct intel_renderbuffer *dst_irb = intel_renderbuffer(dst_rb);
   struct intel_mipmap_tree *dst_mt = dst_irb->mt;
   if (!dst_mt) return false; /* TODO: or is this guaranteed non-NULL? */
   if (buffer_bit == GL_STENCIL_BUFFER_BIT && dst_mt->stencil_mt)
      dst_mt = dst_mt->stencil_mt; /* TODO: verify that this line is needed */

   /* Make sure width and height match, and there is no mirroring.
    * TODO: allow mirroring.
    */
   if (srcX1 < srcX0) return false;
   if (srcY1 < srcY0) return false;
   GLsizei width = srcX1 - srcX0;
   GLsizei height = srcY1 - srcY0;
   if (width != dstX1 - dstX0) return false;
   if (height != dstY1 - dstY0) return false;

   /* Make sure width and height don't need to be clipped or scissored.
    * TODO: support clipping and scissoring.
    */
   if (srcX0 < 0 || (GLuint) srcX1 > read_fb->Width) return false;
   if (srcY0 < 0 || (GLuint) srcY1 > read_fb->Height) return false;
   if (dstX0 < 0 || (GLuint) dstX1 > draw_fb->Width) return false;
   if (dstY0 < 0 || (GLuint) dstY1 > draw_fb->Height) return false;
   if (ctx->Scissor.Enabled) return false;

   /* Get ready to blit.  This includes depth resolving the src and dst
    * buffers if necessary.
    */
   intel_prepare_render(intel);
   intel_renderbuffer_resolve_depth(intel, src_irb);
   intel_renderbuffer_resolve_depth(intel, dst_irb);

   /* Do the blit */
   brw_blorp_blit_params params(src_mt, dst_mt,
                                srcX0, srcY0, dstX0, dstY0, dstX1, dstY1);
   params.exec(intel);

   /* Mark the dst buffer as needing a HiZ resolve if necessary. */
   intel_renderbuffer_set_needs_hiz_resolve(dst_irb);

   return true;
}

GLbitfield
brw_blorp_framebuffer(struct intel_context *intel,
                      GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                      GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                      GLbitfield mask, GLenum filter)
{
   /* BLORP is only supported on GEN6 and above */
   if (intel->gen < 6)
      return mask;

   static GLbitfield buffer_bits[] = {
      GL_COLOR_BUFFER_BIT,
      GL_DEPTH_BUFFER_BIT,
      GL_STENCIL_BUFFER_BIT,
   };

   for (unsigned int i = 0; i < ARRAY_SIZE(buffer_bits); ++i) {
      if ((mask & buffer_bits[i]) &&
       try_blorp_blit(intel,
                      srcX0, srcY0, srcX1, srcY1,
                      dstX0, dstY0, dstX1, dstY1,
                      filter, buffer_bits[i])) {
         mask &= ~buffer_bits[i];
      }
   }

   return mask;
}

/**
 * Generator for WM programs used in BLORP blits.
 *
 * The bulk of the work done by the WM program is to wrap and unwrap the
 * coordinate transformations used by the hardware to store surfaces in
 * memory.  The hardware transforms a pixel location (X, Y, S) (where S is the
 * sample index for a multisampled surface) to a memory offset by the
 * following formulas:
 *
 *   offset = tile(tiling_format, encode_msaa(num_samples, X, Y, S))
 *   (X, Y, S) = decode_msaa(num_samples, detile(tiling_format, offset))
 *
 * For a single-sampled surface, encode_msaa() and decode_msaa are the
 * identity function:
 *
 *   encode_msaa(1, X, Y, 0) = (X, Y)
 *   decode_msaa(1, X, Y) = (X, Y, 0)
 *
 * For a 4x multisampled surface, encode_msaa() embeds the sample number into
 * bit 1 of the X and Y coordinates:
 *
 *   encode_msaa(4, X, Y, S) = (X', Y')
 *     where X' = (X & ~0b1) << 1 | (S & 0b1) << 1 | (X & 0b1)
 *           Y' = (Y & ~0b1 ) << 1 | (S & 0b10) | (Y & 0b1)
 *   decode_msaa(4, X, Y) = (X', Y', S)
 *     where X' = (X & ~0b11) >> 1 | (X & 0b1)
 *           Y' = (Y & ~0b11) >> 1 | (Y & 0b1)
 *           S = (Y & 0b10) | (X & 0b10) >> 1
 *
 * For X tiling, tile() combines together the low-order bits of the X and Y
 * coordinates in the pattern 0byyyxxxxxxxxx, creating 4k tiles that are 512
 * bytes wide and 8 rows high:
 *
 *   tile(x_tiled, X, Y) = A
 *     where A = tile_num << 12 | offset
 *           tile_num = (Y >> 3) * tile_pitch + (X' >> 9)
 *           offset = (Y & 0b111) << 9
 *                    | (X & 0b111111111)
 *           X' = X * cpp
 *   detile(x_tiled, A) = (X, Y)
 *     where X = X' / cpp
 *           Y = (tile_num / tile_pitch) << 3
 *               | (A & 0b111000000000) >> 9
 *           X' = (tile_num % tile_pitch) << 9
 *                | (A & 0b111111111)
 *
 * (In all tiling formulas, cpp is the number of bytes occupied by a single
 * sample ("chars per pixel"), and tile_pitch is the number of 4k tiles
 * required to fill the width of the surface).
 *
 * For Y tiling, tile() combines together the low-order bits of the X and Y
 * coordinates in the pattern 0bxxxyyyyyxxxx, creating 4k tiles that are 128
 * bytes wide and 32 rows high:
 *
 *   tile(y_tiled, X, Y) = A
 *     where A = tile_num << 12 | offset
 *           tile_num = (Y >> 5) * tile_pitch + (X' >> 7)
 *           offset = (X' & 0b1110000) << 5
 *                    | (Y' & 0b11111) << 4
 *                    | (X' & 0b1111)
 *           X' = X * cpp
 *   detile(y_tiled, A) = (X, Y)
 *     where X = X' / cpp
 *           Y = (tile_num / tile_pitch) << 5
 *               | (A & 0b111110000) >> 4
 *           X' = (tile_num % tile_pitch) << 7
 *                | (A & 0b111000000000) >> 5
 *                | (A & 0b1111)
 *
 * For W tiling, tile() combines together the low-order bits of the X and Y
 * coordinates in the pattern 0bxxxyyyyxyxyx, creating 4k tiles that are 64
 * bytes wide and 64 rows high (note that W tiling is only used for stencil
 * buffers, which always have cpp = 1):
 *
 *   tile(w_tiled, X, Y) = A
 *     where A = tile_num << 12 | offset
 *           tile_num = (Y >> 6) * tile_pitch + (X' >> 6)
 *           offset = (X' & 0b111000) << 6
 *                    | (Y & 0b111100) << 3
 *                    | (X' & 0b100) << 2
 *                    | (Y & 0b10) << 2
 *                    | (X' & 0b10) << 1
 *                    | (Y & 0b1) << 1
 *                    | (X' & 0b1)
 *           X' = X * cpp = X
 *   detile(w_tiled, A) = (X, Y)
 *     where X = X' / cpp = X'
 *           Y = (tile_num / tile_pitch) << 6
 *               | (A & 0b111100000) >> 3
 *               | (A & 0b1000) >> 2
 *               | (A & 0b10) >> 1
 *           X' = (tile_num % tile_pitch) << 6
 *                | (A & 0b111000000000) >> 6
 *                | (A & 0b10000) >> 2
 *                | (A & 0b100) >> 1
 *                | (A & 0b1)
 *
 * Finally, for a non-tiled surface, tile() simply combines together the X and
 * Y coordinates in the natural way:
 *
 *   tile(untiled, X, Y) = A
 *     where A = Y * pitch + X'
 *           X' = X * cpp
 *   detile(untiled, A) = (X, Y)
 *     where X = X' / cpp
 *           Y = A / pitch
 *           X' = A % pitch
 *
 * (In these formulas, pitch is the number of bytes occupied by a single row
 * of samples).
 */
class brw_blorp_blit_program
{
public:
   brw_blorp_blit_program(struct brw_context *brw,
                          const brw_blorp_blit_prog_key *key);
   ~brw_blorp_blit_program();

   const GLuint *compile(struct brw_context *brw, GLuint *program_size);

   brw_blorp_prog_data prog_data;

private:
   void alloc_regs();
   void alloc_push_const_regs(int base_reg);
   void compute_frag_coords();
   void translate_tiling(bool old_tiled_w, bool new_tiled_w);
   void encode_msaa(unsigned num_samples);
   void decode_msaa(unsigned num_samples);
   void kill_if_outside_dst_rect();
   void translate_dst_to_src();
   void single_to_blend();
   void sample();
   void texel_fetch();
   void texture_lookup(GLuint msg_type,
                       struct brw_reg mrf_u, struct brw_reg mrf_v);
   void render_target_write();

   enum {
      TEXTURE_BINDING_TABLE_INDEX,
      RENDERBUFFER_BINDING_TABLE_INDEX,
      NUM_BINDING_TABLE_ENTRIES /* TODO: don't rely on the coincidence that this == GEN6_HIZ_NUM_BINDING_TABLE_ENTRIES. */
   };

   void *mem_ctx;
   struct brw_context *brw;
   const brw_blorp_blit_prog_key *key;
   struct brw_compile func;

   /* Thread dispatch header */
   struct brw_reg R0;

   /* Pixel X/Y coordinates (always in R1). */
   struct brw_reg R1;

   /* Push constants */
   struct brw_reg dst_x0;
   struct brw_reg dst_x1;
   struct brw_reg dst_y0;
   struct brw_reg dst_y1;
   struct brw_reg x_offset;
   struct brw_reg y_offset;

   /* Data returned from texture lookup (4 vec16's) */
   struct brw_reg Rdata;

   /* X coordinates.  We have two of them so that we can perform coordinate
    * transformations easily.
    */
   struct brw_reg x_coords[2];

   /* Y coordinates.  We have two of them so that we can perform coordinate
    * transformations easily.
    */
   struct brw_reg y_coords[2];

   /* Which element of x_coords and y_coords is currently in use.
    */
   int xy_coord_index;

   /* True if, at the point in the program currently being compiled, the
    * sample index is known to be zero.
    */
   bool s_is_zero;

   /* Register storing the sample index when s_is_zero is false. */
   struct brw_reg sample_index;

   /* Temporaries */
   struct brw_reg t1;
   struct brw_reg t2;

   /* M2-3: u coordinate */
   GLuint base_mrf;
   struct brw_reg mrf_u_float;

   /* M4-5: v coordinate */
   struct brw_reg mrf_v_float;

   /* M6-7: r coordinate */
   struct brw_reg mrf_r_float;
};

brw_blorp_blit_program::brw_blorp_blit_program(
      struct brw_context *brw,
      const brw_blorp_blit_prog_key *key)
   : mem_ctx(ralloc_context(NULL)),
     brw(brw),
     key(key)
{
   brw_init_compile(brw, &func, mem_ctx);
}

brw_blorp_blit_program::~brw_blorp_blit_program()
{
   ralloc_free(mem_ctx);
}

const GLuint *
brw_blorp_blit_program::compile(struct brw_context *brw,
                                GLuint *program_size)
{
   /* Sanity checks */
   if (key->src_tiled_w) {
      /* If the source image is W tiled, then tex_samples must be 0.
       * Otherwise, after conversion between W and Y tiling, there's no
       * guarantee that the sample index will be 0, and we don't know how to
       * texel fetch from a sample index other than 0 yet.  TODO: figure out.
       */
      assert(key->tex_samples == 0);
   }

   if (key->dst_tiled_w) {
      /* If the destination image is W tiled, then dst_samples must be 0.
       * Otherwise, after conversion between W and Y tiling, there's no
       * guarantee that all samples corresponding to a single pixel will still
       * be together.  TODO: decide whether it's worth the effort to ease this
       * restriction.
       */
      assert(key->rt_samples == 0);
   }

   if (key->blend) {
      /* We are blending, which means we'll be using a SAMPLE message, which
       * causes the hardware to pick up the all of the samples corresponding
       * to this pixel and average them together.  Since we'll be relying on
       * the hardware to find all of the samples and combine them together,
       * the surface state for the texture must be configured with the correct
       * tiling and sample count.
       */
      assert(!key->src_tiled_w);
      assert(key->tex_samples == key->src_samples);
      assert(key->tex_samples > 0);
   }

   brw_set_compression_control(&func, BRW_COMPRESSION_NONE);

   alloc_regs();
   compute_frag_coords();

   /* Render target and texture hardware don't support W tiling. */
   const bool rt_tiled_w = false;
   const bool tex_tiled_w = false;

   /* The address that data will be written to is determined by the
    * coordinates supplied to the WM thread and the tiling and sample count of
    * the render target, according to the formula:
    *
    * (X, Y, S) = decode_msaa(rt_samples, detile(rt_tiling, offset))
    *
    * If the actual tiling and sample count of the destination surface are not
    * the same as the configuration of the render target, then these
    * coordinates are wrong and we have to adjust them to compensate for the
    * difference.
    */
   if (rt_tiled_w != key->dst_tiled_w ||
       key->rt_samples != key->dst_samples) {
      encode_msaa(key->rt_samples);
      /* Now (X, Y) = detile(rt_tiling, offset) */
      translate_tiling(rt_tiled_w, key->dst_tiled_w);
      /* Now (X, Y) = detile(dst_tiling, offset) */
      decode_msaa(key->dst_samples);
   }

   /* Now (X, Y, S) = decode_msaa(dst_samples, detile(dst_tiling, offset)).
    *
    * That is: X, Y and S now contain the true coordinates and sample index of
    * the data that the WM thread should output.
    *
    * If we need to kill pixels that are outside the destination rectangle,
    * now is the time to do it.
    */

   if (key->use_kill)
      kill_if_outside_dst_rect();

   /* Next, apply a translation to obtain coordinates in the source image. */
   translate_dst_to_src();

   /* X and Y are now the coordinates of the pixel in the source image that we
    * want to texture from.
    *
    * If the source image is multisampled, and we're not blending, then S is
    * the index of the sample we want to fetch.  If we are blending, then we
    * want to fetch all samples, so S is irrelevant.  And if the source image
    * isn't multisampled, then S is also irrelevant.
    */
   if (key->blend) {
      single_to_blend();
      sample();
   } else {
      /* We aren't blending, which means we just want to fetch a single sample
       * from the source surface.  The address that we want to fetch from is
       * related to the X, Y and S values according to the formula:
       *
       * (X, Y, S) = decode_msaa(src_samples, detile(src_tiling, offset)).
       *
       * If the actual tiling and sample count of the source surface are not
       * the same as the configuration of the texture, then we need to adjust
       * the coordinates to compensate for the difference.
       */
      if (tex_tiled_w != key->src_tiled_w ||
          key->tex_samples != key->src_samples) {
         encode_msaa(key->src_samples);
         /* Now (X, Y) = detile(src_tiling, offset) */
         translate_tiling(key->src_tiled_w, tex_tiled_w);
         /* Now (X, Y) = detile(tex_tiling, offset) */
         decode_msaa(key->tex_samples);
      }

      /* Now (X, Y, S) = decode_msaa(tex_samples, detile(tex_tiling, offset)).
       *
       * In other words: X, Y, and S now contain values which, when passed to
       * the texturing unit, will cause data to be read from the correct
       * memory location.  So we can fetch the texel now.
       */
      texel_fetch();
   }

   /* Finally, write the fetched (or blended) value to the render target and
    * terminate the thread.
    */
   render_target_write();
   return brw_get_program(&func, program_size);
}

void
brw_blorp_blit_program::alloc_push_const_regs(int base_reg)
{
#define CONST_LOC(name) offsetof(brw_blorp_wm_push_constants, name)
#define ALLOC_REG(name) \
   this->name = \
      brw_uw1_reg(BRW_GENERAL_REGISTER_FILE, base_reg, CONST_LOC(name) / 2)

   ALLOC_REG(dst_x0);
   ALLOC_REG(dst_x1);
   ALLOC_REG(dst_y0);
   ALLOC_REG(dst_y1);
   ALLOC_REG(x_offset);
   ALLOC_REG(y_offset);
#undef CONST_LOC
#undef ALLOC_REG
}

void
brw_blorp_blit_program::alloc_regs()
{
   int reg = 0;
   this->R0 = retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW);
   this->R1 = retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW);
   prog_data.first_curbe_grf = reg;
   alloc_push_const_regs(reg);
   reg += BRW_BLORP_NUM_PUSH_CONST_REGS;
   this->Rdata = vec16(brw_vec8_grf(reg, 0)); reg += 8;
   for (int i = 0; i < 2; ++i) {
      this->x_coords[i]
         = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
      this->y_coords[i]
         = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
   }
   this->xy_coord_index = 0;
   this->sample_index
      = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
   this->t1 = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));
   this->t2 = vec16(retype(brw_vec8_grf(reg++, 0), BRW_REGISTER_TYPE_UW));

   int mrf = 2;
   this->base_mrf = mrf;
   this->mrf_u_float = vec16(brw_message_reg(mrf)); mrf += 2;
   this->mrf_v_float = vec16(brw_message_reg(mrf)); mrf += 2;
   this->mrf_r_float = vec16(brw_message_reg(mrf)); mrf += 2;
}

/* In the code that follows, X and Y can be used to quickly refer to the
 * active elements of x_coords and y_coords, and Xp and Yp ("X prime" and "Y
 * prime") to the inactive elements.
 *
 * S can be used to quickly refer to sample_index.
 */
#define X x_coords[xy_coord_index]
#define Y y_coords[xy_coord_index]
#define Xp x_coords[!xy_coord_index]
#define Yp y_coords[!xy_coord_index]
#define S sample_index

/* Quickly swap the roles of (X, Y) and (Xp, Yp).  Saves us from having to do
 * MOVs to transfor (Xp, Yp) to (X, Y) after a coordinate transformation.
 */
#define SWAP_XY_AND_XPYP() xy_coord_index = !xy_coord_index;

/**
 * Emit code to compute the X and Y coordinates of the pixels being rendered
 * by this WM invocation.
 *
 * Assuming the render target is set up for Y tiling, these (X, Y) values are
 * related to the address offset where outputs will be written by the formula:
 *
 *   (X, Y, S) = decode_msaa(detile(offset)).
 *
 * (See brw_blorp_blit_program).
 */
void
brw_blorp_blit_program::compute_frag_coords()
{
   /* R1.2[15:0] = X coordinate of upper left pixel of subspan 0 (pixel 0)
    * R1.3[15:0] = X coordinate of upper left pixel of subspan 1 (pixel 4)
    * R1.4[15:0] = X coordinate of upper left pixel of subspan 2 (pixel 8)
    * R1.5[15:0] = X coordinate of upper left pixel of subspan 3 (pixel 12)
    *
    * Pixels within a subspan are laid out in this arrangement:
    * 0 1
    * 2 3
    *
    * So, to compute the coordinates of each pixel, we need to read every 2nd
    * 16-bit value (vstride=2) from R1, starting at the 4th 16-bit value
    * (suboffset=4), and duplicate each value 4 times (hstride=0, width=4).
    * In other words, the data we want to access is R1.4<2;4,0>UW.
    *
    * Then, we need to add the repeating sequence (0, 1, 0, 1, ...) to the
    * result, since pixels n+1 and n+3 are in the right half of the subspan.
    */
   brw_ADD(&func, X, stride(suboffset(R1, 4), 2, 4, 0), brw_imm_v(0x10101010));

   /* Similarly, Y coordinates for subspans come from R1.2[31:16] through
    * R1.5[31:16], so to get pixel Y coordinates we need to start at the 5th
    * 16-bit value instead of the 4th (R1.5<2;4,0>UW instead of
    * R1.4<2;4,0>UW).
    *
    * And we need to add the repeating sequence (0, 0, 1, 1, ...), since
    * pixels n+2 and n+3 are in the bottom half of the subspan.
    */
   brw_ADD(&func, Y, stride(suboffset(R1, 5), 2, 4, 0), brw_imm_v(0x11001100));

   /* Since we always run the WM in a mode that causes a single fragment
    * dispatch per pixel, it's not meaningful to compute a sample value.  Just
    * set it to 0.
    */
   s_is_zero = true;
}

/**
 * Emit code to compensate for the difference between Y and W tiling.
 *
 * This code modifies the X and Y coordinates according to the formula:
 *
 *   (X', Y') = detile(new_tiling, tile(old_tiling, X, Y))
 *
 * (See brw_blorp_blit_program).
 *
 * It can only translate between W and Y tiling, so new_tiling and old_tiling
 * are booleans where true represents W tiling and false represents Y tiling.
 */
void
brw_blorp_blit_program::translate_tiling(bool old_tiled_w, bool new_tiled_w)
{
   if (old_tiled_w == new_tiled_w)
      return;

   if (new_tiled_w) {
      /* Given X and Y coordinates that describe an address using Y tiling,
       * translate to the X and Y coordinates that describe the same address
       * using W tiling.
       *
       * If we break down the low order bits of X and Y, using a
       * single letter to represent each low-order bit:
       *
       *   X = A << 7 | 0bBCDEFGH
       *   Y = J << 5 | 0bKLMNP                                       (1)
       *
       * Then we can apply the Y tiling formula to see the memory offset being
       * addressed:
       *
       *   offset = (J * tile_pitch + A) << 12 | 0bBCDKLMNPEFGH       (2)
       *
       * If we apply the W detiling formula to this memory location, that the
       * corresponding X' and Y' coordinates are:
       *
       *   X' = A << 6 | 0bBCDPFH                                     (3)
       *   Y' = J << 6 | 0bKLMNEG
       *
       * Combining (1) and (3), we see that to transform (X, Y) to (X', Y'),
       * we need to make the following computation:
       *
       *   X' = (X & ~0b1011) >> 1 | (Y & 0b1) << 2 | X & 0b1         (4)
       *   Y' = (Y & ~0b1) << 1 | (X & 0b1000) >> 2 | (X & 0b10) >> 1
       */
      brw_AND(&func, t1, X, brw_imm_uw(0xfff4)); /* X & ~0b1011 */
      brw_SHR(&func, t1, t1, brw_imm_uw(1)); /* (X & ~0b1011) >> 1 */
      brw_AND(&func, t2, Y, brw_imm_uw(1)); /* Y & 0b1 */
      brw_SHL(&func, t2, t2, brw_imm_uw(2)); /* (Y & 0b1) << 2 */
      brw_OR(&func, t1, t1, t2); /* (X & ~0b1011) >> 1 | (Y & 0b1) << 2 */
      brw_AND(&func, t2, X, brw_imm_uw(1)); /* X & 0b1 */
      brw_OR(&func, Xp, t1, t2);
      brw_AND(&func, t1, Y, brw_imm_uw(0xfffe)); /* Y & ~0b1 */
      brw_SHL(&func, t1, t1, brw_imm_uw(1)); /* (Y & ~0b1) << 1 */
      brw_AND(&func, t2, X, brw_imm_uw(8)); /* X & 0b1000 */
      brw_SHR(&func, t2, t2, brw_imm_uw(2)); /* (X & 0b1000) >> 2 */
      brw_OR(&func, t1, t1, t2); /* (Y & ~0b1) << 1 | (X & 0b1000) >> 2 */
      brw_AND(&func, t2, X, brw_imm_uw(2)); /* X & 0b10 */
      brw_SHR(&func, t2, t2, brw_imm_uw(1)); /* (X & 0b10) >> 1 */
      brw_OR(&func, Yp, t1, t2);
      SWAP_XY_AND_XPYP();
   } else {
      /* Applying the same logic as above, but in reverse, we obtain the
       * formulas:
       *
       * X' = (X & ~0b101) << 1 | (Y & 0b10) << 2 | (Y & 0b1) << 1 | X & 0b1
       * Y' = (Y & ~0b11) >> 1 | (X & 0b100) >> 2
       */
      brw_AND(&func, t1, X, brw_imm_uw(0xfffa)); /* X & ~0b101 */
      brw_SHL(&func, t1, t1, brw_imm_uw(1)); /* (X & ~0b101) << 1 */
      brw_AND(&func, t2, Y, brw_imm_uw(2)); /* Y & 0b10 */
      brw_SHL(&func, t2, t2, brw_imm_uw(2)); /* (Y & 0b10) << 2 */
      brw_OR(&func, t1, t1, t2); /* (X & ~0b101) << 1 | (Y & 0b10) << 2 */
      brw_AND(&func, t2, Y, brw_imm_uw(1)); /* Y & 0b1 */
      brw_SHL(&func, t2, t2, brw_imm_uw(1)); /* (Y & 0b1) << 1 */
      brw_OR(&func, t1, t1, t2); /* (X & ~0b101) << 1 | (Y & 0b10) << 2
                                    | (Y & 0b1) << 1 */
      brw_AND(&func, t2, X, brw_imm_uw(1)); /* X & 0b1 */
      brw_OR(&func, Xp, t1, t2);
      brw_AND(&func, t1, Y, brw_imm_uw(0xfffc)); /* Y & ~0b11 */
      brw_SHR(&func, t1, t1, brw_imm_uw(1)); /* (Y & ~0b11) >> 1 */
      brw_AND(&func, t2, X, brw_imm_uw(4)); /* X & 0b100 */
      brw_SHR(&func, t2, t2, brw_imm_uw(2)); /* (X & 0b100) >> 2 */
      brw_OR(&func, Yp, t1, t2);
      SWAP_XY_AND_XPYP();
   }
}

/**
 * Emit code to compensate for the difference between MSAA and non-MSAA
 * surfaces.
 *
 * This code modifies the X and Y coordinates according to the formula:
 *
 *   (X', Y') = encode_msaa_4x(X, Y, S)
 *
 * (See brw_blorp_blit_program).
 */
void
brw_blorp_blit_program::encode_msaa(unsigned num_samples)
{
   if (num_samples == 0) {
      /* No translation necessary. */
   } else {
      /* encode_msaa_4x(X, Y, S) = (X', Y')
       *   where X' = (X & ~0b1) << 1 | (S & 0b1) << 1 | (X & 0b1)
       *         Y' = (Y & ~0b1 ) << 1 | (S & 0b10) | (Y & 0b1)
       */
      brw_AND(&func, t1, X, brw_imm_uw(0xfffe)); /* X & ~0b1 */
      if (!s_is_zero) {
         brw_AND(&func, t2, S, brw_imm_uw(1)); /* S & 0b1 */
         brw_OR(&func, t1, t1, t2); /* (X & ~0b1) | (S & 0b1) */
      }
      brw_SHL(&func, t1, t1, brw_imm_uw(1)); /* (X & ~0b1) << 1
                                                | (S & 0b1) << 1 */
      brw_AND(&func, t2, X, brw_imm_uw(1)); /* X & 0b1 */
      brw_OR(&func, Xp, t1, t2);
      brw_AND(&func, t1, Y, brw_imm_uw(0xfffe)); /* Y & ~0b1 */
      brw_SHL(&func, t1, t1, brw_imm_uw(1)); /* (Y & ~0b1) << 1 */
      if (!s_is_zero) {
         brw_AND(&func, t2, S, brw_imm_uw(2)); /* S & 0b10 */
         brw_OR(&func, t1, t1, t2); /* (Y & ~0b1) << 1 | (S & 0b10) */
      }
      brw_AND(&func, t2, Y, brw_imm_uw(1));
      brw_OR(&func, Yp, t1, t2);
      SWAP_XY_AND_XPYP();
   }
}

/**
 * Emit code to compensate for the difference between MSAA and non-MSAA
 * surfaces.
 *
 * This code modifies the X and Y coordinates according to the formula:
 *
 *   (X', Y', S) = decode_msaa(num_samples, X, Y)
 *
 * (See brw_blorp_blit_program).
 */
void
brw_blorp_blit_program::decode_msaa(unsigned num_samples)
{
   if (num_samples == 0) {
      /* No translation necessary. */
      s_is_zero = true;
   } else {
      /* decode_msaa_4x(X, Y) = (X', Y', S)
       *   where X' = (X & ~0b11) >> 1 | (X & 0b1)
       *         Y' = (Y & ~0b11) >> 1 | (Y & 0b1)
       *         S = (Y & 0b10) | (X & 0b10) >> 1
       */
      brw_AND(&func, t1, X, brw_imm_uw(0xfffc)); /* X & ~0b11 */
      brw_SHR(&func, t1, t1, brw_imm_uw(1)); /* (X & ~0b11) >> 1 */
      brw_AND(&func, t2, X, brw_imm_uw(1)); /* X & 0b1 */
      brw_OR(&func, Xp, t1, t2);
      brw_AND(&func, t1, Y, brw_imm_uw(0xfffc)); /* Y & ~0b11 */
      brw_SHR(&func, t1, t1, brw_imm_uw(1)); /* (Y & ~0b11) >> 1 */
      brw_AND(&func, t2, Y, brw_imm_uw(1)); /* Y & 0b1 */
      brw_OR(&func, Yp, t1, t2);
      brw_AND(&func, t1, Y, brw_imm_uw(2)); /* Y & 0b10 */
      brw_AND(&func, t2, X, brw_imm_uw(2)); /* X & 0b10 */
      brw_SHR(&func, t2, t2, brw_imm_uw(1)); /* (X & 0b10) >> 1 */
      brw_OR(&func, S, t1, t2);
      s_is_zero = false;
      SWAP_XY_AND_XPYP();
   }
}

/**
 * Emit code that kills pixels whose X and Y coordinates are outside the
 * boundary of the rectangle defined by the push constants (dst_x0, dst_y0,
 * dst_x1, dst_y1).
 */
void
brw_blorp_blit_program::kill_if_outside_dst_rect()
{
   struct brw_reg f0 = brw_flag_reg();
   struct brw_reg g1 = retype(brw_vec1_grf(1, 7), BRW_REGISTER_TYPE_UW);
   struct brw_reg null16 = vec16(retype(brw_null_reg(), BRW_REGISTER_TYPE_UW));

   brw_CMP(&func, null16, BRW_CONDITIONAL_GE, X, dst_x0);
   brw_CMP(&func, null16, BRW_CONDITIONAL_GE, Y, dst_y0);
   brw_CMP(&func, null16, BRW_CONDITIONAL_L, X, dst_x1);
   brw_CMP(&func, null16, BRW_CONDITIONAL_L, Y, dst_y1);

   brw_set_predicate_control(&func, BRW_PREDICATE_NONE);
   brw_push_insn_state(&func);
   brw_set_mask_control(&func, BRW_MASK_DISABLE);
   brw_AND(&func, g1, f0, g1);
   brw_pop_insn_state(&func);
}

/**
 * Emit code to translate from destination (X, Y) coordinates to source (X, Y)
 * coordinates.
 */
void
brw_blorp_blit_program::translate_dst_to_src()
{
   brw_ADD(&func, Xp, X, x_offset);
   brw_ADD(&func, Yp, Y, y_offset);
   SWAP_XY_AND_XPYP();
}

/**
 * Emit code to transform the X and Y coordinates as needed for blending
 * together the different samples in an MSAA texture.
 */
void
brw_blorp_blit_program::single_to_blend()
{
   /* When looking up samples in an MSAA texture using the SAMPLE message,
    * Gen6 requires the texture coordinates to be odd integers (so that they
    * correspond to the center of a 2x2 block representing the four samples
    * that maxe up a pixel).  So we need to multiply our X and Y coordinates
    * each by 2 and then add 1.
    */
   brw_SHL(&func, t1, X, brw_imm_w(1));
   brw_SHL(&func, t2, Y, brw_imm_w(1));
   brw_ADD(&func, Xp, t1, brw_imm_w(1));
   brw_ADD(&func, Yp, t2, brw_imm_w(1));
   SWAP_XY_AND_XPYP();
}

/**
 * Emit code to look up a value in the texture using the SAMPLE message (which
 * does blending of MSAA surfaces).
 */
void
brw_blorp_blit_program::sample()
{
   texture_lookup(GEN5_SAMPLER_MESSAGE_SAMPLE, mrf_u_float, mrf_v_float);
}

/**
 * Emit code to look up a value in the texture using the SAMPLE_LD message
 * (which does a simple texel fetch).
 */
void
brw_blorp_blit_program::texel_fetch()
{
   assert(s_is_zero);
   texture_lookup(GEN5_SAMPLER_MESSAGE_SAMPLE_LD,
                  retype(mrf_u_float, BRW_REGISTER_TYPE_UD),
                  retype(mrf_v_float, BRW_REGISTER_TYPE_UD));
}

void
brw_blorp_blit_program::texture_lookup(GLuint msg_type,
                                       struct brw_reg mrf_u,
                                       struct brw_reg mrf_v)
{
   /* TODO: can we do some of this faster with a compressed instruction? */
   /* TODO: do we need to use 2NDHALF compression mode? */
   brw_MOV(&func, vec8(mrf_u), vec8(X));
   brw_MOV(&func, offset(vec8(mrf_u), 1), suboffset(vec8(X), 8));
   brw_MOV(&func, vec8(mrf_v), vec8(Y));
   brw_MOV(&func, offset(vec8(mrf_v), 1), suboffset(vec8(Y), 8));

   /* TODO: is this necessary? */
   /* TODO: what does this mean for LD mode? */
   brw_MOV(&func, mrf_r_float, brw_imm_f(0.5));

   brw_SAMPLE(&func,
              retype(Rdata, BRW_REGISTER_TYPE_UW) /* dest */,
              base_mrf /* msg_reg_nr */,
              vec8(mrf_u) /* src0 */,
              TEXTURE_BINDING_TABLE_INDEX,
              0 /* sampler -- ignored for SAMPLE_LD message */,
              WRITEMASK_XYZW,
              msg_type,
              8 /* response_length.  TODO: should be smaller for non-RGBA formats? */,
              6 /* msg_length */,
              0 /* header_present */,
              BRW_SAMPLER_SIMD_MODE_SIMD16,
              BRW_SAMPLER_RETURN_FORMAT_FLOAT32);
}

#undef X
#undef Y
#undef U
#undef V
#undef S
#undef SWAP_XY_AND_XPYP

void
brw_blorp_blit_program::render_target_write()
{
   struct brw_reg mrf_rt_write = vec16(brw_message_reg(base_mrf));
   int mrf_offset = 0;

   /* If we may have killed pixels, then we need to send R0 and R1 in a header
    * so that the render target knows which pixels we killed.
    */
   bool use_header = key->use_kill;
   if (use_header) {
      /* Copy R0/1 to MRF */
      brw_MOV(&func, retype(mrf_rt_write, BRW_REGISTER_TYPE_UD),
              retype(R0, BRW_REGISTER_TYPE_UD));
      mrf_offset += 2;
   }

   /* Copy texture data to MRFs */
   for (int i = 0; i < 4; ++i) {
      /* E.g. mov(16) m2.0<1>:f r2.0<8;8,1>:f { Align1, H1 } */
      brw_MOV(&func, offset(mrf_rt_write, mrf_offset), offset(vec8(Rdata), 2*i));
      mrf_offset += 2;
   }

   /* Now write to the render target and terminate the thread */
   brw_fb_WRITE(&func,
                16 /* dispatch_width */,
                base_mrf /* msg_reg_nr */,
                mrf_rt_write /* src0 */,
                RENDERBUFFER_BINDING_TABLE_INDEX,
                mrf_offset /* msg_length.  Should be smaller for non-RGBA formats. */,
                0 /* response_length */,
                true /* eot */,
                use_header);
}

brw_blorp_blit_params::brw_blorp_blit_params(struct intel_mipmap_tree *src_mt,
                                             struct intel_mipmap_tree *dst_mt,
                                             GLuint src_x0, GLuint src_y0,
                                             GLuint dst_x0, GLuint dst_y0,
                                             GLuint dst_x1, GLuint dst_y1)
{
   src.set(src_mt, 0, 0);
   src.map_multisampled = src_mt->num_samples > 0;
   dst.set(dst_mt, 0, 0);
   dst.map_multisampled = dst_mt->num_samples > 0;

   /* Temporary implementation restrictions.  TODO: eliminate. */
   {
      assert(dst_mt->num_samples == 0 || src_mt->num_samples == 0);
   }

   /* Provisionally set up for a straightforward blit. */
   use_wm_prog = true;
   memset(&wm_prog_key, 0, sizeof(wm_prog_key));
   wm_prog_key.tex_samples = wm_prog_key.src_samples = src_mt->num_samples;
   wm_prog_key.rt_samples  = wm_prog_key.dst_samples = dst_mt->num_samples;
   wm_prog_key.src_tiled_w = src.map_stencil_as_y_tiled;
   wm_prog_key.dst_tiled_w = dst.map_stencil_as_y_tiled;
   wm_prog_key.blend = false;
   wm_prog_key.use_kill = false;
   x0 = wm_push_consts.dst_x0 = dst_x0;
   y0 = wm_push_consts.dst_y0 = dst_y0;
   x1 = wm_push_consts.dst_x1 = dst_x1;
   y1 = wm_push_consts.dst_y1 = dst_y1;
   wm_push_consts.x_offset = (src_x0 - dst_x0);
   wm_push_consts.y_offset = (src_y0 - dst_y0);

   if (src_mt->num_samples > 0 && dst_mt->num_samples > 0) {
      /* We are blitting from a multisample buffer to a multisample buffer, so
       * we must preserve samples within a pixel.  This means we have to
       * configure the render target and texture surface states as
       * single-sampled, so that the WM program can access each sample
       * individually.
       */
      wm_prog_key.tex_samples = wm_prog_key.rt_samples = 0;
   }

   if (src.map_stencil_as_y_tiled) {
      /* We are blitting stencil buffers, which are W-tiled.  This requires
       * that we use a single-sampled render target and a single-sampled
       * texture, because two bytes that represent different samples for the
       * same pixel in W tiling may represent different pixels in Y tiling,
       * and vice versa.
       */
      wm_prog_key.tex_samples = wm_prog_key.rt_samples = 0;
      src.map_multisampled = dst.map_multisampled = false;
   } else {
      GLenum base_format = _mesa_get_format_base_format(src_mt->format);
      if (base_format != GL_DEPTH_COMPONENT /* TODO: what about GL_DEPTH_STENCIL? */
          && src_mt->num_samples > 0 && dst_mt->num_samples == 0) {
         /* We are downsampling a color buffer, so blend. */
         wm_prog_key.blend = true;
      }
   }

   if (wm_prog_key.rt_samples == 0 && wm_prog_key.dst_samples > 0) {
      /* We must expand the rectangle we send through the rendering pipeline,
       * to account for the fact that we are mapping the destination region as
       * single-sampled when it is in fact multisampled.  We must also align
       * it to a multiple of the multisampling pattern, because the
       * differences between multisampled and single-sampled surface formats
       * will mean that pixels are scrambled within the multisampling pattern.
       * TODO: what if this makes the coordinates too large?
       */
      x0 = (x0 * 2) & ~3;
      y0 = (y0 * 2) & ~3;
      x1 = ALIGN(x1 * 2, 4);
      y1 = ALIGN(y1 * 2, 4);
      wm_prog_key.use_kill = true;
   }

   if (wm_prog_key.dst_tiled_w) {
      /* We must modify the rectangle we send through the rendering pipeline,
       * to account for the fact that we are mapping it as Y-tiled when it is
       * in fact W-tiled.  Y tiles have dimensions 128x32 whereas W tiles have
       * dimensions 64x64.  We must also align it to a multiple of the tile
       * size, because the differences between W and Y tiling formats will
       * mean that pixels are scrambled within the tile.
       * TODO: what if this makes the coordinates too large?
       */
      x0 = (x0 * 2) & ~127;
      y0 = (y0 / 2) & ~31;
      x1 = ALIGN(x1 * 2, 128);
      y1 = ALIGN(y1 / 2, 32);
      wm_prog_key.use_kill = true;
   }
}

uint32_t
brw_blorp_blit_params::get_wm_prog(struct brw_context *brw,
                                   brw_blorp_prog_data **prog_data) const
{
   uint32_t prog_offset;
   if (!brw_search_cache(&brw->cache, BRW_BLORP_BLIT_PROG,
                         &this->wm_prog_key, sizeof(this->wm_prog_key),
                         &prog_offset, prog_data)) {
      brw_blorp_blit_program prog(brw, &this->wm_prog_key);
      GLuint program_size;
      const GLuint *program = prog.compile(brw, &program_size);
      brw_upload_cache(&brw->cache, BRW_BLORP_BLIT_PROG,
                       &this->wm_prog_key, sizeof(this->wm_prog_key),
                       program, program_size,
                       &prog.prog_data, sizeof(prog.prog_data),
                       &prog_offset, prog_data);
   }
   return prog_offset;
}
