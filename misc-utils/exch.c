/*
 * exch(1) - a command line interface for RENAME_EXCHANGE of renameat2(2).
 *
 * Copyright (C) 2023 Red Hat, Inc. All rights reserved.
 * Written by Masatake YAMATO <yamato@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "c.h"
#include "nls.h"

#include <fcntl.h>
#include <getopt.h>

#ifndef HAVE_RENAMEAT2

#include <sys/syscall.h>
#include <unistd.h>

#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE (1 << 1)
#endif

static inline int renameat2(int olddirfd, const char *oldpath,
			    int newdirfd, const char *newpath, unsigned int flags)
{
	return syscall (SYS_renameat2, olddirfd, oldpath, newdirfd, newpath, flags);
}

#endif

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] oldpath newpath\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fprintf(out, USAGE_HELP_OPTIONS(30));

	fprintf(out, USAGE_MAN_TAIL("exch(1)"));

	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int c;
	int rc;

	static const struct option longopts[] = {
		{ "version",    no_argument, NULL, 'V' },
		{ "help",	no_argument, NULL, 'h' },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while ((c = getopt_long(argc, argv, "Vh", longopts, NULL)) != -1) {
		switch (c) {
		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (argc - optind < 2) {
		warnx(_("too few arguments"));
		errtryhelp(EXIT_FAILURE);
	} else if (argc - optind > 2) {
		warnx(_("too many arguments"));
		errtryhelp(EXIT_FAILURE);
	}

	rc = renameat2(AT_FDCWD, argv[optind],
		       AT_FDCWD, argv[optind + 1], RENAME_EXCHANGE);
	if (rc)
		warn(_("failed to exchange \"%s\" and \"%s\""),
		     argv[optind], argv[optind + 1]);

	return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
