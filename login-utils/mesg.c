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
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 * 2010-12-01 Marek Polacek <mmpolacek@gmail.com>
 * - cleanups
 */

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "nls.h"

/* exit codes */

#define IS_ALLOWED        0  /* Receiving messages is allowed.  */
#define IS_NOT_ALLOWED    1  /* Receiving messages is not allowed.  */
#define MESG_EXIT_FAILURE 2  /* An error occurred.  */

int main(int argc, char *argv[])
{
	struct stat sb;
	char *tty;
	int ch;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while ((ch = getopt(argc, argv, "")) != -1)
		switch (ch) {
		case '?':
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	if ((tty = ttyname(STDERR_FILENO)) == NULL)
		err(MESG_EXIT_FAILURE, _("ttyname failed"));

	if (stat(tty, &sb) < 0)
		err(MESG_EXIT_FAILURE, _("stat %s failed"), tty);

	if (!*argv) {
		if (sb.st_mode & (S_IWGRP | S_IWOTH)) {
			puts(_("is y"));
			return IS_ALLOWED;
		}
		puts(_("is n"));
		return IS_NOT_ALLOWED;
	}

	switch (*argv[0]) {
	case 'y':
#ifdef USE_TTY_GROUP
		if (chmod(tty, sb.st_mode | S_IWGRP) < 0)
#else
		if (chmod(tty, sb.st_mode | S_IWGRP | S_IWOTH) < 0)
#endif
			err(MESG_EXIT_FAILURE, _("change %s mode failed"), tty);
		return IS_ALLOWED;
	case 'n':
		if (chmod(tty, sb.st_mode & ~(S_IWGRP|S_IWOTH)) < 0)
			 err(MESG_EXIT_FAILURE, _("change %s mode failed"), tty);
		return IS_NOT_ALLOWED;
	}

usage:
	fprintf(stderr, _("Usage: %s [y | n]"), program_invocation_short_name);
	return MESG_EXIT_FAILURE;
}
