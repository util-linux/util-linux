include $(top_srcdir)/config/include-Makefile.am

AM_CPPFLAGS += -DTEST_PROGRAM

noinst_PROGRAMS = test_blkdev test_ismounted test_wholedisk test_mangle \
		  test_tt test_canonicalize test_strutils test_procutils
if LINUX
if HAVE_CPU_SET_T
noinst_PROGRAMS += test_cpuset test_sysfs
endif
endif

test_blkdev_SOURCES = blkdev.c
test_ismounted_SOURCES = ismounted.c
test_wholedisk_SOURCES = wholedisk.c
test_mangle_SOURCES = mangle.c
#test_at_SOURCES = at.c
test_strutils_SOURCES = strutils.c
test_procutils_SOURCES = procutils.c
if LINUX
test_cpuset_SOURCES = cpuset.c
test_sysfs_SOURCES = sysfs.c at.c
endif
test_tt_SOURCES = tt.c $(top_srcdir)/lib/mbsalign.c
test_canonicalize_SOURCES = canonicalize.c

if LINUX
test_blkdev_SOURCES += linux_version.c
endif

