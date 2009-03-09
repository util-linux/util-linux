include $(top_srcdir)/config/include-Makefile.am

AM_CPPFLAGS += -I$(ul_libblkid_srcdir)
AM_LDFLAGS += $(ul_libblkid_la)

sbin_PROGRAMS = blkid findfs
dist_man_MANS = blkid.8 findfs.8

if HAVE_STATIC_BLKID
sbin_PROGRAMS += blkid.static
blkid_static_SOURCES = $(blkid_SOURCES)
blkid_static_LDFLAGS = "-all-static"		# libtool option
blkid_static_LDADD = $(ul_libblkid_la)
blkid_static_CFLAGS = -I$(ul_libblkid_srcdir)
endif
