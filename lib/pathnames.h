/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	@(#)pathnames.h	5.3 (Berkeley) 5/9/89
 *
 * Changed: Sun Nov 21 12:30:54 1993 by faith@cs.unc.edu
 * Changed: Wed Jun 22 20:47:27 1994 by faith@cs.unc.edu, based on changes
 *                                   from poe@daimi.aau.dk
 * Changed: Wed Jun 22 22:50:13 1994 by faith@cs.unc.edu
 * Changed: Sat Feb  4 16:02:10 1995 by faith@cs.unc.edu
 * Changed: Tue Jul  2 09:37:36 1996 by janl@math.uio.no, axp patches
 * Changed: Thu Nov  9 21:58:36 1995 by joey@infodrom.north.de
 */

#ifndef __STDC__
# error "we need an ANSI compiler"
#endif

/* The paths for some of these are wrong in /usr/include/paths.h, but we
   re-define them here. */

#undef _PATH_UTMP
#undef _PATH_WTMP
#undef _PATH_DEFPATH
#undef _PATH_DEFPATH_ROOT
#undef _PATH_LASTLOG
#undef _PATH_MAILDIR

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
#define	_PATH_DEFPATH_ROOT	SBINDIR ":/bin:" USRSBINDIR ":/usr/bin"
#define	_PATH_HUSHLOGIN		".hushlogin"
#define	_PATH_LASTLOG		LOGDIR "/lastlog"
#define	_PATH_MAILDIR		VARPATH "/spool/mail"
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

#define _PATH_MTAB		"/etc/mtab"
#define _PATH_UMOUNT		"/bin/umount"
#define UMOUNT_ARGS		"umount", "-a"
#define SWAPOFF_ARGS            "swapoff", "-a"

#define _PATH_PASSWD            "/etc/passwd"
#define _PATH_PTMP              "/etc/ptmp"
#define _PATH_PTMPTMP           "/etc/ptmptmp"

#define _PATH_GROUP             "/etc/group"
#define _PATH_GTMP              "/etc/gtmp"
#define _PATH_GTMPTMP           "/etc/gtmptmp"

#define _PATH_WORDS             "/usr/dict/words"
#define _PATH_WORDS_ALT         "/usr/dict/web2"
