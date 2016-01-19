/*
 * chrt.c - manipulate a task's real-time attributes
 *
 * 27-Apr-2002: initial version - Robert Love <rml@tech9.net>
 * 04-May-2011: make it thread-aware - Davidlohr Bueso <dave@gnu.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2004 Robert Love
 */

#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>

#include "c.h"
#include "nls.h"
#include "closestream.h"
#include "strutils.h"
#include "procutils.h"

/* the SCHED_BATCH is supported since Linux 2.6.16
 *  -- temporary workaround for people with old glibc headers
 */
#if defined (__linux__) && !defined(SCHED_BATCH)
# define SCHED_BATCH 3
#endif

/* the SCHED_IDLE is supported since Linux 2.6.23
 * commit id 0e6aca43e08a62a48d6770e9a159dbec167bf4c6
 * -- temporary workaround for people with old glibc headers
 */
#if defined (__linux__) && !defined(SCHED_IDLE)
# define SCHED_IDLE 5
#endif

#if defined(__linux__) && !defined(SCHED_RESET_ON_FORK)
#define SCHED_RESET_ON_FORK 0x40000000
#endif

/* control struct */
struct chrt_ctl {
	pid_t	pid;
	int	policy;				/* SCHED_* */
	int	priority;

	unsigned int all_tasks : 1,		/* all threads of the PID */
		     reset_on_fork : 1,		/* SCHED_RESET_ON_FORK */
		     altered : 1,		/* sched_set**() used */
		     verbose : 1;		/* verbose output */
};

static void __attribute__((__noreturn__)) show_usage(int rc)
{
	FILE *out = rc == EXIT_SUCCESS ? stdout : stderr;

	fputs(_("Show or change the real-time scheduling attributes of a process.\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(_("Set policy:\n"
	" chrt [options] <priority> <command> [<arg>...]\n"
	" chrt [options] -p <priority> <pid>\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(_("Get policy:\n"
	" chrt [options] -p <pid>\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Policy options:\n"), out);
	fputs(_(" -b, --batch          set policy to SCHED_BATCH\n"), out);
	fputs(_(" -f, --fifo           set policy to SCHED_FIFO\n"), out);
	fputs(_(" -i, --idle           set policy to SCHED_IDLE\n"), out);
	fputs(_(" -o, --other          set policy to SCHED_OTHER\n"), out);
	fputs(_(" -r, --rr             set policy to SCHED_RR (default)\n"), out);

#ifdef SCHED_RESET_ON_FORK
	fputs(USAGE_SEPARATOR, out);
	fputs(_("Scheduling flag:\n"), out);
	fputs(_(" -R, --reset-on-fork  set SCHED_RESET_ON_FORK for FIFO or RR\n"), out);
#endif
	fputs(USAGE_SEPARATOR, out);
	fputs(_("Other options:\n"), out);
	fputs(_(" -a, --all-tasks      operate on all the tasks (threads) for a given pid\n"), out);
	fputs(_(" -m, --max            show min and max valid priorities\n"), out);
	fputs(_(" -p, --pid            operate on existing given pid\n"), out);
	fputs(_(" -v, --verbose        display status information\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, USAGE_MAN_TAIL("chrt(1)"));
	exit(rc);
}

static void show_sched_pid_info(struct chrt_ctl *ctl, pid_t pid)
{
	struct sched_param sp;
	int policy;

	/* don't display "pid 0" as that is confusing */
	if (!pid)
		pid = getpid();

	policy = sched_getscheduler(pid);
	if (policy == -1)
		err(EXIT_FAILURE, _("failed to get pid %d's policy"), pid);

	if (ctl->altered)
		printf(_("pid %d's new scheduling policy: "), pid);
	else
		printf(_("pid %d's current scheduling policy: "), pid);

	switch (policy) {
	case SCHED_OTHER:
		printf("SCHED_OTHER\n");
		break;
	case SCHED_FIFO:
		printf("SCHED_FIFO\n");
		break;
#ifdef SCHED_RESET_ON_FORK
	case SCHED_FIFO | SCHED_RESET_ON_FORK:
		printf("SCHED_FIFO|SCHED_RESET_ON_FORK\n");
		break;
#endif
#ifdef SCHED_IDLE
	case SCHED_IDLE:
		printf("SCHED_IDLE\n");
		break;
#endif
	case SCHED_RR:
		printf("SCHED_RR\n");
		break;
#ifdef SCHED_RESET_ON_FORK
	case SCHED_RR | SCHED_RESET_ON_FORK:
		printf("SCHED_RR|SCHED_RESET_ON_FORK\n");
		break;
#endif
#ifdef SCHED_BATCH
	case SCHED_BATCH:
		printf("SCHED_BATCH\n");
		break;
#endif
	default:
		warnx(_("unknown scheduling policy"));
	}

	if (sched_getparam(pid, &sp))
		err(EXIT_FAILURE, _("failed to get pid %d's attributes"), pid);

	if (ctl->altered)
		printf(_("pid %d's new scheduling priority: %d\n"),
		       pid, sp.sched_priority);
	else
		printf(_("pid %d's current scheduling priority: %d\n"),
		       pid, sp.sched_priority);
}


static void show_sched_info(struct chrt_ctl *ctl)
{
	if (ctl->all_tasks) {
		pid_t tid;
		struct proc_tasks *ts = proc_open_tasks(ctl->pid);

		if (!ts)
			err(EXIT_FAILURE, _("cannot obtain the list of tasks"));

		while (!proc_next_tid(ts, &tid))
			show_sched_pid_info(ctl, tid);

		proc_close_tasks(ts);
	} else
		show_sched_pid_info(ctl, ctl->pid);
}

static void show_min_max(void)
{
	unsigned long i;
	int policies[] = {
		SCHED_OTHER,
		SCHED_FIFO,
		SCHED_RR,
#ifdef SCHED_BATCH
		SCHED_BATCH,
#endif
#ifdef SCHED_IDLE
		SCHED_IDLE,
#endif
	};
	const char *names[] = {
		"OTHER",
		"FIFO",
		"RR",
#ifdef SCHED_BATCH
		"BATCH",
#endif
#ifdef SCHED_IDLE
		"IDLE",
#endif
	};

	for (i = 0; i < ARRAY_SIZE(policies); i++) {
		int max = sched_get_priority_max(policies[i]);
		int min = sched_get_priority_min(policies[i]);

		if (max >= 0 && min >= 0)
			printf(_("SCHED_%s min/max priority\t: %d/%d\n"),
					names[i], min, max);
		else
			printf(_("SCHED_%s not supported?\n"), names[i]);
	}
}

static void set_sched(struct chrt_ctl *ctl)
{
	struct sched_param sp = { .sched_priority = ctl->priority };

	if (ctl->all_tasks) {
		pid_t tid;
		struct proc_tasks *ts = proc_open_tasks(ctl->pid);

		if (!ts)
			err(EXIT_FAILURE, _("cannot obtain the list of tasks"));

		while (!proc_next_tid(ts, &tid))
			if (sched_setscheduler(tid, ctl->policy, &sp) == -1)
				err(EXIT_FAILURE, _("failed to set tid %d's policy"), tid);

		proc_close_tasks(ts);

	} else if (sched_setscheduler(ctl->pid, ctl->policy, &sp) == -1)
		err(EXIT_FAILURE, _("failed to set pid %d's policy"), ctl->pid);

	ctl->altered = 1;
}

int main(int argc, char **argv)
{
	struct chrt_ctl _ctl = { .pid = -1 }, *ctl = &_ctl;
	int c;

	static const struct option longopts[] = {
		{ "all-tasks",  0, NULL, 'a' },
		{ "batch",	0, NULL, 'b' },
		{ "fifo",	0, NULL, 'f' },
		{ "idle",	0, NULL, 'i' },
		{ "pid",	0, NULL, 'p' },
		{ "help",	0, NULL, 'h' },
		{ "max",        0, NULL, 'm' },
		{ "other",	0, NULL, 'o' },
		{ "rr",		0, NULL, 'r' },
		{ "reset-on-fork", 0, NULL, 'R' },
		{ "verbose",	0, NULL, 'v' },
		{ "version",	0, NULL, 'V' },
		{ NULL,		0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while((c = getopt_long(argc, argv, "+abfiphmoRrvV", longopts, NULL)) != -1)
	{
		int ret = EXIT_FAILURE;

		switch (c) {
		case 'a':
			ctl->all_tasks = 1;
			break;
		case 'b':
#ifdef SCHED_BATCH
			ctl->policy = SCHED_BATCH;
#endif
			break;
		case 'f':
			ctl->policy = SCHED_FIFO;
			break;
		case 'R':
			ctl->reset_on_fork = 1;
			break;
		case 'i':
#ifdef SCHED_IDLE
			ctl->policy = SCHED_IDLE;
#endif
			break;
		case 'm':
			show_min_max();
			return EXIT_SUCCESS;
		case 'o':
			ctl->policy = SCHED_OTHER;
			break;
		case 'p':
			errno = 0;
			ctl->pid = strtos32_or_err(argv[argc - 1], _("invalid PID argument"));
			break;
		case 'r':
			ctl->policy = SCHED_RR;
			break;
		case 'v':
			ctl->verbose = 1;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			ret = EXIT_SUCCESS;
			/* fallthrough */
		default:
			show_usage(ret);
		}
	}

	if (((ctl->pid > -1) && argc - optind < 1) ||
	    ((ctl->pid == -1) && argc - optind < 2))
		show_usage(EXIT_FAILURE);

	if ((ctl->pid > -1) && (ctl->verbose || argc - optind == 1)) {
		show_sched_info(ctl);
		if (argc - optind == 1)
			return EXIT_SUCCESS;
	}

	errno = 0;
	ctl->priority = strtos32_or_err(argv[optind], _("invalid priority argument"));

#ifdef SCHED_RESET_ON_FORK
	if (ctl->reset_on_fork) {
		if (ctl->policy != SCHED_FIFO && ctl->policy != SCHED_RR)
			errx(EXIT_FAILURE, _("SCHED_RESET_ON_FORK flag is supported for "
					     "SCHED_FIFO and SCHED_RR policies only"));
		ctl->policy |= SCHED_RESET_ON_FORK;
	}
#endif

	if (ctl->pid == -1)
		ctl->pid = 0;

	set_sched(ctl);

	if (ctl->verbose)
		show_sched_info(ctl);

	if (!ctl->pid) {
		argv += optind + 1;
		execvp(argv[0], argv);
		err(EXIT_FAILURE, _("failed to execute %s"), argv[0]);
	}

	return EXIT_SUCCESS;
}
