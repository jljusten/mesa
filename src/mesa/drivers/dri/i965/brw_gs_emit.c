/*
 Copyright (C) Intel Corp.  2006.  All Rights Reserved.
 Intel funded Tungsten Graphics (http://www.tungstengraphics.com) to
 develop this 3D driver.
 
 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:
 
 The above copyright notice and this permission notice (including the
 next paragraph) shall be included in all copies or substantial
 portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 
 **********************************************************************/
 /*
  * Authors:
  *   Keith Whitwell <keith@tungstengraphics.com>
  */
 

#include "main/glheader.h"
#include "main/macros.h"
#include "main/enums.h"

#include "program/program.h"
#include "intel_batchbuffer.h"

#include "brw_defines.h"
#include "brw_context.h"
#include "brw_eu.h"
#include "brw_gs.h"

static void brw_gs_alloc_regs( struct brw_gs_compile *c,
			       GLuint nr_verts )
{
   GLuint i = 0,j;

   /* Register usage is static, precompute here:
    */
   c->reg.R0 = retype(brw_vec8_grf(i, 0), BRW_REGISTER_TYPE_UD); i++;

   /* Payload vertices plus space for more generated vertices:
    */
   for (j = 0; j < nr_verts; j++) {
      c->reg.vertex[j] = brw_vec4_grf(i, 0);
      i += c->nr_regs;
   }

   c->reg.header = retype(brw_vec8_grf(i++, 0), BRW_REGISTER_TYPE_UD);
   c->reg.temp = retype(brw_vec8_grf(i++, 0), BRW_REGISTER_TYPE_UD);

   c->prog_data.urb_read_length = c->nr_regs; 
   c->prog_data.total_grf = i;
}


/**
 * Set up the initial value of c->reg.header register based on c->reg.R0.
 *
 * The following information is passed to the GS thread in R0, and needs to be
 * included in the first URB_WRITE or FF_SYNC message sent by the GS:
 *
 * - DWORD 0 [31:0] handle info (Gen4 only)
 * - DWORD 5 [7:0] FFTID
 * - DWORD 6 [31:0] Debug info
 * - DWORD 7 [31:0] Debug info
 *
 * This function sets up the above data by copying by copying the contents of
 * R0 to the header register.
 */
static void brw_gs_initialize_header(struct brw_gs_compile *c)
{
   struct brw_compile *p = &c->func;
   brw_MOV(p, c->reg.header, c->reg.R0);
}

/**
 * Overwrite DWORD 2 of c->reg.header with the given immediate unsigned value.
 *
 * In URB_WRITE messages, DWORD 2 contains the fields PrimType, PrimStart,
 * PrimEnd, Increment CL_INVOCATIONS, and SONumPrimsWritten, many of which we
 * need to be able to update on a per-vertex basis.
 */
static void brw_gs_overwrite_header_dw2(struct brw_gs_compile *c,
                                        unsigned dw2)
{
   struct brw_compile *p = &c->func;
   brw_MOV(p, get_element_ud(c->reg.header, 2), brw_imm_ud(dw2));
}

/**
 * Overwrite DWORD 2 of c->reg.header with the primitive type from c->reg.R0.
 *
 * When the thread is spawned, GRF 0 contains the primitive type in bits 4:0
 * of DWORD 2.  URB_WRITE messages need the primitive type in bits 6:2 of
 * DWORD 2.  So this function extracts the primitive type field, bitshifts it
 * appropriately, and stores it in c->reg.header.
 */
static void brw_gs_overwrite_header_dw2_from_r0(struct brw_gs_compile *c)
{
   struct brw_compile *p = &c->func;
   brw_AND(p, get_element_ud(c->reg.header, 2), get_element_ud(c->reg.R0, 2),
           brw_imm_ud(0x1f));
   brw_SHL(p, get_element_ud(c->reg.header, 2),
           get_element_ud(c->reg.header, 2), brw_imm_ud(2));
}

/**
 * Apply an additive offset to DWORD 2 of c->reg.header.
 *
 * This is used to set/unset the "PrimStart" and "PrimEnd" flags appropriately
 * for each vertex.
 */
static void brw_gs_offset_header_dw2(struct brw_gs_compile *c, int offset)
{
   struct brw_compile *p = &c->func;
   brw_ADD(p, get_element_d(c->reg.header, 2), get_element_d(c->reg.header, 2),
           brw_imm_d(offset));
}


/**
 * Emit a vertex using the URB_WRITE message.  Use the contents of
 * c->reg.header for the message header, and the registers starting at \c vert
 * for the vertex data.
 *
 * If \c last is true, then this is the last vertex, so no further URB space
 * should be allocated, and this message should end the thread.
 *
 * If \c last is false, then a new URB entry will be allocated, and its handle
 * will be stored in DWORD 0 of c->reg.header for use in the next URB_WRITE
 * message.
 */
static void brw_gs_emit_vue(struct brw_gs_compile *c, 
			    struct brw_reg vert,
			    bool last)
{
   struct brw_compile *p = &c->func;
   bool allocate = !last;

   /* Copy the vertex from vertn into m1..mN+1:
    */
   brw_copy8(p, brw_message_reg(1), vert, c->nr_regs);

   /* Send each vertex as a seperate write to the urb.  This is
    * different to the concept in brw_sf_emit.c, where subsequent
    * writes are used to build up a single urb entry.  Each of these
    * writes instantiates a seperate urb entry, and a new one must be
    * allocated each time.
    */
   brw_urb_WRITE(p, 
		 allocate ? c->reg.temp
                          : retype(brw_null_reg(), BRW_REGISTER_TYPE_UD),
		 0,
		 c->reg.header,
		 allocate,
		 1,		/* used */
		 c->nr_regs + 1, /* msg length */
		 allocate ? 1 : 0, /* response length */
		 allocate ? 0 : 1, /* eot */
		 1,		/* writes_complete */
		 0,		/* urb offset */
		 BRW_URB_SWIZZLE_NONE);

   if (allocate) {
      brw_MOV(p, get_element_ud(c->reg.header, 0),
              get_element_ud(c->reg.temp, 0));
   }
}

/**
 * Send an FF_SYNC message to ensure that all previously spawned GS threads
 * have finished sending primitives down the pipeline, and to allocate a URB
 * entry for the first output vertex.  Only needed when intel->needs_ff_sync
 * is true.
 *
 * This function modifies c->reg.header: in DWORD 1, it stores num_prim (which
 * is needed by the FF_SYNC message), and in DWORD 0, it stores the handle to
 * the allocated URB entry (which will be needed by the URB_WRITE meesage that
 * follows).
 */
static void brw_gs_ff_sync(struct brw_gs_compile *c, int num_prim)
{
   struct brw_compile *p = &c->func;

   brw_MOV(p, get_element_ud(c->reg.header, 1), brw_imm_ud(num_prim));
   brw_ff_sync(p,
               c->reg.temp,
               0,
               c->reg.header,
               1, /* allocate */
               1, /* response length */
               0 /* eot */);
   brw_MOV(p, get_element_ud(c->reg.header, 0),
           get_element_ud(c->reg.temp, 0));
}


void brw_gs_quads( struct brw_gs_compile *c, struct brw_gs_prog_key *key )
{
   struct intel_context *intel = &c->func.brw->intel;

   brw_gs_alloc_regs(c, 4);
   brw_gs_initialize_header(c);
   /* Use polygons for correct edgeflag behaviour. Note that vertex 3
    * is the PV for quads, but vertex 0 for polygons:
    */
   if (intel->needs_ff_sync)
      brw_gs_ff_sync(c, 1);
   brw_gs_overwrite_header_dw2(
      c, ((_3DPRIM_POLYGON << URB_WRITE_PRIM_TYPE_SHIFT)
          | URB_WRITE_PRIM_START));
   if (key->pv_first) {
      brw_gs_emit_vue(c, c->reg.vertex[0], 0);
      brw_gs_overwrite_header_dw2(
         c, _3DPRIM_POLYGON << URB_WRITE_PRIM_TYPE_SHIFT);
      brw_gs_emit_vue(c, c->reg.vertex[1], 0);
      brw_gs_emit_vue(c, c->reg.vertex[2], 0);
      brw_gs_overwrite_header_dw2(
         c, ((_3DPRIM_POLYGON << URB_WRITE_PRIM_TYPE_SHIFT)
             | URB_WRITE_PRIM_END));
      brw_gs_emit_vue(c, c->reg.vertex[3], 1);
   }
   else {
      brw_gs_emit_vue(c, c->reg.vertex[3], 0);
      brw_gs_overwrite_header_dw2(
         c, _3DPRIM_POLYGON << URB_WRITE_PRIM_TYPE_SHIFT);
      brw_gs_emit_vue(c, c->reg.vertex[0], 0);
      brw_gs_emit_vue(c, c->reg.vertex[1], 0);
      brw_gs_overwrite_header_dw2(
         c, ((_3DPRIM_POLYGON << URB_WRITE_PRIM_TYPE_SHIFT)
             | URB_WRITE_PRIM_END));
      brw_gs_emit_vue(c, c->reg.vertex[2], 1);
   }
}

void brw_gs_quad_strip( struct brw_gs_compile *c, struct brw_gs_prog_key *key )
{
   struct intel_context *intel = &c->func.brw->intel;

   brw_gs_alloc_regs(c, 4);
   brw_gs_initialize_header(c);
   
   if (intel->needs_ff_sync)
      brw_gs_ff_sync(c, 1);
   brw_gs_overwrite_header_dw2(
      c, ((_3DPRIM_POLYGON << URB_WRITE_PRIM_TYPE_SHIFT)
          | URB_WRITE_PRIM_START));
   if (key->pv_first) {
      brw_gs_emit_vue(c, c->reg.vertex[0], 0);
      brw_gs_overwrite_header_dw2(
         c, _3DPRIM_POLYGON << URB_WRITE_PRIM_TYPE_SHIFT);
      brw_gs_emit_vue(c, c->reg.vertex[1], 0);
      brw_gs_emit_vue(c, c->reg.vertex[2], 0);
      brw_gs_overwrite_header_dw2(
         c, ((_3DPRIM_POLYGON << URB_WRITE_PRIM_TYPE_SHIFT)
             | URB_WRITE_PRIM_END));
      brw_gs_emit_vue(c, c->reg.vertex[3], 1);
   }
   else {
      brw_gs_emit_vue(c, c->reg.vertex[2], 0);
      brw_gs_overwrite_header_dw2(
         c, _3DPRIM_POLYGON << URB_WRITE_PRIM_TYPE_SHIFT);
      brw_gs_emit_vue(c, c->reg.vertex[3], 0);
      brw_gs_emit_vue(c, c->reg.vertex[0], 0);
      brw_gs_overwrite_header_dw2(
         c, ((_3DPRIM_POLYGON << URB_WRITE_PRIM_TYPE_SHIFT)
             | URB_WRITE_PRIM_END));
      brw_gs_emit_vue(c, c->reg.vertex[1], 1);
   }
}

void brw_gs_lines( struct brw_gs_compile *c )
{
   struct intel_context *intel = &c->func.brw->intel;

   brw_gs_alloc_regs(c, 2);
   brw_gs_initialize_header(c);

   if (intel->needs_ff_sync)
      brw_gs_ff_sync(c, 1);
   brw_gs_overwrite_header_dw2(
      c, ((_3DPRIM_LINESTRIP << URB_WRITE_PRIM_TYPE_SHIFT)
          | URB_WRITE_PRIM_START));
   brw_gs_emit_vue(c, c->reg.vertex[0], 0);
   brw_gs_overwrite_header_dw2(
      c, ((_3DPRIM_LINESTRIP << URB_WRITE_PRIM_TYPE_SHIFT)
          | URB_WRITE_PRIM_END));
   brw_gs_emit_vue(c, c->reg.vertex[1], 1);
}

/**
 * Generate the geometry shader program used on Gen6 to perform stream output
 * (transform feedback).
 */
void
gen6_sol_program(struct brw_gs_compile *c, struct brw_gs_prog_key *key,
	         unsigned num_verts, bool check_edge_flags)
{
   struct brw_compile *p = &c->func;

   brw_gs_alloc_regs(c, num_verts);
   brw_gs_initialize_header(c);

   brw_gs_ff_sync(c, 1);

   brw_gs_overwrite_header_dw2_from_r0(c);
   switch (num_verts) {
   case 1:
      brw_gs_offset_header_dw2(c, URB_WRITE_PRIM_START | URB_WRITE_PRIM_END);
      brw_gs_emit_vue(c, c->reg.vertex[0], true);
      break;
   case 2:
      brw_gs_offset_header_dw2(c, URB_WRITE_PRIM_START);
      brw_gs_emit_vue(c, c->reg.vertex[0], false);
      brw_gs_offset_header_dw2(c, URB_WRITE_PRIM_END - URB_WRITE_PRIM_START);
      brw_gs_emit_vue(c, c->reg.vertex[1], true);
      break;
   case 3:
      if (check_edge_flags) {
         /* Only emit vertices 0 and 1 if this is the first triangle of the
          * polygon.  Otherwise they are redundant.
          */
         brw_set_conditionalmod(p, BRW_CONDITIONAL_NZ);
         brw_AND(p, retype(brw_null_reg(), BRW_REGISTER_TYPE_UD),
                 get_element_ud(c->reg.R0, 2),
                 brw_imm_ud(BRW_GS_EDGE_INDICATOR_0));
         brw_IF(p, BRW_EXECUTE_1);
      }
      brw_gs_offset_header_dw2(c, URB_WRITE_PRIM_START);
      brw_gs_emit_vue(c, c->reg.vertex[0], false);
      brw_gs_offset_header_dw2(c, -URB_WRITE_PRIM_START);
      brw_gs_emit_vue(c, c->reg.vertex[1], false);
      if (check_edge_flags) {
         brw_ENDIF(p);
         /* Only emit vertex 2 in PRIM_END mode if this is the last triangle
          * of the polygon.  Otherwise leave the primitive incomplete because
          * there are more polygon vertices coming.
          */
         brw_set_conditionalmod(p, BRW_CONDITIONAL_NZ);
         brw_AND(p, retype(brw_null_reg(), BRW_REGISTER_TYPE_UD),
                 get_element_ud(c->reg.R0, 2),
                 brw_imm_ud(BRW_GS_EDGE_INDICATOR_1));
         brw_set_predicate_control(p, BRW_PREDICATE_NORMAL);
      }
      brw_gs_offset_header_dw2(c, URB_WRITE_PRIM_END);
      brw_set_predicate_control(p, BRW_PREDICATE_NONE);
      brw_gs_emit_vue(c, c->reg.vertex[2], true);
      break;
   }
}
