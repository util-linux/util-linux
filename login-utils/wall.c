/*
 * Copyright (c) 1988, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 * Modified Sun Mar 12 10:34:34 1995, faith@cs.unc.edu, for Linux
 */

/*
 * This program is not related to David Wall, whose Stanford Ph.D. thesis
 * is entitled "Mechanisms for Broadcast and Selective Broadcast".
 *
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <err.h>
#include <errno.h>
#include <paths.h>
#include <ctype.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#include "nls.h"
#include "xalloc.h"
#include "strutils.h"
#include "ttymsg.h"
#include "pathnames.h"
#include "carefulputc.h"

void	makemsg __P((char *));

#define	IGNOREUSER	"sleeper"

#ifndef MAXHOSTNAMELEN
# ifdef HOST_NAME_MAX
#  define MAXHOSTNAMELEN HOST_NAME_MAX
# else
#  define MAXHOSTNAMELEN 64
# endif
#endif

int nobanner;
int mbufsize;
char *mbuf;

static void __attribute__((__noreturn__)) usage(void)
{
	errx(EXIT_FAILURE, _("usage: %s [-n] [file]\n"),
	     program_invocation_short_name);
}

int
main(int argc, char **argv) {
	extern int optind;
	int ch;
	struct iovec iov;
	struct utmp *utmpptr;
	char *p;
	char line[sizeof(utmpptr->ut_line) + 1];

	setlocale(LC_ALL, "");
        bindtextdomain(PACKAGE, LOCALEDIR);
        textdomain(PACKAGE);

	while ((ch = getopt(argc, argv, "n")) != -1) {
		switch (ch) {
		case 'n':
			if (geteuid() == 0)
				nobanner = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 1)
		usage();

	makemsg(*argv);

	setutent();

	iov.iov_base = mbuf;
	iov.iov_len = mbufsize;
	while((utmpptr = getutent())) {
		if (!utmpptr->ut_name[0] ||
		    !strncmp(utmpptr->ut_name, IGNOREUSER,
			     sizeof(utmpptr->ut_name)))
			continue;
#ifdef USER_PROCESS
		if (utmpptr->ut_type != USER_PROCESS)
			continue;
#endif

		/* Joey Hess reports that use-sessreg in /etc/X11/wdm/
		   produces ut_line entries like :0, and a write
		   to /dev/:0 fails. */
		if (utmpptr->ut_line[0] == ':')
			continue;

		xstrncpy(line, utmpptr->ut_line, sizeof(utmpptr->ut_line));
		if ((p = ttymsg(&iov, 1, line, 60*5)) != NULL)
			warnx("%s", p);
	}
	endutent();
	exit(EXIT_SUCCESS);
}

void
makemsg(fname)
	char *fname;
{
	register int ch, cnt;
	struct tm *lt;
	struct passwd *pw;
	struct stat sbuf;
	time_t now;
	FILE *fp;
	int fd;
	char *p, *whom, *where, hostname[MAXHOSTNAMELEN],
		lbuf[MAXHOSTNAMELEN + 320],
		tmpname[sizeof(_PATH_TMP) + 20];

	sprintf(tmpname, "%s/wall.XXXXXX", _PATH_TMP);
	if (!(fd = mkstemp(tmpname)) || !(fp = fdopen(fd, "r+")))
		errx(EXIT_FAILURE, _("can't open temporary file"));

	unlink(tmpname);

	if (!nobanner) {
		if (!(whom = getlogin()) || !*whom)
			whom = (pw = getpwuid(getuid())) ? pw->pw_name : "???";
		if (!whom || strlen(whom) > 100)
			whom = "someone";
		where = ttyname(2);
		if (!where || strlen(where) > 100)
			where = "somewhere";
		gethostname(hostname, sizeof(hostname));
		time(&now);
		lt = localtime(&now);

		/*
		 * all this stuff is to blank out a square for the message;
		 * we wrap message lines at column 79, not 80, because some
		 * terminals wrap after 79, some do not, and we can't tell.
		 * Which means that we may leave a non-blank character
		 * in column 80, but that can't be helped.
		 */
		/* snprintf is not always available, but the sprintf's here
		   will not overflow as long as %d takes at most 100 chars */
		fprintf(fp, "\r%79s\r\n", " ");
		sprintf(lbuf, _("Broadcast Message from %s@%s"),
			      whom, hostname);
		fprintf(fp, "%-79.79s\007\007\r\n", lbuf);
		sprintf(lbuf, "        (%s) at %d:%02d ...",
			      where, lt->tm_hour, lt->tm_min);
		fprintf(fp, "%-79.79s\r\n", lbuf);
	}
	fprintf(fp, "%79s\r\n", " ");

	if (fname) {
		/*
		 * When we are not root, but suid or sgid, refuse to read files
		 * (e.g. device files) that the user may not have access to.
		 * After all, our invoker can easily do "wall < file"
		 * instead of "wall file".
		 */
		int uid = getuid();
		if (uid && (uid != geteuid() || getgid() != getegid()))
			errx(EXIT_FAILURE, _("will not read %s - use stdin."),
			     fname);

		if (!freopen(fname, "r", stdin))
			errx(EXIT_FAILURE, _("can't read %s."), fname);
	}

	while (fgets(lbuf, sizeof(lbuf), stdin)) {
		for (cnt = 0, p = lbuf; (ch = *p) != '\0'; ++p, ++cnt) {
			if (cnt == 79 || ch == '\n') {
				for (; cnt < 79; ++cnt)
					putc(' ', fp);
				putc('\r', fp);
				putc('\n', fp);
				cnt = 0;
			}
			if (ch != '\n')
				carefulputc(ch, fp);
		}
	}
	fprintf(fp, "%79s\r\n", " ");
	rewind(fp);

	if (fstat(fd, &sbuf))
		err(EXIT_FAILURE, _("fstat failed"));

	mbufsize = sbuf.st_size;
	mbuf = xmalloc(mbufsize);

	if (fread(mbuf, sizeof(*mbuf), mbufsize, fp) != mbufsize)
		err(EXIT_FAILURE, _("fread failed"));

	close(fd);
}
