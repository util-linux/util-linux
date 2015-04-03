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
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>

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
#include <getopt.h>

#include "nls.h"
#include "xalloc.h"
#include "strutils.h"
#include "ttymsg.h"
#include "pathnames.h"
#include "carefulputc.h"
#include "c.h"
#include "fileutils.h"
#include "closestream.h"

#define	TERM_WIDTH	79
#define	WRITE_TIME_OUT	300		/* in seconds */

/* Function prototypes */
static char *makemsg(char *fname, char **mvec, int mvecsz,
		    size_t *mbufsize, int print_banner);

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] [<file> | <message>]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Write a message to all users.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -n, --nobanner          do not print banner, works only for root\n"), out);
	fputs(_(" -t, --timeout <timeout> write timeout in seconds\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("wall(1)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int ch;
	struct iovec iov;
	struct utmp *utmpptr;
	char *p;
	char line[sizeof(utmpptr->ut_line) + 1];
	int print_banner = TRUE;
	char *mbuf, *fname = NULL;
	size_t mbufsize;
	unsigned timeout = WRITE_TIME_OUT;
	char **mvec = NULL;
	int mvecsz = 0;

	static const struct option longopts[] = {
		{ "nobanner",	no_argument,		0, 'n' },
		{ "timeout",	required_argument,	0, 't' },
		{ "version",	no_argument,		0, 'V' },
		{ "help",	no_argument,		0, 'h' },
		{ NULL,	0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((ch = getopt_long(argc, argv, "nt:Vh", longopts, NULL)) != -1) {
		switch (ch) {
		case 'n':
			if (geteuid() == 0)
				print_banner = FALSE;
			else
				warnx(_("--nobanner is available only for root"));
			break;
		case 't':
			timeout = strtou32_or_err(optarg, _("invalid timeout argument"));
			if (timeout < 1)
				errx(EXIT_FAILURE, _("invalid timeout argument: %s"), optarg);
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
		case 'h':
			usage(stdout);
		default:
			usage(stderr);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 1 && access(argv[0], F_OK) == 0)
		fname = argv[0];
	else if (argc >= 1) {
		mvec = argv;
		mvecsz = argc;
	}

	mbuf = makemsg(fname, mvec, mvecsz, &mbufsize, print_banner);

	iov.iov_base = mbuf;
	iov.iov_len = mbufsize;
	while((utmpptr = getutent())) {
		if (!utmpptr->ut_user[0])
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
		if ((p = ttymsg(&iov, 1, line, timeout)) != NULL)
			warnx("%s", p);
	}
	endutent();
	free(mbuf);
	exit(EXIT_SUCCESS);
}

struct buffer {
	size_t	sz;
	size_t	used;
	char	*data;
};

static void buf_enlarge(struct buffer *bs, size_t len)
{
	if (bs->sz == 0 || len > bs->sz - bs->used) {
		bs->sz += len < 128 ? 128 : len;
		bs->data = xrealloc(bs->data, bs->sz);
	}
}

static void buf_puts(struct buffer *bs, const char *s)
{
	size_t len = strlen(s);

	buf_enlarge(bs, len + 1);
	memcpy(bs->data + bs->used, s, len + 1);
	bs->used += len;
}

static void buf_printf(struct buffer *bs, const char *fmt, ...)
{
	int rc;
	va_list ap;
	size_t limit;

	buf_enlarge(bs, 0);	/* default size */
	limit = bs->sz - bs->used;

	va_start(ap, fmt);
	rc = vsnprintf(bs->data + bs->used, limit, fmt, ap);
	va_end(ap);

	if (rc > 0 && (size_t) rc + 1 > limit) {	/* not enoght, enlarge */
		buf_enlarge(bs, rc + 1);
		limit = bs->sz - bs->used;
		va_start(ap, fmt);
		rc = vsnprintf(bs->data  + bs->used, limit, fmt, ap);;
		va_end(ap);
	}

	if (rc > 0)
		bs->used += rc;
}

static void buf_putc_careful(struct buffer *bs, int c)
{
	if (isprint(c) || c == '\a' || c == '\t' || c == '\r' || c == '\n') {
		buf_enlarge(bs, 1);
		bs->data[bs->used++] = c;
	} else if (!isascii(c))
		buf_printf(bs, "\\%3o", (unsigned char)c);
	else {
		char tmp[] = { '^', c ^ 0x40, '\0' };
		buf_puts(bs, tmp);
	}
}

static char *makemsg(char *fname, char **mvec, int mvecsz,
		     size_t *mbufsize, int print_banner)
{
	struct buffer _bs = {.used = 0}, *bs = &_bs;
	register int ch, cnt;
	char *p, *lbuf;
	long line_max;

	line_max = sysconf(_SC_LINE_MAX);
	if (line_max <= 0)
		line_max = 512;

	lbuf = xmalloc(line_max);

	if (print_banner == TRUE) {
		char *hostname = xgethostname();
		char *whom, *where, *date;
		struct passwd *pw;
		time_t now;

		if (!(whom = getlogin()) || !*whom)
			whom = (pw = getpwuid(getuid())) ? pw->pw_name : "???";
		if (!whom) {
			whom = "someone";
			warn(_("cannot get passwd uid"));
		}
		where = ttyname(STDOUT_FILENO);
		if (!where) {
			where = "somewhere";
			warn(_("cannot get tty name"));
		} else if (strncmp(where, "/dev/", 5) == 0)
			where += 5;

		time(&now);
		date = xstrdup(ctime(&now));
		date[strlen(date) - 1] = '\0';

		/*
		 * all this stuff is to blank out a square for the message;
		 * we wrap message lines at column 79, not 80, because some
		 * terminals wrap after 79, some do not, and we can't tell.
		 * Which means that we may leave a non-blank character
		 * in column 80, but that can't be helped.
		 */
		/* snprintf is not always available, but the sprintf's here
		   will not overflow as long as %d takes at most 100 chars */
		buf_printf(bs, "\r%*s\r\n", TERM_WIDTH, " ");

		snprintf(lbuf, line_max,
				_("Broadcast message from %s@%s (%s) (%s):"),
				whom, hostname, where, date);
		buf_printf(bs, "%-*.*s\007\007\r\n", TERM_WIDTH, TERM_WIDTH, lbuf);
		free(hostname);
		free(date);
	}
	buf_printf(bs, "%*s\r\n", TERM_WIDTH, " ");

	 if (mvec) {
		/*
		 * Read message from argv[]
		 */
		int i;

		for (i = 0; i < mvecsz; i++) {
			buf_puts(bs, mvec[i]);
			if (i < mvecsz - 1)
				buf_puts(bs, " ");
		}
		buf_puts(bs, "\r\n");
	} else {
		/*
		 * read message from <file>
		 */
		if (fname) {
			/*
			 * When we are not root, but suid or sgid, refuse to read files
			 * (e.g. device files) that the user may not have access to.
			 * After all, our invoker can easily do "wall < file"
			 * instead of "wall file".
			 */
			uid_t uid = getuid();
			if (uid && (uid != geteuid() || getgid() != getegid()))
				errx(EXIT_FAILURE, _("will not read %s - use stdin."),
				     fname);

			if (!freopen(fname, "r", stdin))
				err(EXIT_FAILURE, _("cannot open %s"), fname);

		}

		/*
		 * Read message from stdin.
		 */
		while (fgets(lbuf, line_max, stdin)) {
			for (cnt = 0, p = lbuf; (ch = *p) != '\0'; ++p, ++cnt) {
				if (cnt == TERM_WIDTH || ch == '\n') {
					for (; cnt < TERM_WIDTH; ++cnt)
						buf_puts(bs, " ");
					buf_puts(bs, "\r\n");
					cnt = 0;
				}
				if (ch == '\t')
					cnt += (7 - (cnt % 8));
				if (ch != '\n')
					buf_putc_careful(bs, ch);
			}
		}
	}
	buf_printf(bs, "%*s\r\n", TERM_WIDTH, " ");

	free(lbuf);

	bs->data[bs->used] = '\0';	/* be paranoid */
	*mbufsize = bs->used;
	return bs->data;
}
