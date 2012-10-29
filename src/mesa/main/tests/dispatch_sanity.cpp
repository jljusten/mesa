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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \name dispatch_sanity.cpp
 *
 * Verify that only set of functions that should be available in a particular
 * API are available in that API.
 *
 * The list of expected functions originally came from the functions set by
 * api_exec_es2.c.  This file no longer exists in Mesa (but api_exec_es1.c was
 * still generated at the time this test was written).  It was the generated
 * file that configured the dispatch table for ES2 contexts.  This test
 * verifies that all of the functions set by the old api_exec_es2.c (with the
 * recent addition of VAO functions) are set in the dispatch table and
 * everything else is a NOP.
 *
 * When adding extensions that add new functions, this test will need to be
 * modified to expect dispatch functions for the new extension functions.
 */

extern "C" {
#include "main/mfeatures.h"
}

#include <gtest/gtest.h>

extern "C" {
#include "GL/gl.h"
#include "GL/glext.h"
#include "main/compiler.h"
#include "main/api_exec.h"
#include "main/context.h"
#include "main/remap.h"
#include "glapi/glapi.h"
#include "drivers/common/driverfuncs.h"

#include "swrast/swrast.h"
#include "vbo/vbo.h"
#include "tnl/tnl.h"
#include "swrast_setup/swrast_setup.h"

#ifndef GLAPIENTRYP
#define GLAPIENTRYP GL_APIENTRYP
#endif

#include "main/dispatch.h"
}

struct function {
   const char *name;
   int offset;
};

extern const struct function gles11_functions_possible[];
extern const struct function gles2_functions_possible[];
extern const struct function gles3_functions_possible[];

class DispatchSanity_test : public ::testing::Test {
public:
   virtual void SetUp();

   struct gl_config visual;
   struct dd_function_table driver_functions;
   struct gl_context share_list;
   struct gl_context ctx;
};

void
DispatchSanity_test::SetUp()
{
   memset(&visual, 0, sizeof(visual));
   memset(&driver_functions, 0, sizeof(driver_functions));
   memset(&share_list, 0, sizeof(share_list));
   memset(&ctx, 0, sizeof(ctx));

   _mesa_init_driver_functions(&driver_functions);
}

static const char *
offset_to_proc_name_safe(unsigned offset)
{
   const char *name = _glapi_get_proc_name(offset);
   return name ? name : "???";
}

/* Scan through the dispatch table and check that all the functions in
 * _glapi_proc *table exist. When found, set their pointers in the table
 * to _mesa_generic_nop.  */
static void
validate_functions(_glapi_proc *table, const struct function *function_table)
{
   for (unsigned i = 0; function_table[i].name != NULL; i++) {
      const int offset = (function_table[i].offset != -1)
         ? function_table[i].offset
         : _glapi_get_proc_offset(function_table[i].name);

      ASSERT_NE(-1, offset)
         << "Function: " << function_table[i].name;
      ASSERT_EQ(offset,
                _glapi_get_proc_offset(function_table[i].name))
         << "Function: " << function_table[i].name;
      EXPECT_NE((_glapi_proc) _mesa_generic_nop, table[offset])
         << "Function: " << function_table[i].name
         << " at offset " << offset;

      table[offset] = (_glapi_proc) _mesa_generic_nop;
   }
}

/* Scan through the table and ensure that there is nothing except
 * _mesa_generic_nop (as set by validate_functions().  */
static void
validate_nops(const _glapi_proc *table)
{
   const unsigned size = _glapi_get_dispatch_table_size();
   for (unsigned i = 0; i < size; i++) {
      EXPECT_EQ((_glapi_proc) _mesa_generic_nop, table[i])
         << "i = " << i << " (" << offset_to_proc_name_safe(i) << ")";
   }
}

TEST_F(DispatchSanity_test, GLES11)
{
   ctx.Version = 11;
   _mesa_initialize_context(&ctx,
                            API_OPENGLES,
                            &visual,
                            NULL /* share_list */,
                            &driver_functions);

   _swrast_CreateContext(&ctx);
   _vbo_CreateContext(&ctx);
   _tnl_CreateContext(&ctx);
   _swsetup_CreateContext(&ctx);

   validate_functions((_glapi_proc *) ctx.Exec, gles11_functions_possible);
   validate_nops((_glapi_proc *) ctx.Exec);
}

TEST_F(DispatchSanity_test, GLES2)
{
   ctx.Version = 20;
   _mesa_initialize_context(&ctx,
                            API_OPENGLES2, //api,
                            &visual,
                            NULL, //&share_list,
                            &driver_functions);

   _swrast_CreateContext(&ctx);
   _vbo_CreateContext(&ctx);
   _tnl_CreateContext(&ctx);
   _swsetup_CreateContext(&ctx);

   validate_functions((_glapi_proc *) ctx.Exec, gles2_functions_possible);
   validate_nops((_glapi_proc *) ctx.Exec);
}

TEST_F(DispatchSanity_test, GLES3)
{
   ctx.Version = 30;
   _mesa_initialize_context(&ctx,
                            API_OPENGLES2, //api,
                            &visual,
                            NULL, //&share_list,
                            &driver_functions);

   _swrast_CreateContext(&ctx);
   _vbo_CreateContext(&ctx);
   _tnl_CreateContext(&ctx);
   _swsetup_CreateContext(&ctx);

   validate_functions((_glapi_proc *) ctx.Exec, gles2_functions_possible);
   validate_functions((_glapi_proc *) ctx.Exec, gles3_functions_possible);
   validate_nops((_glapi_proc *) ctx.Exec);
}

const struct function gles11_functions_possible[] = {
   { "glActiveTexture", _gloffset_ActiveTextureARB },
   { "glAlphaFunc", _gloffset_AlphaFunc },
   { "glAlphaFuncx", -1 },
   { "glBindBuffer", -1 },
   { "glBindFramebufferOES", -1 },
   { "glBindRenderbufferOES", -1 },
   { "glBindTexture", _gloffset_BindTexture },
   { "glBlendEquationOES", _gloffset_BlendEquation },
   { "glBlendEquationSeparateOES", -1 },
   { "glBlendFunc", _gloffset_BlendFunc },
   { "glBlendFuncSeparateOES", -1 },
   { "glBufferData", -1 },
   { "glBufferSubData", -1 },
   { "glCheckFramebufferStatusOES", -1 },
   { "glClear", _gloffset_Clear },
   { "glClearColor", _gloffset_ClearColor },
   { "glClearColorx", -1 },
   { "glClearDepthf", -1 },
   { "glClearDepthx", -1 },
   { "glClearStencil", _gloffset_ClearStencil },
   { "glClientActiveTexture", _gloffset_ClientActiveTextureARB },
   { "glClipPlanef", -1 },
   { "glClipPlanex", -1 },
   { "glColor4f", _gloffset_Color4f },
   { "glColor4ub", _gloffset_Color4ub },
   { "glColor4x", -1 },
   { "glColorMask", _gloffset_ColorMask },
   { "glColorPointer", _gloffset_ColorPointer },
   { "glCompressedTexImage2D", -1 },
   { "glCompressedTexSubImage2D", -1 },
   { "glCopyTexImage2D", _gloffset_CopyTexImage2D },
   { "glCopyTexSubImage2D", _gloffset_CopyTexSubImage2D },
   { "glCullFace", _gloffset_CullFace },
   { "glDeleteBuffers", -1 },
   { "glDeleteFramebuffersOES", -1 },
   { "glDeleteRenderbuffersOES", -1 },
   { "glDeleteTextures", _gloffset_DeleteTextures },
   { "glDepthFunc", _gloffset_DepthFunc },
   { "glDepthMask", _gloffset_DepthMask },
   { "glDepthRangef", -1 },
   { "glDepthRangex", -1 },
   { "glDisable", _gloffset_Disable },
   { "glDisableClientState", _gloffset_DisableClientState },
   { "glDrawArrays", _gloffset_DrawArrays },
   { "glDrawElements", _gloffset_DrawElements },
   { "glDrawTexfOES", -1 },
   { "glDrawTexfvOES", -1 },
   { "glDrawTexiOES", -1 },
   { "glDrawTexivOES", -1 },
   { "glDrawTexsOES", -1 },
   { "glDrawTexsvOES", -1 },
   { "glDrawTexxOES", -1 },
   { "glDrawTexxvOES", -1 },
   { "glEGLImageTargetRenderbufferStorageOES", -1 },
   { "glEGLImageTargetTexture2DOES", -1 },
   { "glEnable", _gloffset_Enable },
   { "glEnableClientState", _gloffset_EnableClientState },
   { "glFinish", _gloffset_Finish },
   { "glFlush", _gloffset_Flush },
   { "glFlushMappedBufferRangeEXT", -1 },
   { "glFogf", _gloffset_Fogf },
   { "glFogfv", _gloffset_Fogfv },
   { "glFogx", -1 },
   { "glFogxv", -1 },
   { "glFramebufferRenderbufferOES", -1 },
   { "glFramebufferTexture2DOES", -1 },
   { "glFrontFace", _gloffset_FrontFace },
   { "glFrustumf", -1 },
   { "glFrustumx", -1 },
   { "glGenBuffers", -1 },
   { "glGenFramebuffersOES", -1 },
   { "glGenRenderbuffersOES", -1 },
   { "glGenTextures", _gloffset_GenTextures },
   { "glGenerateMipmapOES", -1 },
   { "glGetBooleanv", _gloffset_GetBooleanv },
   { "glGetBufferParameteriv", -1 },
   { "glGetBufferPointervOES", -1 },
   { "glGetClipPlanef", -1 },
   { "glGetClipPlanex", -1 },
   { "glGetError", _gloffset_GetError },
   { "glGetFixedv", -1 },
   { "glGetFloatv", _gloffset_GetFloatv },
   { "glGetFramebufferAttachmentParameterivOES", -1 },
   { "glGetIntegerv", _gloffset_GetIntegerv },
   { "glGetLightfv", _gloffset_GetLightfv },
   { "glGetLightxv", -1 },
   { "glGetMaterialfv", _gloffset_GetMaterialfv },
   { "glGetMaterialxv", -1 },
   { "glGetPointerv", _gloffset_GetPointerv },
   { "glGetRenderbufferParameterivOES", -1 },
   { "glGetString", _gloffset_GetString },
   { "glGetTexEnvfv", _gloffset_GetTexEnvfv },
   { "glGetTexEnviv", _gloffset_GetTexEnviv },
   { "glGetTexEnvxv", -1 },
   { "glGetTexGenfvOES", _gloffset_GetTexGenfv },
   { "glGetTexGenivOES", _gloffset_GetTexGeniv },
   { "glGetTexGenxvOES", -1 },
   { "glGetTexParameterfv", _gloffset_GetTexParameterfv },
   { "glGetTexParameteriv", _gloffset_GetTexParameteriv },
   { "glGetTexParameterxv", -1 },
   { "glHint", _gloffset_Hint },
   { "glIsBuffer", -1 },
   { "glIsEnabled", _gloffset_IsEnabled },
   { "glIsFramebufferOES", -1 },
   { "glIsRenderbufferOES", -1 },
   { "glIsTexture", _gloffset_IsTexture },
   { "glLightModelf", _gloffset_LightModelf },
   { "glLightModelfv", _gloffset_LightModelfv },
   { "glLightModelx", -1 },
   { "glLightModelxv", -1 },
   { "glLightf", _gloffset_Lightf },
   { "glLightfv", _gloffset_Lightfv },
   { "glLightx", -1 },
   { "glLightxv", -1 },
   { "glLineWidth", _gloffset_LineWidth },
   { "glLineWidthx", -1 },
   { "glLoadIdentity", _gloffset_LoadIdentity },
   { "glLoadMatrixf", _gloffset_LoadMatrixf },
   { "glLoadMatrixx", -1 },
   { "glLogicOp", _gloffset_LogicOp },
   { "glMapBufferOES", -1 },
   { "glMapBufferRangeEXT", -1 },
   { "glMaterialf", _gloffset_Materialf },
   { "glMaterialfv", _gloffset_Materialfv },
   { "glMaterialx", -1 },
   { "glMaterialxv", -1 },
   { "glMatrixMode", _gloffset_MatrixMode },
   { "glMultMatrixf", _gloffset_MultMatrixf },
   { "glMultMatrixx", -1 },
   { "glMultiDrawArraysEXT", -1 },
   { "glMultiDrawElementsEXT", -1 },
   { "glMultiTexCoord4f", _gloffset_MultiTexCoord4fARB },
   { "glMultiTexCoord4x", -1 },
   { "glNormal3f", _gloffset_Normal3f },
   { "glNormal3x", -1 },
   { "glNormalPointer", _gloffset_NormalPointer },
   { "glOrthof", -1 },
   { "glOrthox", -1 },
   { "glPixelStorei", _gloffset_PixelStorei },
   { "glPointParameterf", -1 },
   { "glPointParameterfv", -1 },
   { "glPointParameterx", -1 },
   { "glPointParameterxv", -1 },
   { "glPointSize", _gloffset_PointSize },
   { "glPointSizePointerOES", -1 },
   { "glPointSizex", -1 },
   { "glPolygonOffset", _gloffset_PolygonOffset },
   { "glPolygonOffsetx", -1 },
   { "glPopMatrix", _gloffset_PopMatrix },
   { "glPushMatrix", _gloffset_PushMatrix },
   { "glQueryMatrixxOES", -1 },
   { "glReadPixels", _gloffset_ReadPixels },
   { "glRenderbufferStorageOES", -1 },
   { "glRotatef", _gloffset_Rotatef },
   { "glRotatex", -1 },
   { "glSampleCoverage", -1 },
   { "glSampleCoveragex", -1 },
   { "glScalef", _gloffset_Scalef },
   { "glScalex", -1 },
   { "glScissor", _gloffset_Scissor },
   { "glShadeModel", _gloffset_ShadeModel },
   { "glStencilFunc", _gloffset_StencilFunc },
   { "glStencilMask", _gloffset_StencilMask },
   { "glStencilOp", _gloffset_StencilOp },
   { "glTexCoordPointer", _gloffset_TexCoordPointer },
   { "glTexEnvf", _gloffset_TexEnvf },
   { "glTexEnvfv", _gloffset_TexEnvfv },
   { "glTexEnvi", _gloffset_TexEnvi },
   { "glTexEnviv", _gloffset_TexEnviv },
   { "glTexEnvx", -1 },
   { "glTexEnvxv", -1 },
   { "glTexGenfOES", _gloffset_TexGenf },
   { "glTexGenfvOES", _gloffset_TexGenfv },
   { "glTexGeniOES", _gloffset_TexGeni },
   { "glTexGenivOES", _gloffset_TexGeniv },
   { "glTexGenxOES", -1 },
   { "glTexGenxvOES", -1 },
   { "glTexImage2D", _gloffset_TexImage2D },
   { "glTexParameterf", _gloffset_TexParameterf },
   { "glTexParameterfv", _gloffset_TexParameterfv },
   { "glTexParameteri", _gloffset_TexParameteri },
   { "glTexParameteriv", _gloffset_TexParameteriv },
   { "glTexParameterx", -1 },
   { "glTexParameterxv", -1 },
   { "glTexSubImage2D", _gloffset_TexSubImage2D },
   { "glTranslatef", _gloffset_Translatef },
   { "glTranslatex", -1 },
   { "glUnmapBufferOES", -1 },
   { "glVertexPointer", _gloffset_VertexPointer },
   { "glViewport", _gloffset_Viewport },
   { NULL, -1 }
};

const struct function gles2_functions_possible[] = {
   { "glActiveTexture", _gloffset_ActiveTextureARB },
   { "glAttachShader", -1 },
   { "glBindAttribLocation", -1 },
   { "glBindBuffer", -1 },
   { "glBindFramebuffer", -1 },
   { "glBindRenderbuffer", -1 },
   { "glBindTexture", _gloffset_BindTexture },
   { "glBindVertexArrayOES", -1 },
   { "glBlendColor", _gloffset_BlendColor },
   { "glBlendEquation", _gloffset_BlendEquation },
   { "glBlendEquationSeparate", -1 },
   { "glBlendFunc", _gloffset_BlendFunc },
   { "glBlendFuncSeparate", -1 },
   { "glBufferData", -1 },
   { "glBufferSubData", -1 },
   { "glCheckFramebufferStatus", -1 },
   { "glClear", _gloffset_Clear },
   { "glClearColor", _gloffset_ClearColor },
   { "glClearDepthf", -1 },
   { "glClearStencil", _gloffset_ClearStencil },
   { "glColorMask", _gloffset_ColorMask },
   { "glCompileShader", -1 },
   { "glCompressedTexImage2D", -1 },
   { "glCompressedTexImage3DOES", -1 },
   { "glCompressedTexSubImage2D", -1 },
   { "glCompressedTexSubImage3DOES", -1 },
   { "glCopyTexImage2D", _gloffset_CopyTexImage2D },
   { "glCopyTexSubImage2D", _gloffset_CopyTexSubImage2D },
   { "glCopyTexSubImage3DOES", _gloffset_CopyTexSubImage3D },
   { "glCreateProgram", -1 },
   { "glCreateShader", -1 },
   { "glCullFace", _gloffset_CullFace },
   { "glDeleteBuffers", -1 },
   { "glDeleteFramebuffers", -1 },
   { "glDeleteProgram", -1 },
   { "glDeleteRenderbuffers", -1 },
   { "glDeleteShader", -1 },
   { "glDeleteTextures", _gloffset_DeleteTextures },
   { "glDeleteVertexArraysOES", -1 },
   { "glDepthFunc", _gloffset_DepthFunc },
   { "glDepthMask", _gloffset_DepthMask },
   { "glDepthRangef", -1 },
   { "glDetachShader", -1 },
   { "glDisable", _gloffset_Disable },
   { "glDisableVertexAttribArray", -1 },
   { "glDrawArrays", _gloffset_DrawArrays },
   { "glDrawBuffersNV", -1 },
   { "glDrawElements", _gloffset_DrawElements },
   { "glEGLImageTargetRenderbufferStorageOES", -1 },
   { "glEGLImageTargetTexture2DOES", -1 },
   { "glEnable", _gloffset_Enable },
   { "glEnableVertexAttribArray", -1 },
   { "glFinish", _gloffset_Finish },
   { "glFlush", _gloffset_Flush },
   { "glFlushMappedBufferRangeEXT", -1 },
   { "glFramebufferRenderbuffer", -1 },
   { "glFramebufferTexture2D", -1 },
   { "glFramebufferTexture3DOES", -1 },
   { "glFrontFace", _gloffset_FrontFace },
   { "glGenBuffers", -1 },
   { "glGenFramebuffers", -1 },
   { "glGenRenderbuffers", -1 },
   { "glGenTextures", _gloffset_GenTextures },
   { "glGenVertexArraysOES", -1 },
   { "glGenerateMipmap", -1 },
   { "glGetActiveAttrib", -1 },
   { "glGetActiveUniform", -1 },
   { "glGetAttachedShaders", -1 },
   { "glGetAttribLocation", -1 },
   { "glGetBooleanv", _gloffset_GetBooleanv },
   { "glGetBufferParameteriv", -1 },
   { "glGetBufferPointervOES", -1 },
   { "glGetError", _gloffset_GetError },
   { "glGetFloatv", _gloffset_GetFloatv },
   { "glGetFramebufferAttachmentParameteriv", -1 },
   { "glGetIntegerv", _gloffset_GetIntegerv },
   { "glGetProgramInfoLog", -1 },
   { "glGetProgramiv", -1 },
   { "glGetRenderbufferParameteriv", -1 },
   { "glGetShaderInfoLog", -1 },
   { "glGetShaderPrecisionFormat", -1 },
   { "glGetShaderSource", -1 },
   { "glGetShaderiv", -1 },
   { "glGetString", _gloffset_GetString },
   { "glGetTexParameterfv", _gloffset_GetTexParameterfv },
   { "glGetTexParameteriv", _gloffset_GetTexParameteriv },
   { "glGetUniformLocation", -1 },
   { "glGetUniformfv", -1 },
   { "glGetUniformiv", -1 },
   { "glGetVertexAttribPointerv", -1 },
   { "glGetVertexAttribfv", -1 },
   { "glGetVertexAttribiv", -1 },
   { "glHint", _gloffset_Hint },
   { "glIsBuffer", -1 },
   { "glIsEnabled", _gloffset_IsEnabled },
   { "glIsFramebuffer", -1 },
   { "glIsProgram", -1 },
   { "glIsRenderbuffer", -1 },
   { "glIsShader", -1 },
   { "glIsTexture", _gloffset_IsTexture },
   { "glIsVertexArrayOES", -1 },
   { "glLineWidth", _gloffset_LineWidth },
   { "glLinkProgram", -1 },
   { "glMapBufferOES", -1 },
   { "glMapBufferRangeEXT", -1 },
   { "glMultiDrawArraysEXT", -1 },
   { "glMultiDrawElementsEXT", -1 },
   { "glPixelStorei", _gloffset_PixelStorei },
   { "glPolygonOffset", _gloffset_PolygonOffset },
   { "glReadBufferNV", _gloffset_ReadBuffer },
   { "glReadPixels", _gloffset_ReadPixels },
   { "glReleaseShaderCompiler", -1 },
   { "glRenderbufferStorage", -1 },
   { "glSampleCoverage", -1 },
   { "glScissor", _gloffset_Scissor },
   { "glShaderBinary", -1 },
   { "glShaderSource", -1 },
   { "glStencilFunc", _gloffset_StencilFunc },
   { "glStencilFuncSeparate", -1 },
   { "glStencilMask", _gloffset_StencilMask },
   { "glStencilMaskSeparate", -1 },
   { "glStencilOp", _gloffset_StencilOp },
   { "glStencilOpSeparate", -1 },
   { "glTexImage2D", _gloffset_TexImage2D },
   { "glTexImage3DOES", _gloffset_TexImage3D },
   { "glTexParameterf", _gloffset_TexParameterf },
   { "glTexParameterfv", _gloffset_TexParameterfv },
   { "glTexParameteri", _gloffset_TexParameteri },
   { "glTexParameteriv", _gloffset_TexParameteriv },
   { "glTexSubImage2D", _gloffset_TexSubImage2D },
   { "glTexSubImage3DOES", _gloffset_TexSubImage3D },
   { "glUniform1f", -1 },
   { "glUniform1fv", -1 },
   { "glUniform1i", -1 },
   { "glUniform1iv", -1 },
   { "glUniform2f", -1 },
   { "glUniform2fv", -1 },
   { "glUniform2i", -1 },
   { "glUniform2iv", -1 },
   { "glUniform3f", -1 },
   { "glUniform3fv", -1 },
   { "glUniform3i", -1 },
   { "glUniform3iv", -1 },
   { "glUniform4f", -1 },
   { "glUniform4fv", -1 },
   { "glUniform4i", -1 },
   { "glUniform4iv", -1 },
   { "glUniformMatrix2fv", -1 },
   { "glUniformMatrix3fv", -1 },
   { "glUniformMatrix4fv", -1 },
   { "glUnmapBufferOES", -1 },
   { "glUseProgram", -1 },
   { "glValidateProgram", -1 },
   { "glVertexAttrib1f", -1 },
   { "glVertexAttrib1fv", -1 },
   { "glVertexAttrib2f", -1 },
   { "glVertexAttrib2fv", -1 },
   { "glVertexAttrib3f", -1 },
   { "glVertexAttrib3fv", -1 },
   { "glVertexAttrib4f", -1 },
   { "glVertexAttrib4fv", -1 },
   { "glVertexAttribPointer", -1 },
   { "glViewport", _gloffset_Viewport },
   { NULL, -1 }
};

const struct function gles3_functions_possible[] = {
   { "glBeginQuery", -1 },
   { "glBeginTransformFeedback", -1 },
   { "glBindBufferBase", -1 },
   { "glBindBufferRange", -1 },
   { "glBindSampler", -1 },
   { "glBindTransformFeedback", -1 },
   // We check for the aliased -OES version in GLES 2
   // { "glBindVertexArray", -1 },
   { "glBlitFramebuffer", -1 },
   { "glClearBufferfi", -1 },
   { "glClearBufferfv", -1 },
   { "glClearBufferiv", -1 },
   { "glClearBufferuiv", -1 },
   { "glClientWaitSync", -1 },
   // We check for the aliased -OES version in GLES 2
   // { "glCompressedTexImage3D", -1 },
   // We check for the aliased -OES version in GLES 2
   // { "glCompressedTexSubImage3D", -1 },
   { "glCopyBufferSubData", -1 },
   // We check for the aliased -OES version in GLES 2
   // { "glCopyTexSubImage3D", -1 },
   { "glDeleteQueries", -1 },
   { "glDeleteSamplers", -1 },
   { "glDeleteSync", -1 },
   { "glDeleteTransformFeedbacks", -1 },
   // We check for the aliased -OES version in GLES 2
   // { "glDeleteVertexArrays", -1 },
   { "glDrawArraysInstanced", -1 },
   // We check for the aliased -NV version in GLES 2
   // { "glDrawBuffers", -1 },
   { "glDrawElementsInstanced", -1 },
   { "glDrawRangeElements", -1 },
   { "glEndQuery", -1 },
   { "glEndTransformFeedback", -1 },
   { "glFenceSync", -1 },
   // We check for the aliased -EXT version in GLES 2
   // { "glFlushMappedBufferRange", -1 },
   { "glFramebufferTextureLayer", -1 },
   { "glGenQueries", -1 },
   { "glGenSamplers", -1 },
   { "glGenTransformFeedbacks", -1 },
   // We check for the aliased -OES version in GLES 2
   // { "glGenVertexArrays", -1 },
   { "glGetActiveUniformBlockiv", -1 },
   { "glGetActiveUniformBlockName", -1 },
   { "glGetActiveUniformsiv", -1 },
   // We have an implementation (added Jan 1 2010, 1fbc7193) but never tested...
   // { "glGetBufferParameteri64v", -1 },
   // We check for the aliased -OES version in GLES 2
   // { "glGetBufferPointerv", -1 },
   { "glGetFragDataLocation", -1 },
   /// XXX: Missing implementation of glGetInteger64i_v
   // { "glGetInteger64i_v", -1 },
   { "glGetInteger64v", -1 },
   { "glGetIntegeri_v", -1 },
   // XXX: Missing implementation of ARB_internalformat_query
   // { "glGetInternalformativ", -1 },
   // XXX: Missing implementation of ARB_get_program_binary
   /// { "glGetProgramBinary", -1 },
   { "glGetQueryiv", -1 },
   { "glGetQueryObjectuiv", -1 },
   { "glGetSamplerParameterfv", -1 },
   { "glGetSamplerParameteriv", -1 },
   { "glGetStringi", -1 },
   { "glGetSynciv", -1 },
   { "glGetTransformFeedbackVarying", -1 },
   { "glGetUniformBlockIndex", -1 },
   { "glGetUniformIndices", -1 },
   { "glGetUniformuiv", -1 },
   { "glGetVertexAttribIiv", -1 },
   { "glGetVertexAttribIuiv", -1 },
   { "glInvalidateFramebuffer", -1 },
   { "glInvalidateSubFramebuffer", -1 },
   { "glIsQuery", -1 },
   { "glIsSampler", -1 },
   { "glIsSync", -1 },
   { "glIsTransformFeedback", -1 },
   // We check for the aliased -OES version in GLES 2
   // { "glIsVertexArray", -1 },
   // We check for the aliased -EXT version in GLES 2
   // { "glMapBufferRange", -1 },
   { "glPauseTransformFeedback", -1 },
   // XXX: Missing implementation of ARB_get_program_binary
   // { "glProgramBinary", -1 },
   // XXX: Missing implementation of ARB_get_program_binary
   // { "glProgramParameteri", -1 },
   // We check for the aliased -NV version in GLES 2
   // { "glReadBuffer", -1 },
   { "glRenderbufferStorageMultisample", -1 },
   { "glResumeTransformFeedback", -1 },
   { "glSamplerParameterf", -1 },
   { "glSamplerParameterfv", -1 },
   { "glSamplerParameteri", -1 },
   { "glSamplerParameteriv", -1 },
   // We check for the aliased -OES version in GLES 2
   // { "glTexImage3D", -1 },
   { "glTexStorage2D", -1 },
   { "glTexStorage3D", -1 },
   // We check for the aliased -OES version in GLES 2
   // { "glTexSubImage3D", -1 },
   { "glTransformFeedbackVaryings", -1 },
   { "glUniform1ui", -1 },
   { "glUniform1uiv", -1 },
   { "glUniform2ui", -1 },
   { "glUniform2uiv", -1 },
   { "glUniform3ui", -1 },
   { "glUniform3uiv", -1 },
   { "glUniform4ui", -1 },
   { "glUniform4uiv", -1 },
   { "glUniformBlockBinding", -1 },
   { "glUniformMatrix2x3fv", -1 },
   { "glUniformMatrix2x4fv", -1 },
   { "glUniformMatrix3x2fv", -1 },
   { "glUniformMatrix3x4fv", -1 },
   { "glUniformMatrix4x2fv", -1 },
   { "glUniformMatrix4x3fv", -1 },
   // We check for the aliased -OES version in GLES 2
   // { "glUnmapBuffer", -1 },
   { "glVertexAttribDivisor", -1 },
   { "glVertexAttribI4i", -1 },
   { "glVertexAttribI4iv", -1 },
   { "glVertexAttribI4ui", -1 },
   { "glVertexAttribI4uiv", -1 },
   { "glVertexAttribIPointer", -1 },
   { "glWaitSync", -1 },
   { NULL, -1 }
};
