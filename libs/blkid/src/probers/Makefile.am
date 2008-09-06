include $(top_srcdir)/config/include-Makefile.am

AM_CPPFLAGS += -I$(top_builddir)/libs/blkid/src

lib_LIBRARIES = libprobers.a
libprobers_a_SOURCES = probers.h cramfs.c swap.c
libprobers_a_CFLAGS = -fPIC

all-local: $(lib_LIBRARIES)
