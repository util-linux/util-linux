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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org/licenses/>.
 */

#include <sys/epoll.h>
#include <sys/timerfd.h>
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
#include "timeutils.h"
#include "optutils.h"
#include "pidutils.h"

#define EXIT_TIMEOUT_EXPIRED 3

#define TIMEOUT_SOCKET_IDX UINT64_MAX

struct process_info {
	pid_t		pid;
	int		pidfd;
	uint64_t	pidfd_ino;
};

/* list of processes specified by PIDs on the command line */
static struct process_info *proc_infos;

struct waitpid_control {
	size_t 	count;

	bool	allow_exited;
	bool	verbose;

	struct timespec timeout;
};

static void parse_pids_or_err(struct process_info *pinfos, size_t n_strings, char * const *strings)
{
	for (size_t i = 0; i < n_strings; i++) {
		struct process_info *pi = &pinfos[i];
		ul_parse_pid_str_or_err(strings[i], &pi->pid, &pi->pidfd_ino);
	}
}

static inline int get_pidfd(const struct waitpid_control *ctl, struct process_info *pi)
{
	int fd;

	if (pi->pidfd_ino) {
		fd = ul_get_valid_pidfd(pi->pid, pi->pidfd_ino);
		if (fd < 0 && ctl->verbose)
			warnx(_("pidfd inode %"PRIu64" not found for pid %d"),
					pi->pidfd_ino, pi->pid);
	} else {
		fd = pidfd_open(pi->pid, 0);
	}
	return fd;
}

static void open_pidfds_or_err(const struct waitpid_control *ctl, struct process_info *pinfos, size_t n_pids)
{
	for (size_t i = 0; i < n_pids; i++) {
		struct process_info *pi = &pinfos[i];

		pi->pidfd = get_pidfd(ctl, pi);
		if (pi->pidfd < 0) {
			if (ctl->allow_exited && errno == ESRCH) {
				if (ctl->verbose)
					warnx(_("PID %d has exited, skipping"), pi->pid);
				continue;
			}
			err_nosys(EXIT_FAILURE, _("could not open pid %u"), pi->pid);
		}
	}
}

static int open_timeoutfd(const struct waitpid_control *ctl)
{
	int fd;
	struct itimerspec timer = {};

	if (!ctl->timeout.tv_sec && !ctl->timeout.tv_nsec)
		return -1;

	timer.it_value = ctl->timeout;

	fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	if (fd == -1)
		err_nosys(EXIT_FAILURE, _("could not create timerfd"));

	if (timerfd_settime(fd, 0, &timer, NULL))
		err_nosys(EXIT_FAILURE, _("could not set timer"));

	return fd;

}

static size_t add_listeners(int epll, size_t n_pids,
			struct process_info *pinfos, int timeoutfd)
{
	size_t skipped = 0;
	struct epoll_event evt = {
		.events = EPOLLIN,
	};

	if (timeoutfd != -1) {
		evt.data.u64 = TIMEOUT_SOCKET_IDX;
		if (epoll_ctl(epll, EPOLL_CTL_ADD, timeoutfd, &evt))
			err_nosys(EXIT_FAILURE, _("could not add timerfd"));
	}

	for (size_t i = 0; i < n_pids; i++) {
		struct process_info *pi = &pinfos[i];
		if (pi->pidfd == -1) {
			skipped++;
			continue;
		}
		evt.data.u64 = i;
		if (epoll_ctl(epll, EPOLL_CTL_ADD, pi->pidfd, &evt))
			err_nosys(EXIT_FAILURE, _("could not add listener"));
	}

	return n_pids - skipped;
}

static void wait_for_exits(const struct waitpid_control *ctl, int epll,
			size_t active_pids, struct process_info *pinfos)
{
	while (active_pids) {
		struct epoll_event evt;
		int ret, fd;

		ret = epoll_wait(epll, &evt, 1, -1);
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			else
				err_nosys(EXIT_FAILURE, _("failure during wait"));
		}

		if (evt.data.u64 == TIMEOUT_SOCKET_IDX) {
			if (ctl->verbose)
				printf(_("Timeout expired\n"));
			exit(EXIT_TIMEOUT_EXPIRED);
		}

		struct process_info *pi = &pinfos[evt.data.u64];

		if (ctl->verbose)
			printf(_("PID %d finished\n"), pi->pid);
		assert((size_t) ret <= active_pids);
		fd = pi->pidfd;
		epoll_ctl(epll, EPOLL_CTL_DEL, fd, NULL);
		close(fd);
		active_pids -= ret;
	}
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	char *pid_arg = NULL;
#ifdef USE_PIDFD_INO_SUPPORT
	pid_arg = "PID[:inode]";
#else
	pid_arg = "PID";
#endif
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] %s...\n"), program_invocation_short_name, pid_arg);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -v, --verbose           be more verbose\n"), out);
	fputs(_(" -t, --timeout=<timeout> wait at most timeout seconds\n"), out);
	fputs(_(" -e, --exited            allow exited PIDs\n"), out);
	fputs(_(" -c, --count=<count>     number of process exits to wait for\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fprintf(out, USAGE_HELP_OPTIONS(25));

	fprintf(out, USAGE_MAN_TAIL("waitpid(1)"));

	exit(EXIT_SUCCESS);
}

static int parse_options(struct waitpid_control *ctl, int argc, char **argv)
{
	int c;
	static const struct option longopts[] = {
		{ "verbose", no_argument,       NULL, 'v' },
		{ "timeout", required_argument, NULL, 't' },
		{ "exited",  no_argument,       NULL, 'e' },
		{ "count",   required_argument, NULL, 'c' },
		{ "version", no_argument,       NULL, 'V' },
		{ "help",    no_argument,       NULL, 'h' },
		{ 0 }
	};
	static const ul_excl_t excl[] = {       /* rows and cols in ASCII order */
		{ 'c', 'e' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	while ((c = getopt_long (argc, argv, "vVht:c:e", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case 'v':
			ctl->verbose = true;
			break;
		case 't':
			strtotimespec_or_err(optarg, &ctl->timeout,  _("invalid timeout"));
			break;
		case 'e':
			ctl->allow_exited = true;
			break;
		case 'c':
			ctl->count = str2num_or_err(optarg, 10, _("invalid count"),
					       1, INT64_MAX);
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
	int pid_idx, epoll, timeoutfd;
	size_t n_pids, active_pids;
	struct waitpid_control ctl = {
		.allow_exited = false,
		.verbose = false,
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	pid_idx = parse_options(&ctl, argc, argv);
	n_pids = argc - pid_idx;
	if (!n_pids)
		errx(EXIT_FAILURE, _("no PIDs specified"));

	if (ctl.count && ctl.count > n_pids)
		errx(EXIT_FAILURE,
		     _("can't wait for %zu of %zu PIDs"), ctl.count, n_pids);

	proc_infos = xcalloc(n_pids, sizeof(*proc_infos));

	parse_pids_or_err(proc_infos, argc - pid_idx, argv + pid_idx);
	open_pidfds_or_err(&ctl, proc_infos, n_pids);

	timeoutfd = open_timeoutfd(&ctl);
	epoll = epoll_create(n_pids);
	if (epoll == -1)
		err_nosys(EXIT_FAILURE, _("could not create epoll"));

	active_pids = add_listeners(epoll, n_pids, proc_infos, timeoutfd);
	if (ctl.count)
		active_pids = min(active_pids, ctl.count);
	wait_for_exits(&ctl, epoll, active_pids, proc_infos);
}
