AM_CFLAGS = \
	-I $(top_srcdir)/include/drm \
	-I $(top_srcdir)/gsgpu \
	-I $(top_srcdir) \
	-pthread

LDADD = $(top_builddir)/libdrm.la \
	$(top_builddir)/gsgpu/libdrm_gsgpu.la \
	$(CUNIT_LIBS)

if HAVE_INSTALL_TESTS
bin_PROGRAMS = \
	gsgpu_test
else
noinst_PROGRAMS = \
	gsgpu_test
endif

gsgpu_test_CPPFLAGS = $(CUNIT_CFLAGS)

gsgpu_test_SOURCES = \
	gsgpu_test.c \
	gsgpu_test.h \
	basic_tests.c \
	bo_tests.c \
	dma_tests.c \
	deadlock_tests.c \
	vm_tests.c
