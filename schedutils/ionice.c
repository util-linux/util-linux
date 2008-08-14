/*
 * ionice: set or get process io scheduling class and priority
 *
 * Copyright (C) 2005 Jens Axboe <axboe@suse.de> SUSE Labs
 *
 * Released under the terms of the GNU General Public License version 2
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <asm/unistd.h>
#include <err.h>

#include "nls.h"

static inline int ioprio_set(int which, int who, int ioprio)
{
	return syscall(SYS_ioprio_set, which, who, ioprio);
}

static inline int ioprio_get(int which, int who)
{
	return syscall(SYS_ioprio_get, which, who);
}

enum {
	IOPRIO_CLASS_NONE,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE,
};

enum {
	IOPRIO_WHO_PROCESS = 1,
	IOPRIO_WHO_PGRP,
	IOPRIO_WHO_USER,
};

#define IOPRIO_CLASS_SHIFT	13

const char *to_prio[] = { "none", "realtime", "best-effort", "idle", };

static void usage(int rc)
{
	fprintf(stdout, _(
	"\nionice - sets or gets process io scheduling class and priority.\n\n"
	"Usage: ionice [OPTIONS] [COMMAND [ARG]...]\n\n"
	"  -n <classdata>      class data (0-7, lower being higher prio)\n"
	"  -c <class>          scheduling class\n"
	"                      1: realtime, 2: best-effort, 3: idle\n"
	"  -p <pid>            process pid\n"
	"  -t                  ignore failures, run command unconditionally\n"
	"  -h                  this help\n\n"));

	exit(rc);
}

int main(int argc, char *argv[])
{
	int ioprio = 4, set = 0, tolerant = 0, ioprio_class = IOPRIO_CLASS_BE;
	int c, pid = 0;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while ((c = getopt(argc, argv, "+n:c:p:th")) != EOF) {
		switch (c) {
		case 'n':
			ioprio = strtol(optarg, NULL, 10);
			set |= 1;
			break;
		case 'c':
			ioprio_class = strtol(optarg, NULL, 10);
			set |= 2;
			break;
		case 'p':
			pid = strtol(optarg, NULL, 10);
			break;
		case 't':
			tolerant = 1;
			break;
		case 'h':
			usage(EXIT_SUCCESS);
		default:
			usage(EXIT_FAILURE);
		}
	}

	switch (ioprio_class) {
		case IOPRIO_CLASS_NONE:
			ioprio_class = IOPRIO_CLASS_BE;
			break;
		case IOPRIO_CLASS_RT:
		case IOPRIO_CLASS_BE:
			break;
		case IOPRIO_CLASS_IDLE:
			if (set & 1)
				warnx(_("ignoring given class data for idle class"));
			ioprio = 7;
			break;
		default:
			errx(EXIT_FAILURE, _("bad prio class %d"), ioprio_class);
	}

	if (!set) {
		if (!pid && argv[optind])
			pid = strtol(argv[optind], NULL, 10);

		ioprio = ioprio_get(IOPRIO_WHO_PROCESS, pid);

		if (ioprio == -1)
			err(EXIT_FAILURE, _("ioprio_get failed"));
		else {
			ioprio_class = ioprio >> IOPRIO_CLASS_SHIFT;
			if (ioprio_class != IOPRIO_CLASS_IDLE) {
				ioprio = ioprio & 0xff;
				printf("%s: prio %d\n", to_prio[ioprio_class], ioprio);
			} else
				printf("%s\n", to_prio[ioprio_class]);
		}
	} else {
		if (ioprio_set(IOPRIO_WHO_PROCESS, pid, ioprio | ioprio_class << IOPRIO_CLASS_SHIFT) == -1) {
			if (!tolerant)
				err(EXIT_FAILURE, _("ioprio_set failed"));
		}

		if (argv[optind]) {
			execvp(argv[optind], &argv[optind]);
			/* execvp should never return */
			err(EXIT_FAILURE, _("execvp failed"));
		}
	}

	exit(EXIT_SUCCESS);
}
