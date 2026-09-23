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
#include "vfs.h"

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] --fd FD SOCKSPEC\n"), program_invocation_short_name);
	fputs(USAGE_SEPARATOR, out);
	fputs(_("Send a file descriptor to another process via Unix domain socket.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -f, --fd <num>     file descriptor to send (required)\n"), out);
	fputs(_(" -p, --pid <pid>    process whose fd to send (default: current process)\n"), out);
	fputs(_(" -b, --blocking     wait/retry until receiver is available\n"), out);
	fputs(_(" -a, --abstract     SOCKSPEC is an abstract Unix socket name (Linux)\n"), out);
	fputs(_(" -d, --dup          duplicate fd (shared offset) instead of opening a new copy\n"), out);
	fputs(_(" -m, --mode <mode>  open mode for /proc/PID/fd/FD: r, r+, w\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fprintf(out, USAGE_HELP_OPTIONS(20));
	fprintf(out, USAGE_MAN_TAIL("fdsend(1)"));

	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int c, opt_fd = -1;
	const char *sockspec = NULL;
	struct fdsend_opts opts = { .pid = -1, .open_mode = -1 };

	static const struct option longopts[] = {
		{ "fd",          required_argument, NULL, 'f' },
		{ "pid",         required_argument, NULL, 'p' },
		{ "blocking",    no_argument,       NULL, 'b' },
		{ "abstract",    no_argument,       NULL, 'a' },
		{ "dup",         no_argument,       NULL, 'd' },
		{ "mode",        required_argument, NULL, 'm' },
		{ "help",        no_argument,       NULL, 'h' },
		{ "version",     no_argument,       NULL, 'V' },
		{ NULL, 0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout_atexit);

	/* '+' so we stop at first non-option (SOCKSPEC) */
	while ((c = getopt_long(argc, argv, "+f:p:m:badhV", longopts, NULL)) != -1) {
		switch (c) {
		case 'f':
			opt_fd = str2num_or_err(optarg, 10, _("invalid fd number"), 0, INT_MAX);
			break;
		case 'p':
			ul_parse_pid_str_or_err(optarg, &opts.pid, &opts.pidfd_ino, 0);
			break;
		case 'b':
			opts.blocking = 1;
			break;
		case 'a':
			opts.abstract = 1;
			break;
		case 'd':
			opts.dup_fd = 1;
			break;
		case 'm':
			/* Validate against the documented modes; an invalid string
			 * would otherwise be silently treated as O_RDONLY (0). */
			if (strcmp(optarg, "r") != 0 && strcmp(optarg, "r+") != 0 &&
			    strcmp(optarg, "w") != 0) {
				warnx(_("invalid mode '%s' (use r, r+ or w)"), optarg);
				errtryhelp(EXIT_FAILURE);
			}
			opts.open_mode = ul_mode_to_flags(optarg) & O_ACCMODE;
			break;
		case 'h':
			usage();
		case 'V':
			print_version(EXIT_SUCCESS);
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (opts.dup_fd && opts.open_mode >= 0) {
		warnx(_("--mode is incompatible with --dup"));
		errtryhelp(EXIT_FAILURE);
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

	if (fdsend_do_send(sockspec, opt_fd, &opts) != 0)
		err(EXIT_FAILURE, _("failed to send fd %d to %s"), opt_fd, sockspec);

	return EXIT_SUCCESS;
}
