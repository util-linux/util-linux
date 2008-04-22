include $(top_srcdir)/config/include-Makefile.am

noinst_PROGRAMS = test_blkdev

test_blkdev_SOURCES = 	$(top_srcdir)/lib/blkdev.c

if LINUX
test_blkdev_SOURCES += $(top_srcdir)/lib/linux_version.c
endif

test_blkdev_CFLAGS = -DMAIN_TEST_BLKDEV


