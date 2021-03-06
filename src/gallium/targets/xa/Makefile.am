# Copyright © 2012 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
# HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

include $(top_srcdir)/src/gallium/Automake.inc

AM_CFLAGS = \
	$(GALLIUM_TARGET_CFLAGS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = xatracker.pc

lib_LTLIBRARIES = libxatracker.la

nodist_EXTRA_libxatracker_la_SOURCES = dummy.cpp
libxatracker_la_SOURCES =

libxatracker_la_LIBADD = \
	$(top_builddir)/src/gallium/state_trackers/xa/libxatracker.la \
	$(top_builddir)/src/gallium/auxiliary/libgallium.la \
	$(LIBDRM_LIBS) \
	$(GALLIUM_COMMON_LIB_DEPS)

libxatracker_la_LDFLAGS = \
	-no-undefined \
	-version-number $(XA_MAJOR):$(XA_MINOR):$(XA_TINY) \
	$(GC_SECTIONS) \
	$(LD_NO_UNDEFINED)

if HAVE_LD_VERSION_SCRIPT
libxatracker_la_LDFLAGS += \
	-Wl,--version-script=$(top_srcdir)/src/gallium/targets/xa/xa.sym
endif

if HAVE_GALLIUM_STATIC_TARGETS

STATIC_TARGET_CPPFLAGS =
STATIC_TARGET_LIB_DEPS = \
	$(top_builddir)/src/loader/libloader.la

if HAVE_GALLIUM_I915
STATIC_TARGET_CPPFLAGS += -DGALLIUM_I915
STATIC_TARGET_LIB_DEPS += \
	$(top_builddir)/src/gallium/winsys/i915/drm/libi915drm.la \
	$(top_builddir)/src/gallium/drivers/i915/libi915.la \
	$(INTEL_LIBS)
endif

if HAVE_GALLIUM_ILO
STATIC_TARGET_CPPFLAGS += -DGALLIUM_ILO
STATIC_TARGET_LIB_DEPS += \
	$(top_builddir)/src/gallium/winsys/intel/drm/libintelwinsys.la \
	$(top_builddir)/src/gallium/drivers/ilo/libilo.la \
	$(INTEL_LIBS)
endif

if HAVE_GALLIUM_NOUVEAU
STATIC_TARGET_CPPFLAGS += -DGALLIUM_NOUVEAU
STATIC_TARGET_LIB_DEPS += \
	$(top_builddir)/src/gallium/winsys/nouveau/drm/libnouveaudrm.la \
	$(top_builddir)/src/gallium/drivers/nouveau/libnouveau.la \
	$(NOUVEAU_LIBS)
endif

if NEED_RADEON_DRM_WINSYS
STATIC_TARGET_LIB_DEPS += \
	$(top_builddir)/src/gallium/winsys/radeon/drm/libradeonwinsys.la
endif

if HAVE_GALLIUM_RADEON_COMMON
STATIC_TARGET_LIB_DEPS += \
	$(top_builddir)/src/gallium/drivers/radeon/libradeon.la
endif

if HAVE_GALLIUM_R300
STATIC_TARGET_CPPFLAGS += -DGALLIUM_R300
STATIC_TARGET_LIB_DEPS += \
	$(top_builddir)/src/gallium/drivers/r300/libr300.la \
	$(top_builddir)/src/gallium/drivers/r300/libr300-helper.la \
	$(RADEON_LIBS)
endif

if HAVE_GALLIUM_R600
STATIC_TARGET_CPPFLAGS += -DGALLIUM_R600
STATIC_TARGET_LIB_DEPS += \
	$(top_builddir)/src/gallium/drivers/r600/libr600.la \
	$(RADEON_LIBS)
endif

if HAVE_GALLIUM_RADEONSI
STATIC_TARGET_CPPFLAGS += -DGALLIUM_RADEONSI
STATIC_TARGET_LIB_DEPS += \
	$(top_builddir)/src/gallium/drivers/radeonsi/libradeonsi.la \
	$(RADEON_LIBS)
endif

if HAVE_GALLIUM_SVGA
STATIC_TARGET_CPPFLAGS += -DGALLIUM_VMWGFX
STATIC_TARGET_LIB_DEPS += \
	$(top_builddir)/src/gallium/winsys/svga/drm/libsvgadrm.la \
	$(top_builddir)/src/gallium/drivers/svga/libsvga.la
endif

if HAVE_GALLIUM_FREEDRENO
STATIC_TARGET_CPPFLAGS += -DGALLIUM_FREEDRENO
STATIC_TARGET_LIB_DEPS += \
	$(top_builddir)/src/gallium/winsys/freedreno/drm/libfreedrenodrm.la \
	$(top_builddir)/src/gallium/drivers/freedreno/libfreedreno.la \
	$(FREEDRENO_LIBS)

endif

libxatracker_la_SOURCES += target.c
libxatracker_la_CPPFLAGS = $(STATIC_TARGET_CPPFLAGS)
libxatracker_la_LIBADD += $(STATIC_TARGET_LIB_DEPS)

else # HAVE_GALLIUM_STATIC_TARGETS

libxatracker_la_LIBADD += \
	$(top_builddir)/src/gallium/auxiliary/pipe-loader/libpipe_loader.la \
	$(GALLIUM_PIPE_LOADER_WINSYS_LIBS) \
	$(GALLIUM_PIPE_LOADER_LIBS)

endif # HAVE_GALLIUM_STATIC_TARGETS

if HAVE_MESA_LLVM
libxatracker_la_LIBADD += $(LLVM_LIBS)
libxatracker_la_LDFLAGS += $(LLVM_LDFLAGS)
endif

include $(top_srcdir)/install-lib-links.mk
