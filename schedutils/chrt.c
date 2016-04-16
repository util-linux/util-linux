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
#include "exitcodes.h"

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

/* flag by sched_getscheduler() */
#if defined(__linux__) && !defined(SCHED_RESET_ON_FORK)
# define SCHED_RESET_ON_FORK 0x40000000
#endif

/* flag by sched_getattr() */
#if defined(__linux__) && !defined(SCHED_FLAG_RESET_ON_FORK)
# define SCHED_FLAG_RESET_ON_FORK 0x01
#endif


#if defined (__linux__) && !defined(HAVE_SCHED_SETATTR)
# include <sys/syscall.h>
#endif

#if defined (__linux__) && !defined(HAVE_SCHED_SETATTR) && defined(SYS_sched_setattr)
# define HAVE_SCHED_SETATTR

struct sched_attr {
	uint32_t size;
	uint32_t sched_policy;
	uint64_t sched_flags;

	/* SCHED_NORMAL, SCHED_BATCH */
	int32_t sched_nice;

	/* SCHED_FIFO, SCHED_RR */
	uint32_t sched_priority;

	/* SCHED_DEADLINE (nsec) */
	uint64_t sched_runtime;
	uint64_t sched_deadline;
	uint64_t sched_period;
};

static int sched_setattr(pid_t pid, const struct sched_attr *attr, unsigned int flags)
{
	return syscall(SYS_sched_setattr, pid, attr, flags);
}

static int sched_getattr(pid_t pid, struct sched_attr *attr, unsigned int size, unsigned int flags)
{
	return syscall(SYS_sched_getattr, pid, attr, size, flags);
}
#endif

/* the SCHED_DEADLINE is supported since Linux 3.14
 * commit id aab03e05e8f7e26f51dee792beddcb5cca9215a5
 * -- sched_setattr() is required for this policy!
 */
#if defined (__linux__) && !defined(SCHED_DEADLINE) && defined(HAVE_SCHED_SETATTR)
# define SCHED_DEADLINE 6
#endif

/* control struct */
struct chrt_ctl {
	pid_t	pid;
	int	policy;				/* SCHED_* */
	int	priority;

	uint64_t runtime;			/* --sched-* options */
	uint64_t deadline;
	uint64_t period;

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
	" chrt [options] --pid <priority> <pid>\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(_("Get policy:\n"
	" chrt [options] -p <pid>\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Policy options:\n"), out);
	fputs(_(" -b, --batch          set policy to SCHED_BATCH\n"), out);
	fputs(_(" -d, --deadline       set policy to SCHED_DEADLINE\n"), out);
	fputs(_(" -f, --fifo           set policy to SCHED_FIFO\n"), out);
	fputs(_(" -i, --idle           set policy to SCHED_IDLE\n"), out);
	fputs(_(" -o, --other          set policy to SCHED_OTHER\n"), out);
	fputs(_(" -r, --rr             set policy to SCHED_RR (default)\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Scheduling options:\n"), out);
	fputs(_(" -R, --reset-on-fork       set SCHED_RESET_ON_FORK for FIFO or RR\n"), out);
	fputs(_(" -T, --sched-runtime <ns>  runtime parameter for DEADLINE\n"), out);
	fputs(_(" -P, --sched-period <ns>   period parameter for DEADLINE\n"), out);
	fputs(_(" -D, --sched-deadline <ns> deadline parameter for DEADLINE\n"), out);

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

static const char *get_policy_name(int policy)
{
	switch (policy) {
	case SCHED_OTHER:
		return "SCHED_OTHER";
	case SCHED_FIFO:
#ifdef SCHED_RESET_ON_FORK
	case SCHED_FIFO | SCHED_RESET_ON_FORK:
#endif
		return "SCHED_FIFO";
#ifdef SCHED_IDLE
	case SCHED_IDLE:
		return "SCHED_IDLE";
#endif
	case SCHED_RR:
#ifdef SCHED_RESET_ON_FORK
	case SCHED_RR | SCHED_RESET_ON_FORK:
#endif
		return "SCHED_RR";
#ifdef SCHED_BATCH
	case SCHED_BATCH:
		return "SCHED_BATCH";
#endif
#ifdef SCHED_DEADLINE
	case SCHED_DEADLINE:
		return "SCHED_DEADLINE";
#endif
	default:
		break;
	}

	return _("unknown");
}

static void show_sched_pid_info(struct chrt_ctl *ctl, pid_t pid)
{
	int policy, reset_on_fork = 0, prio = 0;
#ifdef SCHED_DEADLINE
	uint64_t deadline = 0, runtime = 0, period = 0;
#endif

	/* don't display "pid 0" as that is confusing */
	if (!pid)
		pid = getpid();

#ifdef HAVE_SCHED_SETATTR
	{
		struct sched_attr sa;

		if (sched_getattr(pid, &sa, sizeof(sa), 0) != 0)
			err(EXIT_FAILURE, _("failed to get pid %d's policy"), pid);

		policy = sa.sched_policy;
		prio = sa.sched_priority;
		reset_on_fork = sa.sched_flags & SCHED_FLAG_RESET_ON_FORK;
		deadline = sa.sched_deadline;
		runtime = sa.sched_runtime;
		period = sa.sched_period;
	}
#else /* !HAVE_SCHED_SETATTR */
	{
		struct sched_param sp;

		policy = sched_getscheduler(pid);
		if (policy == -1)
			err(EXIT_FAILURE, _("failed to get pid %d's policy"), pid);

		if (sched_getparam(pid, &sp) != 0)
			err(EXIT_FAILURE, _("failed to get pid %d's attributes"), pid);
		else
			prio = sp.sched_priority;
# ifdef SCHED_RESET_ON_FORK
		if (policy == (SCHED_FIFO|SCHED_RESET_ON_FORK) || policy == (SCHED_BATCH|SCHED_RESET_ON_FORK))
			reset_on_fork = 1;
# endif
	}
#endif /* !HAVE_SCHED_SETATTR */

	if (ctl->altered)
		printf(_("pid %d's new scheduling policy: %s"), pid, get_policy_name(policy));
	else
		printf(_("pid %d's current scheduling policy: %s"), pid, get_policy_name(policy));

	if (reset_on_fork)
		printf("|SCHED_RESET_ON_FORK");
	putchar('\n');

	if (ctl->altered)
		printf(_("pid %d's new scheduling priority: %d\n"), pid, prio);
	else
		printf(_("pid %d's current scheduling priority: %d\n"), pid, prio);

#ifdef SCHED_DEADLINE
	if (policy == SCHED_DEADLINE) {
		if (ctl->altered)
			printf(_("pid %d's new runtime/deadline/period parameters: %ju/%ju/%ju\n"),
					pid, runtime, deadline, period);
		else
			printf(_("pid %d's current runtime/deadline/period parameters: %ju/%ju/%ju\n"),
					pid, runtime, deadline, period);
	}
#endif
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
#ifdef SCHED_DEADLINE
		SCHED_DEADLINE,
#endif
	};

	for (i = 0; i < ARRAY_SIZE(policies); i++) {
		int plc = policies[i];
		int max = sched_get_priority_max(plc);
		int min = sched_get_priority_min(plc);

		if (max >= 0 && min >= 0)
			printf(_("SCHED_%s min/max priority\t: %d/%d\n"),
					get_policy_name(plc), min, max);
		else
			printf(_("SCHED_%s not supported?\n"), get_policy_name(plc));
	}
}

#ifndef HAVE_SCHED_SETATTR
static int set_sched_one(struct chrt_ctl *ctl, pid_t pid)
{
	struct sched_param sp = { .sched_priority = ctl->priority };
	int policy = ctl->policy;

# ifdef SCHED_RESET_ON_FORK
	if (ctl->reset_on_fork)
		policy |= SCHED_RESET_ON_FORK;
# endif
	return sched_setscheduler(pid, policy, &sp);
}

#else /* !HAVE_SCHED_SETATTR */
static int set_sched_one(struct chrt_ctl *ctl, pid_t pid)
{
	/* use main() to check if the setting makes sense */
	struct sched_attr sa = {
		.size		= sizeof(struct sched_attr),
		.sched_policy	= ctl->policy,
		.sched_priority	= ctl->priority,
		.sched_runtime  = ctl->runtime,
		.sched_period   = ctl->period,
		.sched_deadline = ctl->deadline
	};
# ifdef SCHED_RESET_ON_FORK
	if (ctl->reset_on_fork)
		sa.sched_flags |= SCHED_RESET_ON_FORK;
# endif
	return sched_setattr(pid, &sa, 0);
}
#endif /* HAVE_SCHED_SETATTR */

static void set_sched(struct chrt_ctl *ctl)
{
	if (ctl->all_tasks) {
		pid_t tid;
		struct proc_tasks *ts = proc_open_tasks(ctl->pid);

		if (!ts)
			err(EXIT_FAILURE, _("cannot obtain the list of tasks"));

		while (!proc_next_tid(ts, &tid))
			if (set_sched_one(ctl, tid) == -1)
				err(EXIT_FAILURE, _("failed to set tid %d's policy"), tid);

		proc_close_tasks(ts);

	} else if (set_sched_one(ctl, ctl->pid) == -1)
		err(EXIT_FAILURE, _("failed to set pid %d's policy"), ctl->pid);

	ctl->altered = 1;
}

int main(int argc, char **argv)
{
	struct chrt_ctl _ctl = { .pid = -1 }, *ctl = &_ctl;
	int c;

	static const struct option longopts[] = {
		{ "all-tasks",  no_argument, NULL, 'a' },
		{ "batch",	no_argument, NULL, 'b' },
		{ "deadline",   no_argument, NULL, 'd' },
		{ "fifo",	no_argument, NULL, 'f' },
		{ "idle",	no_argument, NULL, 'i' },
		{ "pid",	no_argument, NULL, 'p' },
		{ "help",	no_argument, NULL, 'h' },
		{ "max",        no_argument, NULL, 'm' },
		{ "other",	no_argument, NULL, 'o' },
		{ "rr",		no_argument, NULL, 'r' },
		{ "sched-runtime",  required_argument, NULL, 'T' },
		{ "sched-period",   required_argument, NULL, 'P' },
		{ "sched-deadline", required_argument, NULL, 'D' },
		{ "reset-on-fork",  no_argument,       NULL, 'R' },
		{ "verbose",	no_argument, NULL, 'v' },
		{ "version",	no_argument, NULL, 'V' },
		{ NULL,		no_argument, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while((c = getopt_long(argc, argv, "+abdD:fiphmoP:T:rRvV", longopts, NULL)) != -1)
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

		case 'd':
#ifdef SCHED_DEADLINE
			ctl->policy = SCHED_DEADLINE;
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
		case 'T':
			ctl->runtime = strtou64_or_err(optarg, _("invalid runtime argument"));
			break;
		case 'P':
			ctl->period = strtou64_or_err(optarg, _("invalid period argument"));
			break;
		case 'D':
			ctl->deadline = strtou64_or_err(optarg, _("invalid deadline argument"));
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
	if (ctl->reset_on_fork && ctl->policy != SCHED_FIFO && ctl->policy != SCHED_RR)
		errx(EXIT_FAILURE, _("--reset-on-fork option is supported for "
				     "SCHED_FIFO and SCHED_RR policies only"));
#endif
#ifdef SCHED_DEADLINE
	if ((ctl->runtime || ctl->deadline || ctl->period) && ctl->policy != SCHED_DEADLINE)
		errx(EXIT_FAILURE, _("--sched-{runtime,deadline,period} options "
				     "are supported for SCHED_DEADLINE only"));
	if (ctl->policy == SCHED_DEADLINE) {
		/* The basic rule is runtime <= deadline <= period, so we can
		 * make deadline and runtime optional on command line. Note we
		 * don't check any values or set any defaults, it's kernel
		 * responsibility.
		 */
		if (ctl->deadline == 0)
			ctl->deadline = ctl->period;
		if (ctl->runtime == 0)
			ctl->runtime = ctl->deadline;
	}
#else
	if (ctl->runtime || ctl->deadline || ctl->period)
		errx(EXIT_FAILURE, _("SCHED_DEADLINE is unsupported"));
#endif
	if (ctl->pid == -1)
		ctl->pid = 0;
	if (ctl->priority < sched_get_priority_min(ctl->policy) ||
	    sched_get_priority_max(ctl->policy) < ctl->priority)
		errx(EXIT_FAILURE,
		     _("unsupported priority value for the policy: %d: see --max for valid range"),
		     ctl->priority);
	set_sched(ctl);

	if (ctl->verbose)
		show_sched_info(ctl);

	if (!ctl->pid) {
		argv += optind + 1;
		execvp(argv[0], argv);
		err(EX_EXEC, _("failed to execute %s"), argv[0]);
	}

	return EXIT_SUCCESS;
}
