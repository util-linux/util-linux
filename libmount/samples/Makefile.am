include $(top_srcdir)/config/include-Makefile.am

AM_CPPFLAGS += -I$(ul_libmount_incdir)
AM_LDFLAGS += $(ul_libmount_la)

noinst_PROGRAMS = mount

mount_SOURCES = mount.c $(top_srcdir)/lib/env.c

