include $(top_srcdir)/config/include-Makefile.am

usrsbin_exec_PROGRAMS = addpart delpart
dist_man_MANS = addpart.8 delpart.8

if BUILD_PARTX
if BUILD_LIBBLKID
usrsbin_exec_PROGRAMS += partx
partx_SOURCES = partx.c partx.h \
		$(top_srcdir)/lib/blkdev.c \
		$(top_srcdir)/lib/tt.c \
		$(top_srcdir)/lib/strutils.c

if LINUX
partx_SOURCES += $(top_srcdir)/lib/linux_version.c
endif

partx_CFLAGS = -I$(ul_libblkid_incdir)
partx_LDADD = $(ul_libblkid_la)
dist_man_MANS += partx.8
endif
endif

