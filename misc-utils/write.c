/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jef Poskanzer and Craig Leres of the Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Modified for Linux, Mon Mar  8 18:16:24 1993, faith@cs.unc.edu
 * Wed Jun 22 21:41:56 1994, faith@cs.unc.edu:
 *      Added fix from Mike Grupenhoff (kashmir@umiacs.umd.edu)
 * Mon Jul  1 17:01:39 MET DST 1996, janl@math.uio.no:
 *      - Added fix from David.Chapell@mail.trincoll.edu enabeling daemons
 *	  to use write.
 *      - ANSIed it since I was working on it anyway.
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <utmp.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <pwd.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/time.h>
#include <paths.h>
#include "pathnames.h"
#include "carefulputc.h"
#include "nls.h"
 
void search_utmp(char *, char *, char *, uid_t);
void do_write(char *, char *, uid_t);
void wr_fputs(char *);
static void done(int);
int term_chk(char *, int *, time_t *, int);
int utmp_chk(char *, char *);

int
main(int argc, char **argv) {
	time_t atime;
	uid_t myuid;
	int msgsok, myttyfd;
	char tty[MAXPATHLEN], *mytty;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* check that sender has write enabled */
	if (isatty(fileno(stdin)))
		myttyfd = fileno(stdin);
	else if (isatty(fileno(stdout)))
		myttyfd = fileno(stdout);
	else if (isatty(fileno(stderr)))
		myttyfd = fileno(stderr);
	else {
	  	myttyfd = -1;
	}
	if (myttyfd != -1) {
	  if (!(mytty = ttyname(myttyfd))) {
		(void)fprintf(stderr, _("write: can't find your tty's name\n"));
		exit(1);
	  }
	  /* We may have /dev/ttyN but also /dev/pts/xx.
	     Below, term_chk() will put "/dev/" in front, so remove that part. */
	  if (!strncmp(mytty, "/dev/", 5))
		mytty += 5;
	  if (term_chk(mytty, &msgsok, &atime, 1))
		exit(1);
	  if (!msgsok) {
		(void)fprintf(stderr,
		    _("write: you have write permission turned off.\n"));
		exit(1);
	  }
	
	} else {
	    mytty = "<no tty>";
	}

	myuid = getuid();

	/* check args */
	switch (argc) {
	case 2:
		search_utmp(argv[1], tty, mytty, myuid);
		do_write(tty, mytty, myuid);
		break;
	case 3:
		if (!strncmp(argv[2], "/dev/", 5))
			argv[2] += 5;
		if (utmp_chk(argv[1], argv[2])) {
			(void)fprintf(stderr,
			    _("write: %s is not logged in on %s.\n"),
			    argv[1], argv[2]);
			exit(1);
		}
		if (term_chk(argv[2], &msgsok, &atime, 1))
			exit(1);
		if (myuid && !msgsok) {
			(void)fprintf(stderr,
			    _("write: %s has messages disabled on %s\n"),
			    argv[1], argv[2]);
			exit(1);
		}
		do_write(argv[2], mytty, myuid);
		break;
	default:
		(void)fprintf(stderr, _("usage: write user [tty]\n"));
		exit(1);
	}
	done(0);
	/* NOTREACHED */
	return 0;
}


/*
 * utmp_chk - checks that the given user is actually logged in on
 *     the given tty
 */
int utmp_chk(char *user, char *tty)

{
	struct utmp u;
	struct utmp *uptr;
	int res = 1;

	utmpname(_PATH_UTMP);
	setutent();

	while ((uptr = getutent())) {
		memcpy(&u, uptr, sizeof(u));
		if (strncmp(user, u.ut_name, sizeof(u.ut_name)) == 0 &&
		    strncmp(tty, u.ut_line, sizeof(u.ut_line)) == 0) {
			res = 0;
			break;
		}
	}

	endutent();
	return(res);
}

/*
 * search_utmp - search utmp for the "best" terminal to write to
 *
 * Ignores terminals with messages disabled, and of the rest, returns
 * the one with the most recent access time.  Returns as value the number
 * of the user's terminals with messages enabled, or -1 if the user is
 * not logged in at all.
 *
 * Special case for writing to yourself - ignore the terminal you're
 * writing from, unless that's the only terminal with messages enabled.
 */
void search_utmp(char *user, char *tty, char *mytty, uid_t myuid)

{
	struct utmp u;
	struct utmp *uptr;
	time_t bestatime, atime;
	int nloggedttys, nttys, msgsok, user_is_me;
	char atty[sizeof(u.ut_line) + 1];

	utmpname(_PATH_UTMP);
	setutent();

	nloggedttys = nttys = 0;
	bestatime = 0;
	user_is_me = 0;
	while ((uptr = getutent())) {
		memcpy(&u, uptr, sizeof(u));
		if (strncmp(user, u.ut_name, sizeof(u.ut_name)) == 0) {
			++nloggedttys;
			(void)strncpy(atty, u.ut_line, sizeof(u.ut_line));
			atty[sizeof(u.ut_line)] = '\0';
			if (term_chk(atty, &msgsok, &atime, 0))
				continue;	/* bad term? skip */
			if (myuid && !msgsok)
				continue;	/* skip ttys with msgs off */
			if (strcmp(atty, mytty) == 0) {
				user_is_me = 1;
				continue;	/* don't write to yourself */
			}
                        if (u.ut_type != USER_PROCESS)
			        continue;       /* it's not a valid entry */
			++nttys;
			if (atime > bestatime) {
				bestatime = atime;
				(void)strcpy(tty, atty);
			}
		}
	}

	endutent();
	if (nloggedttys == 0) {
		(void)fprintf(stderr, _("write: %s is not logged in\n"), user);
		exit(1);
	}
	if (nttys == 0) {
		if (user_is_me) {		/* ok, so write to yourself! */
			(void)strcpy(tty, mytty);
			return;
		}
		(void)fprintf(stderr,
		    _("write: %s has messages disabled\n"), user);
		exit(1);
	} else if (nttys > 1) {
		(void)fprintf(stderr,
		    _("write: %s is logged in more than once; writing to %s\n"),
		    user, tty);
	}
}

/*
 * term_chk - check that a terminal exists, and get the message bit
 *     and the access time
 */
int term_chk(char *tty, int *msgsokP, time_t *atimeP, int showerror)

{
	struct stat s;
	char path[MAXPATHLEN];

	if (strlen(tty) + 6 > sizeof(path))
		return(1);
	(void)sprintf(path, "/dev/%s", tty);
	if (stat(path, &s) < 0) {
		if (showerror)
			(void)fprintf(stderr,
			    "write: %s: %s\n", path, strerror(errno));
		return(1);
	}
	*msgsokP = (s.st_mode & (S_IWRITE >> 3)) != 0;	/* group write bit */
	*atimeP = s.st_atime;
	return(0);
}

/*
 * do_write - actually make the connection
 */
void do_write(char *tty, char *mytty, uid_t myuid) {
	char *login, *pwuid, *nows;
	struct passwd *pwd;
	time_t now;
	char path[MAXPATHLEN], host[MAXHOSTNAMELEN], line[512];

	/* Determine our login name(s) before the we reopen() stdout */
	if ((pwd = getpwuid(myuid)) != NULL)
		pwuid = pwd->pw_name;
	else
		pwuid = "???";
	if ((login = getlogin()) == NULL)
		login = pwuid;

	if (strlen(tty) + 6 > sizeof(path))
		exit(1);
	(void)sprintf(path, "/dev/%s", tty);
	if ((freopen(path, "w", stdout)) == NULL) {
		(void)fprintf(stderr, "write: %s: %s\n",
			      path, strerror(errno));
		exit(1);
	}

	(void)signal(SIGINT, done);
	(void)signal(SIGHUP, done);

	/* print greeting */
	if (gethostname(host, sizeof(host)) < 0)
		(void)strcpy(host, "???");
	now = time((time_t *)NULL);
	nows = ctime(&now);
	nows[16] = '\0';
	printf("\r\n\007\007\007");
	if (strcmp(login, pwuid))
		(void)printf(_("Message from %s@%s (as %s) on %s at %s ..."),
			     login, host, pwuid, mytty, nows + 11);
	else
		(void)printf(_("Message from %s@%s on %s at %s ..."),
			     login, host, mytty, nows + 11);
	printf("\r\n");

	while (fgets(line, sizeof(line), stdin) != NULL)
		wr_fputs(line);
}

/*
 * done - cleanup and exit
 */
static void
done(int dummy) {
	(void)printf("EOF\r\n");
	exit(0);
}

/*
 * wr_fputs - like fputs(), but makes control characters visible and
 *     turns \n into \r\n.
 */
void
wr_fputs(char *s) {
	char c;

#define	PUTC(c)	if (carefulputc(c,stdout) == EOF) goto err;

	while(*s) {
		c = *s++;
		if (c == '\n')
			PUTC('\r');
		PUTC(c);
	}
	return;

err:
	fprintf(stderr, "write: %s\n", strerror(errno));
	exit(1);
#undef PUTC
}
