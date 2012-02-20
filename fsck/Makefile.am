include $(top_srcdir)/config/include-Makefile.am

sbin_PROGRAMS = fsck
dist_man_MANS = fsck.8

fsck_SOURCES = fsck.c
fsck_LDADD = $(ul_libmount_la) $(ul_libblkid_la)
fsck_CFLAGS = $(AM_CFLAGS) -I$(ul_libmount_incdir) -I$(ul_libblkid_incdir)
