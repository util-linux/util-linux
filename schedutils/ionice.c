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
#include <sys/syscall.h>

#include "nls.h"
#include "strutils.h"
#include "c.h"

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

#define IOPRIO_CLASS_SHIFT	(13)
#define IOPRIO_PRIO_MASK	((1UL << IOPRIO_CLASS_SHIFT) - 1)

#define IOPRIO_PRIO_CLASS(mask)	((mask) >> IOPRIO_CLASS_SHIFT)
#define IOPRIO_PRIO_DATA(mask)	((mask) & IOPRIO_PRIO_MASK)
#define IOPRIO_PRIO_VALUE(class, data)	(((class) << IOPRIO_CLASS_SHIFT) | data)

const char *to_prio[] = {
	[IOPRIO_CLASS_NONE] = "none",
	[IOPRIO_CLASS_RT]   = "realtime",
	[IOPRIO_CLASS_BE]   = "best-effort",
	[IOPRIO_CLASS_IDLE] = "idle"
};

static void ioprio_print(int pid)
{
	int ioprio = ioprio_get(IOPRIO_WHO_PROCESS, pid);

	if (ioprio == -1)
		err(EXIT_FAILURE, _("ioprio_get failed"));
	else {
		int ioclass = IOPRIO_PRIO_CLASS(ioprio);

		if (ioclass != IOPRIO_CLASS_IDLE)
			printf("%s: prio %lu\n", to_prio[ioclass],
					IOPRIO_PRIO_DATA(ioprio));
		else
			printf("%s\n", to_prio[ioclass]);
	}
}

static void ioprio_setpid(pid_t pid, int ioclass, int data)
{
	int rc = ioprio_set(IOPRIO_WHO_PROCESS, pid,
			    IOPRIO_PRIO_VALUE(ioclass, data));

	if (rc == -1 && !tolerant)
		err(EXIT_FAILURE, _("ioprio_set failed"));
}

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fprintf(out,
	       _("\n"
		 "%1$s - sets or gets process io scheduling class and priority.\n"
		 "\n"
		 "Usage:\n"
		 "  %1$s [OPTION] -p PID [PID...]\n"
		 "  %1$s [OPTION] COMMAND\n"
		 "\n"
		 "Options:\n"
		 "  -c, --class=NUM     scheduling class\n"
		 "                         0: none, 1: realtime, 2: best-effort, 3: idle\n"
		 "  -n, --classdata=NUM scheduling class data\n"
		 "                         0-7 for realtime and best-effort classes\n"
		 "  -p, --pid=PID       view or modify already running process\n"
		 "  -t, --ignore        ignore failures\n"
		 "  -V, --version       output version information and exit\n"
		 "  -h, --help          display this help and exit\n\n"),
		program_invocation_short_name);

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	int data = 4, set = 0, ioclass = IOPRIO_CLASS_BE, c;
	pid_t pid = 0;

	static const struct option longopts[] = {
		{ "classdata", required_argument, NULL, 'n' },
		{ "class",     required_argument, NULL, 'c' },
		{ "help",      no_argument,       NULL, 'h' },
		{ "ignore",    no_argument,       NULL, 't' },
		{ "pid",       required_argument, NULL, 'p' },
		{ "version",   no_argument,       NULL, 'V' },
		{ NULL, 0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while ((c = getopt_long(argc, argv, "+n:c:p:tVh", longopts, NULL)) != EOF) {
		switch (c) {
		case 'n':
			data = strtol_or_err(optarg, _("failed to parse class data"));
			set |= 1;
			break;
		case 'c':
			ioclass = strtol_or_err(optarg, _("failed to parse class"));
			set |= 2;
			break;
		case 'p':
			pid = strtol_or_err(optarg, _("failed to parse pid"));
			break;
		case 't':
			tolerant = 1;
			break;
		case 'V':
			printf(_("%s (%s)\n"),
				program_invocation_short_name, PACKAGE_STRING);
			exit(EXIT_SUCCESS);
		case 'h':
			usage(stdout);
		default:
			usage(stderr);
		}
	}

	if (!set && !pid && optind == argc)
		errx(EXIT_FAILURE, _("PID or COMMAND not specified"));
	if (!set && !pid)
		errx(EXIT_FAILURE, _("scheduling for the COMMAND not specified"));

	switch (ioclass) {
		case IOPRIO_CLASS_NONE:
			if (set & 1)
				warnx(_("ignoring given class data for none class"));
			data = 0;
			break;
		case IOPRIO_CLASS_RT:
		case IOPRIO_CLASS_BE:
			break;
		case IOPRIO_CLASS_IDLE:
			if (set & 1)
				warnx(_("ignoring given class data for idle class"));
			data = 7;
			break;
		default:
			errx(EXIT_FAILURE, _("bad prio class %d"), ioclass);
	}

	if (!set) {
		ioprio_print(pid);

		for(; argv[optind]; ++optind) {
			pid = strtol_or_err(argv[optind], _("failed to parse pid"));
			ioprio_print(pid);
		}
	} else {
		if (pid) {
			ioprio_setpid(pid, ioclass, data);

			for(; argv[optind]; ++optind) {
				pid = strtol_or_err(argv[optind], _("failed to parse pid"));
				ioprio_setpid(pid, ioclass, data);
			}
		} else if (argv[optind]) {
			ioprio_setpid(0, ioclass, data);
			execvp(argv[optind], &argv[optind]);
			/* execvp should never return */
			err(EXIT_FAILURE, _("executing %s failed"), argv[optind]);
		}
	}

	exit(EXIT_SUCCESS);
}
