include $(top_srcdir)/config/include-Makefile.am

EXTRA_DIST = README.flushb uuidd.rc

bin_PROGRAMS =
sbin_PROGRAMS =
usrsbin_exec_PROGRAMS =

usrbin_exec_PROGRAMS = cal ddate logger look mcookie \
	namei script whereis scriptreplay
EXTRA_DIST += README.cal README.ddate README.namei README.namei2

mcookie_SOURCES = mcookie.c ../lib/md5.c

usrbin_exec_SCRIPTS = chkdupexe

CLEANFILES = chkdupexe

dist_man_MANS = cal.1 chkdupexe.1 ddate.1 logger.1 look.1 mcookie.1 \
	namei.1 script.1 whereis.1 scriptreplay.1

if BUILD_LIBUUID
usrbin_exec_PROGRAMS += uuidgen
dist_man_MANS += uuidgen.1
uuidgen_LDADD = $(ul_libuuid_la)
uuidgen_CFLAGS = $(AM_CFLAGS) -I$(ul_libuuid_srcdir)
if BUILD_UUIDD
usrsbin_exec_PROGRAMS += uuidd
dist_man_MANS += uuidd.8
uuidd_LDADD = $(ul_libuuid_la)
uuidd_CFLAGS = $(AM_CFLAGS) -I$(ul_libuuid_srcdir)
endif
endif

if BUILD_LIBBLKID
sbin_PROGRAMS += blkid findfs wipefs
dist_man_MANS += blkid.8 findfs.8 wipefs.8
blkid_SOURCES = blkid.c $(top_srcdir)/lib/ismounted.c
blkid_LDADD = $(ul_libblkid_la)
blkid_CFLAGS = $(AM_CFLAGS) -I$(ul_libblkid_incdir)
findfs_LDADD = $(ul_libblkid_la)
findfs_CFLAGS = $(AM_CFLAGS) -I$(ul_libblkid_incdir)
wipefs_LDADD = $(ul_libblkid_la)
wipefs_CFLAGS = $(AM_CFLAGS) -I$(ul_libblkid_incdir)
if HAVE_STATIC_BLKID
sbin_PROGRAMS += blkid.static
blkid_static_SOURCES = $(blkid_SOURCES)
blkid_static_LDFLAGS = -all-static
blkid_static_LDADD = $(ul_libblkid_la)
blkid_static_CFLAGS = $(AM_CFLAGS) -I$(ul_libblkid_incdir)
endif
endif

if HAVE_TINFO
cal_LDADD = -ltinfo @NCURSES_LIBS@
else
if HAVE_NCURSES
cal_LDADD = @NCURSES_LIBS@
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
usrbin_exec_PROGRAMS += setterm
dist_man_MANS += setterm.1
endif
if HAVE_TINFO
setterm_LDADD = -ltinfo
else
setterm_LDADD = @NCURSES_LIBS@
endif
endif

if BUILD_KILL
bin_PROGRAMS += kill
kill_SOURCES = kill.c procs.c kill.h
dist_man_MANS += kill.1
endif

if BUILD_RENAME
usrbin_exec_PROGRAMS += rename
dist_man_MANS += rename.1
endif

if BUILD_RESET
dist_usrbin_exec_SCRIPTS = reset
dist_man_MANS += reset.1
endif
EXTRA_DIST += README.reset reset.033c

if BUILD_WRITE
usrbin_exec_PROGRAMS += write
dist_man_MANS += write.1
write_CFLAGS = $(SUID_CFLAGS) $(AM_CFLAGS)
write_LDFLAGS = $(SUID_LDFLAGS) $(AM_LDFLAGS)

if USE_TTY_GROUP
if MAKEINSTALL_DO_CHOWN
install-exec-hook::
	chgrp tty $(DESTDIR)$(usrbin_execdir)/write
	chmod g+s $(DESTDIR)$(usrbin_execdir)/write
endif
endif
endif
