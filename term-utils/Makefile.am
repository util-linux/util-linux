include $(top_srcdir)/config/include-Makefile.am

bin_PROGRAMS =
usrbin_exec_PROGRAMS =
sbin_PROGRAMS =
usrsbin_exec_PROGRAMS =
dist_man_MANS =

EXTRA_DIST = README.modems-with-agetty

if BUILD_AGETTY
sbin_PROGRAMS += agetty
dist_man_MANS += agetty.8
agetty_SOURCES = agetty.c
if !HAVE_LANGINFO
agetty_SOURCES += $(top_srcdir)/lib/langinfo.c
endif
endif
