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

#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "strutils.h"

#define FDRECV_PAIRS_MAX 64
/* prevents safe_fd overflow in relocate_conflicting_fds(). */
#define FDRECV_TARGET_FD_MAX (1024 * 1024)

#define CHECK_PAIRS_MAX(msg) do { \
	if (n_pairs >= FDRECV_PAIRS_MAX) { \
		warnx(_(msg), FDRECV_PAIRS_MAX); \
		errtryhelp(EXIT_FAILURE); \
	} \
} while (0)

struct fdrecv_pair {
	int target_fd;
	const char *sockspec;
	int abstract;
};

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] ([-a] (-f N|-i|-o|-e) SOCKSPEC)... --run command [args...]\n"), program_invocation_short_name);
	fputs(USAGE_SEPARATOR, out);
	fputs(_("Receive file descriptors from other processes via Unix domain sockets and run a command with them.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -f, --fd <num>     map received fd to <num>\n"), out);
	fputs(_(" -r, --run          exec command with received fds (must appear after all SOCKSPECs)\n"), out);
	fputs(_(" -a, --abstract     SOCKSPEC is abstract\n"), out);
	fputs(_(" -i, --stdin        map received fd to command's stdin (fd 0)\n"), out);
	fputs(_(" -o, --stdout       map received fd to command's stdout (fd 1)\n"), out);
	fputs(_(" -e, --stderr       map received fd to command's stderr (fd 2)\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fprintf(out, USAGE_HELP_OPTIONS(20));
	fprintf(out, USAGE_MAN_TAIL("fdrecv(1)"));

	exit(EXIT_SUCCESS);
}

static void check_duplicate_target_fds(const struct fdrecv_pair *pairs, int n_pairs)
{
	int i, j;

	for (i = 0; i < n_pairs; i++) {
		for (j = i + 1; j < n_pairs; j++) {
			if (pairs[i].target_fd == pairs[j].target_fd) {
				warnx(_("duplicate target fd %d"), pairs[i].target_fd);
				errtryhelp(EXIT_FAILURE);
			}
		}
	}
}

/* Receive from each SOCKSPEC in order; on any failure, close already-received fds and exit. */
static void recv_all_fds(const struct fdrecv_pair *pairs, int *recv_fds, int n_pairs)
{
	int i;

	for (i = 0; i < n_pairs; i++) {
		if (fdrecv_do_recv(pairs[i].sockspec, &recv_fds[i], pairs[i].abstract) != 0) {
			int fail_idx = i;
			while (i > 0)
				close(recv_fds[--i]);
			err(EXIT_FAILURE, _("receive failed: %s"), pairs[fail_idx].sockspec);
		}
	}
}

/*
 * Relocate received fds that would be clobbered by a later dup2.
 * If recv_fds[j] equals pairs[i].target_fd for some i != j and
 * recv_fds[j] != pairs[j].target_fd, dup it to a fd number above
 * all target fds so it cannot collide with any target.
 */
static void relocate_conflicting_fds(const struct fdrecv_pair *pairs, int *recv_fds, int n_pairs)
{
	int i, j, safe_fd = 0;

	for (i = 0; i < n_pairs; i++) {
		if (pairs[i].target_fd >= safe_fd)
			safe_fd = pairs[i].target_fd + 1;
	}

	for (i = 0; i < n_pairs; i++) {
		for (j = 0; j < n_pairs; j++) {
			if (j == i)
				continue;
			if (recv_fds[j] == pairs[i].target_fd
			    && recv_fds[j] != pairs[j].target_fd) {
				int new_fd = fcntl(recv_fds[j], F_DUPFD, safe_fd);
				if (new_fd < 0)
					err(EXIT_FAILURE, _("dup failed for fd %d"), recv_fds[j]);
				close(recv_fds[j]);
				recv_fds[j] = new_fd;
			}
		}
	}
}

/* Map each received fd to its target fd; on dup2 failure, close all received fds and exit. */
static void map_received_fds(const struct fdrecv_pair *pairs, int *recv_fds, int n_pairs)
{
	int i, j;

	relocate_conflicting_fds(pairs, recv_fds, n_pairs);

	for (i = 0; i < n_pairs; i++) {
		if (recv_fds[i] == pairs[i].target_fd)
			continue;
		if (dup2(recv_fds[i], pairs[i].target_fd) < 0) {
			for (j = 0; j < n_pairs; j++) {
				if (recv_fds[j] != pairs[j].target_fd)
					close(recv_fds[j]);
			}
			err(EXIT_FAILURE, _("map received fd failed"));
		}
	}
}

/* Close received fds that differ from their target (the dup2 copies are the ones we keep). */
static void close_unused_received_fds(const struct fdrecv_pair *pairs, const int *recv_fds, int n_pairs)
{
	int i;

	for (i = 0; i < n_pairs; i++) {
		if (recv_fds[i] != pairs[i].target_fd)
			close(recv_fds[i]);
	}
}

int main(int argc, char **argv)
{
	int c;
	int next_abstract = 0;
	struct fdrecv_pair pairs[FDRECV_PAIRS_MAX];
	int n_pairs = 0;
	int recv_fds[FDRECV_PAIRS_MAX];
	char **run_argv = NULL;

	static const struct option longopts[] = {
		{ "fd",       required_argument, NULL, 'f' },
		{ "run",      no_argument,       NULL, 'r' },
		{ "abstract", no_argument,       NULL, 'a' },
		{ "stdin",    no_argument,       NULL, 'i' },
		{ "stdout",   no_argument,       NULL, 'o' },
		{ "stderr",   no_argument,       NULL, 'e' },
		{ "help",     no_argument,       NULL, 'h' },
		{ "version",  no_argument,       NULL, 'V' },
		{ NULL, 0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout_atexit);

	/* '+' keeps command arguments after --run out of fdrecv option parsing. */
	while ((c = getopt_long(argc, argv, "+f:riaoehV", longopts, NULL)) != -1) {
		int target_fd = -1;

		switch (c) {
		case 'f':
			target_fd = (int) str2num_or_err(optarg, 10, _("invalid fd number"),
							 0, FDRECV_TARGET_FD_MAX);
			break;
		case 'i':
			target_fd = 0;
			break;
		case 'o':
			target_fd = 1;
			break;
		case 'e':
			target_fd = 2;
			break;
		case 'a':
			next_abstract = 1;
			continue;
		case 'r':
			run_argv = &argv[optind];
			goto done_options;
		case 'h':
			usage();
		case 'V':
			print_version(EXIT_SUCCESS);
		default:
			errtryhelp(EXIT_FAILURE);
		}

		if (target_fd >= 0) {
			int pair_abstract = next_abstract;

			CHECK_PAIRS_MAX("too many SOCKSPEC groups (max %d)");

			/*
			 * Keep compatibility with the original syntax: -a may be
			 * placed before the target option or between it and SOCKSPEC.
			 */
			while (optind < argc &&
			       (strcmp(argv[optind], "-a") == 0 ||
				strcmp(argv[optind], "--abstract") == 0)) {
				pair_abstract = 1;
				optind++;
			}
			next_abstract = 0;

			if (optind >= argc || strcmp(argv[optind], "-r") == 0 ||
			    strcmp(argv[optind], "--run") == 0) {
				warnx(_("SOCKSPEC is required"));
				errtryhelp(EXIT_FAILURE);
			}

			pairs[n_pairs].target_fd = target_fd;
			pairs[n_pairs].sockspec = argv[optind++];
			pairs[n_pairs].abstract = pair_abstract;
			n_pairs++;
		}
	}

done_options:
	if (!run_argv) {
		if (optind < argc)
			warnx(_("unexpected argument before --run: %s"), argv[optind]);
		else
			warnx(_("--run and a command are required"));
		errtryhelp(EXIT_FAILURE);
	}
	if (!run_argv[0]) {
		warnx(_("--run and a command are required"));
		errtryhelp(EXIT_FAILURE);
	}

	if (n_pairs == 0) {
		warnx(_("at least one SOCKSPEC is required"));
		errtryhelp(EXIT_FAILURE);
	}

	check_duplicate_target_fds(pairs, n_pairs);
	recv_all_fds(pairs, recv_fds, n_pairs);
	map_received_fds(pairs, recv_fds, n_pairs);
	close_unused_received_fds(pairs, recv_fds, n_pairs);

	execvp(run_argv[0], run_argv);
	errexec(run_argv[0]);
}
