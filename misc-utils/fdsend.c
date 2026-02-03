/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) 2026 WanBingjiang <wanbingjiang@webray.com.cn>
 *
 * fdsend(1) - send a file descriptor to another process via Unix socket.
 */
#include "c.h"
#include "nls.h"
#include "closestream.h"
#include "fdsend-common.h"
#include "pidutils.h"

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include "strutils.h"

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] --fd FD SOCKSPEC\n"), program_invocation_short_name);
	fputs(USAGE_SEPARATOR, out);
	fputs(_("Send a file descriptor to another process via Unix domain socket.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -f, --fd <num>     file descriptor to send (required)\n"), out);
	fputs(_(" -p, --pid <pid>   process whose fd to send (default: current process)\n"), out);
	fputs(_(" -b, --blocking    wait for socket to appear before connecting\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fprintf(out, USAGE_HELP_OPTIONS(20));
	fprintf(out, USAGE_MAN_TAIL("fdsend(1)"));

	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int c;
	int opt_fd = -1;
	int opt_blocking = 0;
	pid_t opt_pid = -1;
	const char *sockspec = NULL;

	static const struct option longopts[] = {
		{ "fd",       required_argument, NULL, 'f' },
		{ "pid",      required_argument, NULL, 'p' },
		{ "blocking", no_argument,       NULL, 'b' },
		{ "help",     no_argument,       NULL, 'h' },
		{ "version",  no_argument,       NULL, 'V' },
		{ NULL, 0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout_atexit);

	/* '+' so we stop at first non-option (SOCKSPEC) */
	while ((c = getopt_long(argc, argv, "+f:p:bhV", longopts, NULL)) != -1) {
		switch (c) {
		case 'f':
			opt_fd = (int) str2num_or_err(optarg, 10, _("invalid fd number"), 0, INT_MAX);
			break;
		case 'p':
			ul_parse_pid_str_or_err(optarg, &opt_pid, NULL);
			if (opt_pid < 1)
				errx(EXIT_FAILURE, _("pid must be positive"));
			break;
		case 'b':
			opt_blocking = 1;
			break;
		case 'h':
			usage();
		case 'V':
			print_version(EXIT_SUCCESS);
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (opt_fd < 0) {
		warnx(_("--fd is required"));
		errtryhelp(EXIT_FAILURE);
	}

	if (optind >= argc) {
		warnx(_("SOCKSPEC is required"));
		errtryhelp(EXIT_FAILURE);
	}
	sockspec = argv[optind];
	optind++;
	if (optind < argc) {
		warnx(_("too many arguments"));
		errtryhelp(EXIT_FAILURE);
	}

	if (fdsend_do_send(sockspec, opt_fd, opt_blocking, opt_pid) != 0) {
		warn(_("failed to send fd %d to %s"), opt_fd, sockspec);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
