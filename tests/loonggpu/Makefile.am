AM_CFLAGS = \
	-I $(top_srcdir)/include/drm \
	-I $(top_srcdir)/loonggpu \
	-I $(top_srcdir) \
	-pthread

LDADD = $(top_builddir)/libdrm.la \
	$(top_builddir)/loonggpu/libdrm_loonggpu.la \
	$(CUNIT_LIBS)

if HAVE_INSTALL_TESTS
bin_PROGRAMS = \
	loonggpu_test
else
noinst_PROGRAMS = \
	loonggpu_test
endif

loonggpu_test_CPPFLAGS = $(CUNIT_CFLAGS)

loonggpu_test_SOURCES = \
	loonggpu_test.c \
	loonggpu_test.h \
	basic_tests.c \
	bo_tests.c \
	dma_tests.c \
	deadlock_tests.c \
	vm_tests.c
