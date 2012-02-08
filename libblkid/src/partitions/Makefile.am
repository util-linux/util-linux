include $(top_srcdir)/config/include-Makefile.am

AM_CPPFLAGS += -I$(ul_libblkid_incdir) -I$(ul_libblkid_srcdir)
libblkid_partitions_la_LIBADD =

noinst_LTLIBRARIES = libblkid_partitions.la
libblkid_partitions_la_SOURCES = \
	aix.c \
	aix.h \
	blkid_parttypes.h \
	bsd.c \
	dos.c \
	dos.h \
	gpt.c \
	mac.c \
	minix.c \
	partitions.c \
	partitions.h \
	sgi.c \
	solaris_x86.c \
	sun.c \
	ultrix.c \
	unixware.c
