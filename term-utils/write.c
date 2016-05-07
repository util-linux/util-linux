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
 *      - Added fix from David.Chapell@mail.trincoll.edu enabling daemons
 *	  to use write.
 *      - ANSIed it since I was working on it anyway.
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <utmp.h>
#include <errno.h>
#include <time.h>
#include <pwd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <paths.h>
#include <getopt.h>

#include "c.h"
#include "carefulputc.h"
#include "closestream.h"
#include "nls.h"
#include "strutils.h"
#include "xalloc.h"

static sig_atomic_t signal_received = 0;

struct write_control {
	uid_t src_uid;
	const char *src_login;
	char *src_tty;
	const char *dst_login;
	char dst_tty[PATH_MAX];
};

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] <user> [<ttyname>]\n"),
	      program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Send a message to another user.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, USAGE_MAN_TAIL("write(1)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

/*
 * term_chk - check that a terminal exists, and get the message bit
 *     and the access time
 */
static int term_chk(char *tty, int *msgsokP, time_t * atimeP, int showerror)
{
	struct stat s;
	char path[PATH_MAX];

	if (strlen(tty) + 6 > sizeof(path))
		return 1;
	sprintf(path, "/dev/%s", tty);
	if (stat(path, &s) < 0) {
		if (showerror)
			warn("%s", path);
		return 1;
	}
	if (getuid() == 0)	/* root can always write */
		*msgsokP = 1;
	else
		*msgsokP = (s.st_mode & S_IWGRP) && (getegid() == s.st_gid);
	if (atimeP)
		*atimeP = s.st_atime;
	return 0;
}

/*
 * utmp_chk - checks that the given user is actually logged in on
 *     the given tty
 */
static int utmp_chk(const struct write_control *ctl)
{
	struct utmp u;
	struct utmp *uptr;
	int res = 1;

	utmpname(_PATH_UTMP);
	setutent();

	while ((uptr = getutent())) {
		memcpy(&u, uptr, sizeof(u));
		if (strncmp(ctl->dst_login, u.ut_user, sizeof(u.ut_user)) == 0 &&
		    strncmp(ctl->dst_tty, u.ut_line, sizeof(u.ut_line)) == 0) {
			res = 0;
			break;
		}
	}

	endutent();
	return res;
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
static void search_utmp(struct write_control *ctl)
{
	struct utmp u;
	struct utmp *uptr;
	time_t bestatime, atime;
	int nloggedttys, nttys, msgsok = 0, user_is_me;
	char atty[sizeof(u.ut_line) + 1];

	utmpname(_PATH_UTMP);
	setutent();

	nloggedttys = nttys = 0;
	bestatime = 0;
	user_is_me = 0;
	while ((uptr = getutent())) {
		memcpy(&u, uptr, sizeof(u));
		if (strncmp(ctl->dst_login, u.ut_user, sizeof(u.ut_user)) == 0) {
			++nloggedttys;
			xstrncpy(atty, u.ut_line, sizeof(atty));
			if (term_chk(atty, &msgsok, &atime, 0))
				/* bad term? skip */
				continue;
			if (ctl->src_uid && !msgsok)
				/* skip ttys with msgs off */
				continue;
			if (strcmp(atty, ctl->src_tty) == 0) {
				user_is_me = 1;
				/* don't write to yourself */
				continue;
			}
			if (u.ut_type != USER_PROCESS)
				/* it's not a valid entry */
				continue;
			++nttys;
			if (atime > bestatime) {
				bestatime = atime;
				xstrncpy(ctl->dst_tty, atty, sizeof(ctl->dst_tty));
			}
		}
	}

	endutent();
	if (nloggedttys == 0)
		errx(EXIT_FAILURE, _("%s is not logged in"), ctl->dst_login);
	if (nttys == 0) {
		if (user_is_me) {
			/* ok, so write to yourself! */
			xstrncpy(ctl->dst_tty, ctl->src_tty, sizeof(ctl->dst_tty));
			return;
		}
		errx(EXIT_FAILURE, _("%s has messages disabled"), ctl->dst_login);
	} else if (nttys > 1) {
		warnx(_("%s is logged in more than once; writing to %s"),
		      ctl->dst_login, ctl->dst_tty);
	}

}

/*
 * signal_handler - cause write loop to exit
 */
static void signal_handler(int signo)
{
	signal_received = signo;
}

/*
 * wr_fputs - like fputs(), but makes control characters visible and
 *     turns \n into \r\n.
 */
static void wr_fputs(char *s)
{
	char c;

#define	PUTC(c)	if (fputc_careful(c, stdout, '^') == EOF) \
    err(EXIT_FAILURE, _("carefulputc failed"));
	while (*s) {
		c = *s++;
		if (c == '\n')
			PUTC('\r');
		PUTC(c);
	}
	return;
#undef PUTC
}

/*
 * do_write - actually make the connection
 */
static void do_write(const struct write_control *ctl)
{
	char *login, *pwuid, *nows;
	struct passwd *pwd;
	time_t now;
	char path[PATH_MAX], *host, line[512];
	struct sigaction sigact;

	/* Determine our login name(s) before the we reopen() stdout */
	if ((pwd = getpwuid(ctl->src_uid)) != NULL)
		pwuid = pwd->pw_name;
	else
		pwuid = "???";
	if ((login = getlogin()) == NULL)
		login = pwuid;

	if (strlen(ctl->dst_tty) + 6 > sizeof(path))
		errx(EXIT_FAILURE, _("tty path %s too long"), ctl->dst_tty);
	snprintf(path, sizeof(path), "/dev/%s", ctl->dst_tty);
	if ((freopen(path, "w", stdout)) == NULL)
		err(EXIT_FAILURE, "%s", path);

	sigact.sa_handler = signal_handler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGHUP, &sigact, NULL);

	/* print greeting */
	host = xgethostname();
	if (!host)
		host = xstrdup("???");

	now = time((time_t *) NULL);
	nows = ctime(&now);
	nows[16] = '\0';
	printf("\r\n\007\007\007");
	if (strcmp(login, pwuid))
		printf(_("Message from %s@%s (as %s) on %s at %s ..."),
		       login, host, pwuid, ctl->src_tty, nows + 11);
	else
		printf(_("Message from %s@%s on %s at %s ..."),
		       login, host, ctl->src_tty, nows + 11);
	free(host);
	printf("\r\n");

	while (fgets(line, sizeof(line), stdin) != NULL) {
		if (signal_received)
			break;
		wr_fputs(line);
	}
	printf("EOF\r\n");
}

int main(int argc, char **argv)
{
	int msgsok = 0, src_fd, c;
	struct write_control ctl = { 0 };

	static const struct option longopts[] = {
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "Vh", longopts, NULL)) != -1)
		switch (c) {
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout);
		default:
			usage(stderr);
		}

	/* check that sender has write enabled */
	if (isatty(STDIN_FILENO))
		src_fd = STDIN_FILENO;
	else if (isatty(STDOUT_FILENO))
		src_fd = STDOUT_FILENO;
	else if (isatty(STDERR_FILENO))
		src_fd = STDERR_FILENO;
	else
		src_fd = -1;

	if (src_fd != -1) {
		if (!(ctl.src_tty = ttyname(src_fd)))
			errx(EXIT_FAILURE,
			     _("can't find your tty's name"));

		/*
		 * We may have /dev/ttyN but also /dev/pts/xx. Below,
		 * term_chk() will put "/dev/" in front, so remove that
		 * part.
		 */
		if (!strncmp(ctl.src_tty, "/dev/", 5))
			ctl.src_tty += 5;
		if (term_chk(ctl.src_tty, &msgsok, NULL, 1))
			exit(EXIT_FAILURE);
		if (!msgsok)
			errx(EXIT_FAILURE,
			     _("you have write permission turned off"));
		msgsok = 0;
	} else
		ctl.src_tty = "<no tty>";

	ctl.src_uid = getuid();

	/* check args */
	switch (argc) {
	case 2:
		ctl.dst_login = argv[1];
		search_utmp(&ctl);
		do_write(&ctl);
		break;
	case 3:
		ctl.dst_login = argv[1];
		if (!strncmp(argv[2], "/dev/", 5))
			xstrncpy(ctl.dst_tty, argv[2] + 5, sizeof(ctl.dst_tty));
		else
			xstrncpy(ctl.dst_tty, argv[2], sizeof(ctl.dst_tty));
		if (utmp_chk(&ctl))
			errx(EXIT_FAILURE,
			     _("%s is not logged in on %s"),
			     ctl.dst_login, ctl.dst_tty);
		if (term_chk(ctl.dst_tty, &msgsok, NULL, 1))
			exit(EXIT_FAILURE);
		if (ctl.src_uid && !msgsok)
			errx(EXIT_FAILURE,
			     _("%s has messages disabled on %s"),
			     ctl.dst_login, ctl.dst_tty);
		do_write(&ctl);
		break;
	default:
		usage(stderr);
	}
	return EXIT_SUCCESS;
}
