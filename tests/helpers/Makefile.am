include $(top_srcdir)/config/include-Makefile.am

noinst_PROGRAMS = mnt_test_sysinfo test_blkdev
mnt_test_sysinfo_SOURCES = mnt_test_sysinfo.c

test_blkdev_SOURCES = 	$(top_srcdir)/lib/blkdev.c \
			$(top_srcdir)/lib/linux_version.c
test_blkdev_CFLAGS = -DMAIN_TEST_BLKDEV

