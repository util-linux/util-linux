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

if HAVE_NCURSES
if LINUX
usrbin_exec_PROGRAMS += setterm
dist_man_MANS += setterm.1
endif
if HAVE_TINFO
setterm_LDADD = -ltinfo
else
setterm_LDADD = @NCURSES_LIBS@
endif
endif

