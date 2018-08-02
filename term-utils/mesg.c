/*
 * Copyright (c) 1987, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 * Modified Fri Mar 10 20:27:19 1995, faith@cs.unc.edu, for Linux
 * Modified Mon Jul  1 18:14:10 1996, janl@ifi.uio.no, writing to stdout
 *	as suggested by Michael Meskes <meskes@Informatik.RWTH-Aachen.DE>
 *
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 * 2010-12-01 Marek Polacek <mmpolacek@gmail.com>
 * - cleanups
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>

#include "closestream.h"
#include "nls.h"
#include "c.h"
#include "rpmatch.h"

/* exit codes */

#define IS_ALLOWED        0  /* Receiving messages is allowed.  */
#define IS_NOT_ALLOWED    1  /* Receiving messages is not allowed.  */
#define MESG_EXIT_FAILURE 2  /* An error occurred.  */

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	/* TRANSLATORS: this program uses for y and n rpmatch(3),
	 * which means they can be translated.  */
	fprintf(out,
	      _(" %s [options] [y | n]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Control write access of other users to your terminal.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -v, --verbose  explain what is being done\n"), out);
	printf(USAGE_HELP_OPTIONS(16));
	printf(USAGE_MAN_TAIL("mesg(1)"));

	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	struct stat sb;
	char *tty;
	int ch, fd, verbose = FALSE, ret;

	static const struct option longopts[] = {
		{ "verbose",    no_argument,       NULL, 'v' },
		{ "version",    no_argument,       NULL, 'V' },
		{ "help",       no_argument,       NULL, 'h' },
		{ NULL,         0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((ch = getopt_long(argc, argv, "vVh", longopts, NULL)) != -1)
		switch (ch) {
		case 'v':
			verbose = TRUE;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}

	argc -= optind;
	argv += optind;

	if (!isatty(STDERR_FILENO)) {
		if (verbose)
			warnx(_("no tty"));
		exit(MESG_EXIT_FAILURE);
	}
	if ((tty = ttyname(STDERR_FILENO)) == NULL)
		err(MESG_EXIT_FAILURE, _("ttyname failed"));
	if ((fd = open(tty, O_RDONLY)) < 0)
		err(MESG_EXIT_FAILURE, _("cannot open %s"), tty);
	if (fstat(fd, &sb))
		err(MESG_EXIT_FAILURE, _("stat of %s failed"), tty);

	if (!*argv) {
		close(fd);
		if (sb.st_mode & (S_IWGRP | S_IWOTH)) {
			puts(_("is y"));
			return IS_ALLOWED;
		}
		puts(_("is n"));
		return IS_NOT_ALLOWED;
	}

	switch (rpmatch(argv[0])) {
	case RPMATCH_YES:
#ifdef USE_TTY_GROUP
		if (fchmod(fd, sb.st_mode | S_IWGRP) < 0)
#else
		if (fchmod(fd, sb.st_mode | S_IWGRP | S_IWOTH) < 0)
#endif
			err(MESG_EXIT_FAILURE, _("change %s mode failed"), tty);
		if (verbose)
			puts(_("write access to your terminal is allowed"));
		ret = IS_ALLOWED;
		break;
	case RPMATCH_NO:
		if (fchmod(fd, sb.st_mode & ~(S_IWGRP|S_IWOTH)) < 0)
			 err(MESG_EXIT_FAILURE, _("change %s mode failed"), tty);
		if (verbose)
			puts(_("write access to your terminal is denied"));
		ret = IS_NOT_ALLOWED;
		break;
	case RPMATCH_INVALID:
		warnx(_("invalid argument: %s"), argv[0]);
		errtryhelp(EXIT_FAILURE);
        default:
                abort();
	}
	close(fd);
	return ret;
}
