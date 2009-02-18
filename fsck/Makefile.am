include $(top_srcdir)/config/include-Makefile.am

sbin_PROGRAMS = fsck
dist_man_MANS = fsck.8

fsck_SOURCES = base_device.c fsck.c fsck.h ../lib/ismounted.c
fsck_LDADD =
fsck_CFLAGS =

if HAVE_BLKID
fsck_LDADD += $(BLKID_LIBS)
fsck_CFLAGS += $(BLKID_CFLAGS)
endif
