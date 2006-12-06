/*
 * Vaguely based on
 *	@(#)pathnames.h	5.3 (Berkeley) 5/9/89
 * This code is in the public domain.
 */
#include <paths.h>

#ifndef __STDC__
# error "we need an ANSI compiler"
#endif

/* The paths for some of these are wrong in /usr/include/paths.h,
   but we re-define them here. */

/* Used in login.c, agetty.c, simpleinit.c, shutdown.c, write.c */
#undef _PATH_UTMP
/* Used in login.c, agetty.c, simpleinit.c, shutdown.c, last.c */
#undef _PATH_WTMP
/* These four are used in login.c only */
#undef _PATH_DEFPATH
#undef _PATH_DEFPATH_ROOT
#undef _PATH_LASTLOG

/*
 * HISTORY
 *
What is the history of these six, and related defines?
------------------------------------------------------------------------
_PATH_UTMP and UTMP_FILE and UTMP_FILENAME
	/etc/utmp > /var/adm/utmp > /var/run/utmp.
Traditionally we have /etc/utmp.
In <paths.h> we have /etc/utmp, but since 4.6.0 /var/adm/utmp
and since 5.0.9 (and in glibc2) /var/run/utmp.
In login/pathnames.h we have /etc/utmp, but since 4.6.6 /var/adm/utmp.
In <utmp.h> UTMP_FILE is defined as /etc/utmp, but in 4.6.* as _PATH_UTMP.

_PATH_WTMP and WTMP_FILE and WTMP_FILENAME
	/etc/wtmp > /usr/adm/wtmp > /var/adm/wtmp > /var/log/wtmp.
Traditionally we have /etc/wtmp.
In <paths.h> we have /usr/adm/wtmp, but since 4.5.13 /var/adm/wtmp,
and since 5.0.9 (and in glibc2) /var/log/wtmp.
In login/pathnames.h. we have /etc/wtmp, but since 4.6.6 /var/adm/wtmp.
In <utmp.h> WTMP_FILE is defined as /usr/adm/wtmp, but in 4.5.* as
/var/adm/wtmp, and in 4.6.* as _PATH_WTMP.

_PATH_DEFPATH
Long ago this was ".:/bin:/usr/bin".
In <paths.h> libc 4.4.1-4.4.4 have "/usr/bin:/bin"
and libc 4.5.21-5.4.23 have "/usr/local/bin:/usr/bin:/bin:."
and libc 5.4.38-5.4.46 have "/usr/local/bin:/usr/bin:/bin".
In login/pathnames.h libc4 and libc5 have "/usr/local/bin:/bin:/usr/bin:."

_PATH_DEFPATH_ROOT
Long ago this was identical to _PATH_DEFPATH.
In <paths.h> no definition is present before libc 4.5.13.
Libc 4.5.13 has "/bin:/usr/bin:/etc"
Libc 4.5.14-5.4.46 have "/sbin:/bin:/usr/sbin:/usr/bin"
In login/pathnames.h libc4 and libc5 have "/bin:/usr/bin:/etc"

_PATH_LASTLOG
	/etc/lastlog > /usr/adm/lastlog > /var/adm/lastlog > /var/log/lastlog.
Traditionally we have /etc/lastlog.
In <bsd/utmp.h> libc 4.4.1-4.5.12 have /usr/adm/lastlog, 4.5.13 and
later have /var/adm/lastlog.
In paths.h all libc5 and glibc2 versions have /var/log/lastlog.
In login/pathnames.h all libc4 and libc5 versions have /usr/adm/lastlog.

_PATH_MAILDIR
	/usr/spool/mail > /var/spool/mail > /var/mail.
Traditionally we have /usr/spool/mail.
In <paths.h> we have /usr/spool/mail, but since libc 4.5.13 /var/spool/mail.
In login/pathnames.h all libc4 versions have /var/spool/mail.
Libc5 and glibc 2.0-2.1 have /var/spool/mail, but glibc 2.1.1 has /var/mail.
------------------------------------------------------------------------*/


#ifndef SBINDIR
#define SBINDIR			"/sbin"
#endif

#ifndef USRSBINDIR
#define USRSBINDIR              "/usr/sbin"
#endif

#ifndef LOGDIR
#define LOGDIR                  "/var/log"
#endif

#ifndef VARPATH
#define VARPATH			"/var"
#endif

#ifndef UT_NAMESIZE
#define UT_NAMESIZE     	8
#endif

#define _PATH_BSHELL    	"/bin/sh"
#define _PATH_CSHELL    	"/bin/csh"
#define _PATH_TTY       	"/dev/tty"
#define TTYTYPES        	"/etc/ttytype"
#define SECURETTY       	"/etc/securetty"
#define _PATH_UTMP      	"/var/run/utmp"
#define _PATH_WTMP      	LOGDIR "/wtmp"
#define _PATH_WTMPLOCK		"/etc/wtmplock"

/* no more . in DEFPATH */
#define	_PATH_DEFPATH	        "/usr/local/bin:/bin:/usr/bin"
#define	_PATH_DEFPATH_ROOT	"/usr/local/sbin:/usr/local/bin:" SBINDIR ":/bin:" USRSBINDIR ":/usr/bin"
#define	_PATH_HUSHLOGIN		".hushlogin"
#define	_PATH_LASTLOG		LOGDIR "/lastlog"

#ifndef _PATH_MAILDIR
#define	_PATH_MAILDIR		VARPATH "/spool/mail"
#endif
#define	_PATH_MOTDFILE		"/etc/motd"
#define	_PATH_NOLOGIN		"/etc/nologin"

#define _PATH_LOGIN		"/bin/login"
#define _PATH_INITTAB		"/etc/inittab"
#define _PATH_RC		"/etc/rc"
#define _PATH_REBOOT		SBINDIR "/reboot"
#define _PATH_SINGLE		"/etc/singleboot"
#define _PATH_SHUTDOWN_CONF	"/etc/shutdown.conf"

#define _PATH_SECURE		"/etc/securesingle"
#define _PATH_USERTTY           "/etc/usertty"

/* used in login-utils/shutdown.c */
#define _PATH_MTAB		"/etc/mtab"
#define _PATH_UMOUNT		"/bin/umount"
#define UMOUNT_ARGS		"umount", "-a", "-t", "nodevfs"
#define SWAPOFF_ARGS            "swapoff", "-a"

/* used in login-utils/setpwnam.h and login-utils/islocal.c */
#define _PATH_PASSWD            "/etc/passwd"

/* used in login-utils/setpwnam.h */
#define _PATH_PTMP              "/etc/ptmp"
#define _PATH_PTMPTMP           "/etc/ptmptmp"
#define _PATH_GROUP             "/etc/group"
#define _PATH_GTMP              "/etc/gtmp"
#define _PATH_GTMPTMP           "/etc/gtmptmp"
#define _PATH_SHADOW_PASSWD     "/etc/shadow"
#define _PATH_SHADOW_PTMP       "/etc/sptmp"
#define _PATH_SHADOW_PTMPTMP    "/etc/sptmptmp"
#define _PATH_SHADOW_GROUP      "/etc/gshadow"
#define _PATH_SHADOW_GTMP       "/etc/sgtmp"
#define _PATH_SHADOW_GTMPTMP    "/etc/sgtmptmp"

/* used in misc-utils/look.c */
#define _PATH_WORDS             "/usr/share/dict/words"
#define _PATH_WORDS_ALT         "/usr/share/dict/web2"
