include $(top_srcdir)/config/include-Makefile.am

noinst_PROGRAMS = test_blkdev test_ismounted

test_blkdev_SOURCES = blkdev.c
test_ismounted_SOURCES = ismounted.c

if LINUX
test_blkdev_SOURCES += linux_version.c
endif

test_blkdev_CFLAGS = -DTEST_PROGRAM
test_ismounted_CFLAGS = -DTEST_PROGRAM


