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
 * fdrecv(1) - receive a file descriptor from another process via Unix socket.
 */
#include "c.h"
#include "nls.h"
#include "closestream.h"
#include "fdsend-common.h"

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "strutils.h"
#include "optutils.h"

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] SOCKSPEC --run command args...\n"), program_invocation_short_name);
	fputs(USAGE_SEPARATOR, out);
	fputs(_("Receive a file descriptor from another process via Unix domain socket and exec a command with it.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -f, --fd <num>     dup received fd to this number\n"), out);
	fputs(_(" -r, --run          exec command with received fd (must appear after SOCKSPEC)\n"), out);
	fputs(_(" -i, --stdin        map received fd to command's stdin (fd 0)\n"), out);
	fputs(_(" -o, --stdout       map received fd to command's stdout (fd 1)\n"), out);
	fputs(_(" -e, --stderr       map received fd to command's stderr (fd 2)\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fprintf(out, USAGE_HELP_OPTIONS(20));
	fprintf(out, USAGE_MAN_TAIL("fdrecv(1)"));

	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int c;
	int fd;
	int opt_fd = -1;
	int opt_stdin = 0, opt_stdout = 0, opt_stderr = 0;
	const char *sockspec = NULL;
	char **run_argv = NULL;
	int run_argc = 0;

	static const struct option longopts[] = {
		{ "fd",     required_argument, NULL, 'f' },
		{ "run",    no_argument,       NULL, 'r' },
		{ "stdin",  no_argument,       NULL, 'i' },
		{ "stdout", no_argument,       NULL, 'o' },
		{ "stderr", no_argument,       NULL, 'e' },
		{ "help",   no_argument,       NULL, 'h' },
		{ "version", no_argument,      NULL, 'V' },
		{ NULL, 0, NULL, 0 }
	};
	static const ul_excl_t excl[] = {
		{ 'e', 'f', 'i', 'o' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout_atexit);

	/* '+' so we stop at first non-option (SOCKSPEC) */
	while ((c = getopt_long(argc, argv, "+f:rioehV", longopts, NULL)) != -1) {
		err_exclusive_options(c, longopts, excl, excl_st);
		switch (c) {
		case 'f':
			opt_fd = (int) str2num_or_err(optarg, 10, _("invalid fd number"), 0, INT_MAX);
			break;
		case 'r':
			warnx(_("--run must appear after SOCKSPEC"));
			errtryhelp(EXIT_FAILURE);
		case 'i':
			opt_stdin = 1;
			break;
		case 'o':
			opt_stdout = 1;
			break;
		case 'e':
			opt_stderr = 1;
			break;
		case 'h':
			usage();
		case 'V':
			print_version(EXIT_SUCCESS);
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (optind >= argc) {
		warnx(_("SOCKSPEC is required"));
		errtryhelp(EXIT_FAILURE);
	}
	sockspec = argv[optind];
	optind++;

	/* Parse SOCKSPEC [--run command args...] */
	if (optind < argc && strcmp(argv[optind], "--run") == 0) {
		optind++;
		run_argv = &argv[optind];
		run_argc = argc - optind;
		if (run_argc < 1) {
			warnx(_("--run requires a command"));
			errtryhelp(EXIT_FAILURE);
		}
	}

	/* --run and command are required */
	if (run_argv == NULL) {
		warnx(_("--run and a command are required"));
		errtryhelp(EXIT_FAILURE);
	}
	/* Need --fd or one of --stdin/--stdout/--stderr */
	if (opt_fd < 0 && !opt_stdin && !opt_stdout && !opt_stderr) {
		warnx(_("specify --fd <f> or one of --stdin, --stdout, --stderr"));
		errtryhelp(EXIT_FAILURE);
	}

	/* target_fd: with --stdin/--stdout/--stderr use 0/1/2 */
	if (opt_stdin) opt_fd = 0;
	else if (opt_stdout) opt_fd = 1;
	else if (opt_stderr) opt_fd = 2;

	if (fdrecv_do_recv(sockspec, &fd) != 0) {
		warn(_("receive failed"));
		return EXIT_FAILURE;
	}

	if (dup2(fd, opt_fd) < 0) {
		warn(_("dup2 failed"));
		close(fd);
		return EXIT_FAILURE;
	}
	/* Only close the received fd if it's not the target; dup2(fd, fd) is a no-op. */
	if (fd != opt_fd)
		close(fd);
	execvp(run_argv[0], run_argv);
	errexec(run_argv[0]);

	return EXIT_FAILURE;
}
