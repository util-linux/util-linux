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

#if !defined(SYS_ioprio_get) || !defined(SYS_ioprio_set)

# if defined(__i386__)
#  define __NR_ioprio_set		289
#  define __NR_ioprio_get		290
# elif defined(__powerpc__) || defined(__powerpc64__)
#  define __NR_ioprio_set		273
#  define __NR_ioprio_get		274
# elif defined(__x86_64__)
#  define __NR_ioprio_set		251
#  define __NR_ioprio_get		252
# elif defined(__ia64__)
#  define __NR_ioprio_set		1274
#  define __NR_ioprio_get		1275
# elif defined(__alpha__)
#  define __NR_ioprio_set		442
#  define __NR_ioprio_get		443
# elif defined(__s390x__) || defined(__s390__)
#  define __NR_ioprio_set		282
#  define __NR_ioprio_get		283
# elif defined(__sparc__) || defined(__sparc64__)
#  define __NR_ioprio_set		196
#  define __NR_ioprio_get		218
# else
#  error "Unsupported arch"
# endif

# define SYS_ioprio_get		__NR_ioprio_get
# define SYS_ioprio_set		__NR_ioprio_set

#endif /* !SYS_ioprio_get */

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

static void usage(void)
{
	printf("Usage: ionice [OPTIONS] [COMMAND [ARG]...]\n");
	printf("Sets or gets process io scheduling class and priority.\n");
	printf("\n\t-n\tClass data (typically 0-7, lower being higher prio)\n");
	printf("\t-c\tScheduling class\n");
	printf("\t\t\t1: realtime, 2: best-effort, 3: idle\n");
	printf("\t-p\tProcess pid\n");
	printf("\t-h\tThis help page\n");
	printf("\nJens Axboe <axboe@suse.de> (C) 2005\n");
}

int main(int argc, char *argv[])
{
	int ioprio = 4, set = 0, ioprio_class = IOPRIO_CLASS_BE;
	int c, pid = 0;

	while ((c = getopt(argc, argv, "+n:c:p:h")) != EOF) {
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
		case 'h':
		default:
			usage();
			exit(EXIT_SUCCESS);
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
				printf("Ignoring given class data for idle class\n");
			ioprio = 7;
			break;
		default:
			printf("bad prio class %d\n", ioprio_class);
			exit(EXIT_FAILURE);
	}

	if (!set) {
		if (!pid && argv[optind])
			pid = strtol(argv[optind], NULL, 10);

		ioprio = ioprio_get(IOPRIO_WHO_PROCESS, pid);

		if (ioprio == -1) {
			perror("ioprio_get");
			exit(EXIT_FAILURE);
		} else {
			ioprio_class = ioprio >> IOPRIO_CLASS_SHIFT;
			if (ioprio_class != IOPRIO_CLASS_IDLE) {
				ioprio = ioprio & 0xff;
				printf("%s: prio %d\n", to_prio[ioprio_class], ioprio);
			} else
				printf("%s\n", to_prio[ioprio_class]);
		}
	} else {
		if (ioprio_set(IOPRIO_WHO_PROCESS, pid, ioprio | ioprio_class << IOPRIO_CLASS_SHIFT) == -1) {
			perror("ioprio_set");
			exit(EXIT_FAILURE);
		}

		if (argv[optind]) {
			execvp(argv[optind], &argv[optind]);
			/* execvp should never return */
			perror("execvp");
			exit(EXIT_FAILURE);
		}
	}

	exit(EXIT_SUCCESS);
}
