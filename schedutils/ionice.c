/*
 * ionice: set or get process io scheduling class and priority
 *
 * Copyright (C) 2005 Jens Axboe <jens@axboe.dk>
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

static int tolerant;

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

static void ioprio_print(int pid)
{
	int ioprio, ioclass;

	ioprio = ioprio_get(IOPRIO_WHO_PROCESS, pid);

	if (ioprio == -1)
		err(EXIT_FAILURE, _("ioprio_get failed"));
	else {
		ioclass = ioprio >> IOPRIO_CLASS_SHIFT;
		if (ioclass != IOPRIO_CLASS_IDLE) {
			ioprio = ioprio & 0xff;
			printf("%s: prio %d\n", to_prio[ioclass], ioprio);
		} else
			printf("%s\n", to_prio[ioclass]);
	}
}


static void ioprio_setpid(pid_t pid, int ioprio, int ioclass)
{
	int rc = ioprio_set(IOPRIO_WHO_PROCESS, pid,
			ioprio | ioclass << IOPRIO_CLASS_SHIFT);

	if (rc == -1 && !tolerant)
		err(EXIT_FAILURE, _("ioprio_set failed"));
}

static void usage(int rc)
{
	fprintf(stdout, _(
	"\nionice - sets or gets process io scheduling class and priority.\n"
	"\nUsage:\n"
	"  ionice [ options ] -p <pid> [<pid> ...]\n"
	"  ionice [ options ] <command> [<arg> ...]\n"
	"\nOptions:\n"
	"  -n <classdata>      class data (0-7, lower being higher prio)\n"
	"  -c <class>          scheduling class\n"
	"                      0: none, 1: realtime, 2: best-effort, 3: idle\n"
	"  -t                  ignore failures\n"
	"  -h                  this help\n\n"));
	exit(rc);
}

static long getnum(const char *str)
{
	long num;
	char *end = NULL;

	if (str == NULL || *str == '\0')
		goto err;
	errno = 0;
	num = strtol(str, &end, 10);

	if (errno || (end && *end))
		goto err;

	return num;
err:
	if (errno)
		err(EXIT_SUCCESS, _("cannot parse number '%s'"), str);
	else
		errx(EXIT_SUCCESS, _("cannot parse number '%s'"), str);
	return 0;
}

int main(int argc, char *argv[])
{
	int ioprio = 4, set = 0, ioclass = IOPRIO_CLASS_BE, c;
	pid_t pid = 0;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while ((c = getopt(argc, argv, "+n:c:p:th")) != EOF) {
		switch (c) {
		case 'n':
			ioprio = getnum(optarg);
			set |= 1;
			break;
		case 'c':
			ioclass = getnum(optarg);
			set |= 2;
			break;
		case 'p':
			pid = getnum(optarg);
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

	switch (ioclass) {
		case IOPRIO_CLASS_NONE:
			if (set & 1)
				warnx(_("ignoring given class data for none class"));
			ioprio = 0;
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
			errx(EXIT_FAILURE, _("bad prio class %d"), ioclass);
	}

	if (!set) {
		ioprio_print(pid);

		for(; argv[optind]; ++optind) {
			pid = getnum(argv[optind]);
			ioprio_print(pid);
		}
	} else {
		if (pid) {
			ioprio_setpid(pid, ioprio, ioclass);

			for(; argv[optind]; ++optind)
			{
				pid = getnum(argv[optind]);
				ioprio_setpid(pid, ioprio, ioclass);
			}
		}
		else if (argv[optind]) {
			ioprio_setpid(0, ioprio, ioclass);
			execvp(argv[optind], &argv[optind]);
			/* execvp should never return */
			err(EXIT_FAILURE, _("execvp failed"));
		}
	}

	exit(EXIT_SUCCESS);
}
