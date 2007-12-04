include $(top_srcdir)/config/include-Makefile.am

EXTRA_DIST = README.flushb

bin_PROGRAMS =

usrbinexec_PROGRAMS = cal ddate logger look mcookie \
	namei script whereis
EXTRA_DIST += README.cal README.ddate README.namei README.namei2

mcookie_SOURCES = mcookie.c ../lib/md5.c

usrbinexec_SCRIPTS = chkdupexe scriptreplay

CLEANFILES = chkdupexe scriptreplay

dist_man_MANS = cal.1 chkdupexe.1 ddate.1 logger.1 look.1 mcookie.1 \
	namei.1 script.1 whereis.1 scriptreplay.1

if HAVE_TINFO
cal_LDADD = -ltinfo -lncurses
else
if HAVE_NCURSES
cal_LDADD = -lncurses
else
if HAVE_TERMCAP
cal_LDADD = -ltermcap
endif
endif
endif

if HAVE_UTIL
script_LDADD = -lutil
endif

if HAVE_NCURSES
if LINUX
usrbinexec_PROGRAMS += setterm
dist_man_MANS += setterm.1
endif
if HAVE_TINFO
setterm_LDADD = -ltinfo
else
setterm_LDADD = -lncurses
endif
endif

if BUILD_KILL
bin_PROGRAMS += kill
kill_SOURCES = kill.c procs.c kill.h
dist_man_MANS += kill.1
endif

if BUILD_RENAME
usrbinexec_PROGRAMS += rename
dist_man_MANS += rename.1
endif

if BUILD_RESET
dist_usrbinexec_SCRIPTS = reset
dist_man_MANS += reset.1
endif
EXTRA_DIST += README.reset reset.033c

if BUILD_WRITE

usrbinexec_PROGRAMS += write
dist_man_MANS += write.1
write_CFLAGS = $(SUID_CFLAGS) $(AM_CFLAGS)
write_LDFLAGS = $(SUID_LDFLAGS) $(AM_LDFLAGS)

if USE_TTY_GROUP
if MAKEINSTALL_DO_CHOWN
install-exec-hook::
	chgrp tty $(DESTDIR)$(usrbinexecdir)/write
	chmod g+s $(DESTDIR)$(usrbinexecdir)/write
endif
endif
endif
