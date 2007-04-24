include $(top_srcdir)/config/include-Makefile.am

EXTRA_DIST = 	README.* \
		chkdupexe.pl \
		scriptreplay.pl \
		reset \
		reset.033c

bin_PROGRAMS =

usrbinexec_PROGRAMS = cal ddate logger look mcookie \
	namei script whereis

mcookie_LDADD = $(top_srcdir)/lib/libmd5.a

usrbinexec_SCRIPTS = chkdupexe scriptreplay

chkdupexe: chkdupexe.pl
	sed -e 's,[@]PERL[@],$(PERL),g' < chkdupexe.pl > chkdupexe

scriptreplay: scriptreplay.pl
	sed -e 's,[@]PERL[@],$(PERL),g' < scriptreplay.pl > scriptreplay

CLEANFILES = chkdupexe scriptreplay

man_MANS = cal.1 chkdupexe.1 ddate.1 logger.1 look.1 mcookie.1 \
	namei.1 script.1 whereis.1 scriptreplay.1

if HAVE_NCURSES
cal_LDADD = -lncurses
else
if HAVE_TERMCAP
cal_LDADD = -ltermcap
endif
endif

if HAVE_LIBUTIL
script_LDADD = -lutil
endif

if HAVE_NCURSES
usrbinexec_PROGRAMS += setterm
man_MANS += setterm.1
setterm_LDADD = -lncurses
endif

if BUILD_KILL
bin_PROGRAMS += kill
kill_SOURCES = kill.c procs.c kill.h
man_MANS += kill.1
endif

if BUILD_RENAME
usrbinexec_PROGRAMS += rename
man_MANS += rename.1
endif

if BUILD_RESET
usrbinexec_SCRIPTS += reset
man_MANS += reset.1
endif

if BUILD_WRITE

usrbinexec_PROGRAMS += write
man_MANS += write.1
write_CFLAGS = $(SUID_CFLAGS)

if USE_TTY_GROUP
install-exec-hook::
	chgrp tty $(DESTDIR)$(usrbinexecdir)/write
	chmod g+s $(DESTDIR)$(usrbinexecdir)/write
endif

endif
