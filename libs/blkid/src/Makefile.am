include $(top_srcdir)/config/include-Makefile.am

SUBDIRS = probers .

AM_CPPFLAGS += -I$(top_srcdir)/libs/blkid/src

common_ldadd =

if HAVE_UUID
common_ldadd += -luuid	#TODO $(UUID_LIBS)
endif

# includes
blkidincdir = $(includedir)/blkid
blkidinc_HEADERS = blkid.h

lib_LTLIBRARIES = libblkid.la
libblkid_la_SOURCES = cache.c dev.c devname.c devno.c getsize.c llseek.c  \
		     probe.c read.c resolve.c save.c tag.c version.c verify.c \
		     encode.c blkid.h list.h blkidP.h probers/probers.h \
		     config.c evaluate.c \
		     $(blkidinc_HEADERS) \
		     $(top_srcdir)/lib/blkdev.c \
		     $(top_srcdir)/lib/linux_version.c \
		     $(top_srcdir)/lib/canonicalize.c \
		     $(top_srcdir)/lib/md5.c

libblkid_la_LIBADD = probers/libblkid_probers.la $(common_ldadd)
libblkid_la_DEPENDENCIES = $(libblkid_la_LIBADD) blkid.sym

# TODO: for unknown reason "make distcheck" requires
#       full path to the version string
libblkid_la_LDFLAGS = -Wl,--version-script=$(top_srcdir)/libs/blkid/src/blkid.sym \
                      -version-info @BLKID_VERSION_INFO@

tests = test_cache test_config test_dev test_devname test_devno test_getsize \
	test_read test_resolve test_save test_tag test_verify test_evaluate

EXTRA_DIST = blkid.sym
CLEANFILES = $(tests)

tests: all $(tests)
test_%: %.c
	$(COMPILE) -DTEST_PROGRAM $< .libs/libblkid.a -o $@ $(common_ldadd)

