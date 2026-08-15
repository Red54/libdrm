# Copyright © 2021 Loongson Technology, Ltc
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
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#
# Authors:
#    liyi  <liyi@loongson.cn>
#    ZhiJie.Zhang <zhangzhijie@loongson.cn>

include Makefile.sources

AM_CFLAGS = \
	$(WARN_CFLAGS) \
	-I$(top_srcdir) \
	$(PTHREADSTUBS_CFLAGS) \
	-I$(top_srcdir)/include/drm

libdrmdatadir = @libdrmdatadir@
AM_CPPFLAGS = -DLOONGGPU_ASIC_ID_TABLE=\"${libdrmdatadir}/loonggpu.ids\"

libdrm_loonggpu_la_LTLIBRARIES = libdrm_loonggpu.la
libdrm_loonggpu_ladir = $(libdir)
libdrm_loonggpu_la_LDFLAGS = -version-number 1:0:0 -no-undefined
libdrm_loonggpu_la_LIBADD = ../libdrm.la @PTHREADSTUBS_LIBS@

libdrm_loonggpu_la_SOURCES = $(LIBDRM_LOONGGPU_FILES)
loonggpu_asic_id.lo: $(top_srcdir)/data/loonggpu.ids

libdrm_loonggpuincludedir = ${includedir}/libdrm
libdrm_loonggpuinclude_HEADERS = $(LIBDRM_LOONGGPU_H_FILES)

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = libdrm_loonggpu.pc

TESTS = loonggpu-symbol-check
EXTRA_DIST = $(TESTS)
