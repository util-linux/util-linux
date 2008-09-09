include $(top_srcdir)/config/include-Makefile.am

AM_CPPFLAGS += -I$(top_builddir)/libs/blkid/src

lib_LIBRARIES = libprobers.a
libprobers_a_CFLAGS = -fPIC
libprobers_a_SOURCES =	probers.h \
			cramfs.c \
			swap.c \
			adaptec_raid.c \
			ddf_raid.c \
			isw_raid.c \
			jmicron_raid.c \
			lsi_raid.c \
			nvidia_raid.c \
			promise_raid.c \
			silicon_raid.c \
			via_raid.c

all-local: $(lib_LIBRARIES)
