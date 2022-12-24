/*
 * waitpid(1) - wait for process termination
 *
 * Copyright (C) 2022 Thomas Weißschuh <thomas@t-8ch.de>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <sys/epoll.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <getopt.h>

#include "pidfd-utils.h"
#include "c.h"
#include "nls.h"
#include "xalloc.h"
#include "strutils.h"
#include "exitcodes.h"

#define err_nosys(exitcode, ...) \
	err(errno == ENOSYS ? EXIT_NOTSUPP : exitcode, __VA_ARGS__)

static bool verbose = false;

static pid_t *parse_pids(size_t n_strings, char * const *strings)
{
	pid_t *pids = xcalloc(n_strings, sizeof(*pids));

	for (size_t i = 0; i < n_strings; i++)
		pids[i] = strtopid_or_err(strings[i], _("failed to parse pid"));

	return pids;
}

static int *open_pidfds(size_t n_pids, pid_t *pids)
{
	int *pidfds = xcalloc(n_pids, sizeof(*pidfds));

	for (size_t i = 0; i < n_pids; i++) {
		pidfds[i] = pidfd_open(pids[i], 0);
		if (pidfds[i] == -1)
			err_nosys(EXIT_FAILURE, _("could not open pid %u"), pids[i]);
	}

	return pidfds;
}

static void add_listeners(int epll, size_t n_pids, int * const pidfds)
{
	for (size_t i = 0; i < n_pids; i++) {
		struct epoll_event evt = {
			.events = EPOLLIN,
			.data = { .u64 = i },
		};
		if (epoll_ctl(epll, EPOLL_CTL_ADD, pidfds[i], &evt))
			err_nosys(EXIT_FAILURE, _("could not add listener"));
	}
}

static void wait_for_exits(int epll, size_t n_pids, pid_t * const pids, int * const pidfds)
{
	while (n_pids) {
		struct epoll_event evt;
		int ret, fd;

		ret = epoll_wait(epll, &evt, 1, -1);
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			else
				err_nosys(EXIT_FAILURE, _("failure during wait"));
		}
		if (verbose)
			printf(_("PID %d finished\n"), pids[evt.data.u64]);
		assert((size_t) ret <= n_pids);
		fd = pidfds[evt.data.u64];
		epoll_ctl(epll, EPOLL_CTL_DEL, fd, NULL);
		n_pids -= ret;
	}
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] pid...\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -v, --verbose         be more verbose\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fprintf(out, USAGE_HELP_OPTIONS(23));

	fprintf(out, USAGE_MAN_TAIL("waitpid(1)"));

	exit(EXIT_SUCCESS);
}

static int parse_options(int argc, char **argv)
{
	int c;
	static const struct option longopts[] = {
		{ "verbose", no_argument, NULL, 'v' },
		{ "version", no_argument, NULL, 'V' },
		{ "help",    no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};

	while ((c = getopt_long (argc, argv, "vVh", longopts, NULL)) != -1) {
		switch (c) {
		case 'v':
			verbose = true;
			break;
		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	return optind;
}

int main(int argc, char **argv)
{
	int pid_idx, epoll;
	size_t n_pids;
	int *pidfds;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	pid_idx = parse_options(argc, argv);
	n_pids = argc - pid_idx;
	if (!n_pids)
		return EXIT_FAILURE;

	pid_t *pids = parse_pids(argc - pid_idx, argv + pid_idx);

	pidfds = open_pidfds(n_pids, pids);
	epoll = epoll_create(n_pids);
	if (epoll == -1)
		err_nosys(EXIT_FAILURE, _("could not create epoll"));

	add_listeners(epoll, n_pids, pidfds);
	wait_for_exits(epoll, n_pids, pids, pidfds);
}
