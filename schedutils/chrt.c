/*
 * chrt.c - chrt
 * Command-line utility for manipulating a task's real-time attributes 
 *
 * Robert Love <rml@tech9.net>
 * 27-Apr-2002: initial version
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, v2, as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Copyright (C) 2004 Robert Love
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>

static void show_usage(const char *cmd)
{
	fprintf(stderr, "chrt (%s)\n", PACKAGE_STRING);
	fprintf(stderr, "usage: %s [options] [prio] [pid | cmd [args...]]\n",
			cmd);
	fprintf(stderr, "manipulate real-time attributes of a process\n");
	fprintf(stderr, "  -f, --fifo                         "
			"set policy to SCHED_FF\n");
	fprintf(stderr, "  -p, --pid                          "
			"operate on existing given pid\n");
	fprintf(stderr, "  -m, --max                          "
			"show min and max valid priorities\n");
	fprintf(stderr, "  -o, --other                        "
			"set policy to SCHED_OTHER\n");
	fprintf(stderr, "  -r, --rr                           "
			"set policy to SCHED_RR (default)\n");
	fprintf(stderr, "  -h, --help                         "
			"display this help\n");
	fprintf(stderr, "  -v, --verbose                      "
			"display status information\n");
	fprintf(stderr, "  -V, --version                      "
			"output version information\n\n");
	fprintf(stderr, "You must give a priority if changing policy.\n\n");
	fprintf(stderr, "Report bugs and send patches to <rml@tech9.net>\n");
}

static void show_rt_info(const char *what, pid_t pid)
{
	struct sched_param sp;
	int policy;

	/* don't display "pid 0" as that is confusing */
	if (!pid)
		pid = getpid();

	policy = sched_getscheduler(pid);
	if (policy == -1) {
		perror("sched_getscheduler");
		fprintf(stderr, "failed to get pid %d's policy\n", pid);
		exit(1);
	}

	printf("pid %d's %s scheduling policy: ", pid, what);
	switch (policy) {
	case SCHED_OTHER:
		printf("SCHED_OTHER\n");
		break;
	case SCHED_FIFO:
		printf("SCHED_FIFO\n");
		break;
	case SCHED_RR:
		printf("SCHED_RR\n");
		break;
	default:
		printf("unknown\n");
	}

	if (sched_getparam(pid, &sp)) {
		perror("sched_getparam");
		fprintf(stderr, "failed to get pid %d's attributes\n", pid);
		exit(1);
	}

	printf("pid %d's %s scheduling priority: %d\n",
		pid, what, sp.sched_priority);
}

static void show_min_max(void)
{
	int max, min;

	max = sched_get_priority_max(SCHED_FIFO);
	min = sched_get_priority_min(SCHED_FIFO);
	if (max >= 0 && min >= 0)
		printf("SCHED_FIFO min/max priority\t: %d/%d\n", min, max);
	else
		printf("SCHED_FIFO not supported?\n");

	max = sched_get_priority_max(SCHED_RR);
	min = sched_get_priority_min(SCHED_RR);
	if (max >= 0 && min >= 0)
		printf("SCHED_RR min/max priority\t: %d/%d\n", min, max);
	else
		printf("SCHED_RR not supported?\n");

	max = sched_get_priority_max(SCHED_OTHER);
	min = sched_get_priority_min(SCHED_OTHER);
	if (max >= 0 && min >= 0)
		printf("SCHED_OTHER min/max priority\t: %d/%d\n", min, max);
	else
		printf("SCHED_OTHER not supported?\n");
}

int main(int argc, char *argv[])
{
	int i, policy = SCHED_RR, priority = 0, verbose = 0;
	struct sched_param sp;
	pid_t pid = 0;

	struct option longopts[] = {
		{ "fifo",	0, NULL, 'f' },
		{ "pid",	0, NULL, 'p' },
		{ "help",	0, NULL, 'h' },
		{ "max",        0, NULL, 'm' },
		{ "other",	0, NULL, 'o' },
		{ "rr",		0, NULL, 'r' },
		{ "verbose",	0, NULL, 'v' },
		{ "version",	0, NULL, 'V' },
		{ NULL,		0, NULL, 0 }
	};

	while((i = getopt_long(argc, argv, "+fphmorvV", longopts, NULL)) != -1)
	{
		int ret = 1;

		switch (i) {
		case 'f':
			policy = SCHED_FIFO;
			break;
		case 'm':
			show_min_max();
			return 0;
		case 'o':
			policy = SCHED_OTHER;
			break;
		case 'p':
			errno = 0;
			pid = strtol(argv[argc - 1], NULL, 10);
			if (errno) {
				perror("strtol");
				fprintf(stderr, "failed to parse pid!\n");
				return 1;
			}
			break;
		case 'r':
			policy = SCHED_RR;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'V':
			printf("chrt (%s)\n", PACKAGE_STRING);
			return 0;
		case 'h':
			ret = 0;
		default:
			show_usage(argv[0]);
			return ret;
		}
		
	}

	if ((pid && argc - optind < 1) || (!pid && argc - optind < 2)) {
		show_usage(argv[0]);
		return 1;
	}

	if (pid && (verbose || argc - optind == 1)) {
		show_rt_info("current", pid);
		if (argc - optind == 1)
			return 0;
	}

	errno = 0;
	priority = strtol(argv[optind], NULL, 10);
	if (errno) {
		perror("strtol");
		fprintf(stderr, "failed to parse priority!\n");
		return 1;
	}

	sp.sched_priority = priority;
	if (sched_setscheduler(pid, policy, &sp) == -1) {
		perror("sched_setscheduler");
		fprintf(stderr, "failed to set pid %d's policy\n", pid);
		return 1;
	}

	if (verbose)
		show_rt_info("new", pid);

	if (!pid) {
		argv += optind + 1;
		execvp(argv[0], argv);
		perror("execvp");
		fprintf(stderr, "failed to execute %s\n", argv[0]);
		return 1;
	}

	return 0;
}
