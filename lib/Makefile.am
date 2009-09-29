include $(top_srcdir)/config/include-Makefile.am

AM_CPPFLAGS += -DTEST_PROGRAM

noinst_PROGRAMS = test_blkdev test_ismounted test_wholedisk

test_blkdev_SOURCES = blkdev.c
test_ismounted_SOURCES = ismounted.c
test_wholedisk_SOURCES = wholedisk.c

if LINUX
test_blkdev_SOURCES += linux_version.c
endif

