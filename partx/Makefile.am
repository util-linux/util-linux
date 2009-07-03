include $(top_srcdir)/config/include-Makefile.am

if BUILD_PARTX

usrsbin_exec_PROGRAMS = addpart delpart partx
partx_SOURCES = bsd.c dos.c partx.c solaris.c unixware.c gpt.c crc32.c \
		efi.h gpt.h crc32.h partx.h dos.h ../lib/blkdev.c

if LINUX
partx_SOURCES += ../lib/linux_version.c
endif

dist_man_MANS = addpart.8 delpart.8 partx.8

endif
