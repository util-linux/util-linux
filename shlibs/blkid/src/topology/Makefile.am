include $(top_srcdir)/config/include-Makefile.am

AM_CPPFLAGS += -I$(ul_libblkid_incdir) -I$(ul_libblkid_srcdir)
libblkid_topology_la_LIBADD =

noinst_LTLIBRARIES = libblkid_topology.la
libblkid_topology_la_SOURCES =	topology.c \
				topology.h \
				ioctl.c \
				sysfs.c \
				md.c \
				dm.c \
				evms.c \
				lvm.c
