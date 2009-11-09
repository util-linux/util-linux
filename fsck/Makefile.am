include $(top_srcdir)/config/include-Makefile.am

sbin_PROGRAMS = fsck
dist_man_MANS = fsck.8

fsck_SOURCES = base_device.c fsck.c fsck.h ../lib/ismounted.c \
	       ../lib/fsprobe.c ../lib/canonicalize.c
fsck_LDADD =
fsck_CFLAGS = $(AM_CFLAGS)

if BUILD_LIBBLKID
fsck_LDADD += $(ul_libblkid_la)
fsck_CFLAGS += -I$(ul_libblkid_incdir)
else
fsck_LDADD += $(BLKID_LIBS)
fsck_CFLAGS += $(BLKID_CFLAGS)
endif
