/*
 * Vaguely based on
 *	@(#)pathnames.h	5.3 (Berkeley) 5/9/89
 * This code is in the public domain.
 */
#ifndef PATHNAMES_H
#define PATHNAMES_H

#include <paths.h>

#ifndef __STDC__
# error "we need an ANSI compiler"
#endif

/* DEFPATHs from <paths.h> don't include /usr/local */
#undef _PATH_DEFPATH
#define	_PATH_DEFPATH	        "/usr/local/bin:/bin:/usr/bin"

#undef _PATH_DEFPATH_ROOT
#define	_PATH_DEFPATH_ROOT	"/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin"

#define _PATH_TTY		"/dev/tty"
#define _PATH_SECURETTY		"/etc/securetty"
#define _PATH_WTMPLOCK		"/etc/wtmplock"

#define	_PATH_HUSHLOGIN		".hushlogin"

#ifndef _PATH_MAILDIR
#define	_PATH_MAILDIR		"/var/spool/mail"
#endif
#define	_PATH_MOTDFILE		"/etc/motd"
#define	_PATH_NOLOGIN		"/etc/nologin"

#define _PATH_LOGIN		"/bin/login"
#define _PATH_INITTAB		"/etc/inittab"
#define _PATH_RC		"/etc/rc"
#define _PATH_REBOOT		"/sbin/reboot"
#define _PATH_SINGLE		"/etc/singleboot"
#define _PATH_SHUTDOWN_CONF	"/etc/shutdown.conf"

#define _PATH_SECURE		"/etc/securesingle"
#define _PATH_USERTTY           "/etc/usertty"

/* used in login-utils/shutdown.c */
#define _PATH_MTAB		"/etc/mtab"
#define _PATH_UMOUNT		"/bin/umount"

/* used in login-utils/setpwnam.h and login-utils/islocal.c */
#define _PATH_PASSWD            "/etc/passwd"

/* used in login-utils/newgrp */
#define _PATH_GSHADOW		"/etc/gshadow"

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

#endif /* PATHNAMES_H */

