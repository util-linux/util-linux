include $(top_srcdir)/config/include-Makefile.am

sbin_PROGRAMS = fsck
dist_man_MANS = fsck.8

fsck_SOURCES = base_device.c fsck.c fsck.h ../lib/ismounted.c \
	       ../lib/fsprobe.c ../lib/canonicalize.c
fsck_LDADD =
fsck_CFLAGS =

if HAVE_BLKID
if BUILD_LIBBLKID
fsck_LDADD += $(ul_libblkid_la)
fsck_CFLAGS += -I$(ul_libblkid_srcdir)
else
fsck_LDADD += $(BLKID_LIBS)
fsck_CFLAGS += $(BLKID_CFLAGS)
endif
endif

if HAVE_VOLUME_ID
fsck_LDADD += $(VOLUME_ID_LIBS)
fsck_CFLAGS += $(VOLUME_ID_CFLAGS)
endif
