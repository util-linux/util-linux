include $(top_srcdir)/config/include-Makefile.am

if BUILD_PARTX

usrsbinexec_PROGRAMS = addpart delpart partx
partx_SOURCES = bsd.c dos.c partx.c solaris.c unixware.c gpt.c crc32.c \
		efi.h gpt.h crc32.h partx.h dos.h

dist_man_MANS = addpart.8 delpart.8 partx.8

endif
