/*
 * Copyright (C) 2026 Christian Goeschel Ndjomouo <cgoesc2@wgu.edu>
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

#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <err.h>
#include <getopt.h>
#include <errno.h>

#include "xalloc.h"
#include "strutils.h"

static void __attribute__((__noreturn__)) usage(void)
{
	printf("%s [options] <thread count>\n\n", program_invocation_short_name);
	fputs("Options:\n", stdout);
	fputs(" -s, --sleep <n>         <n> seconds threads should sleep (default: 3)\n", stdout);

	exit(EXIT_SUCCESS);
}

#define DEFAULT_NUM_THREADS 5
#define DEFAULT_THREAD_SLEEP_SEC 3

struct thread_ctl {
    int thread_id;
    uint16_t sleep_time;
};

static void* thread_func(void* arg)
{
	uint64_t tid;

#if defined(__linux__)
	tid = syscall(SYS_gettid);
#elif defined(__APPLE__)
	pthread_threadid_np(NULL, &tid);
#else
#error Unsupported operating system
#endif
	struct thread_ctl *tctl = (struct thread_ctl *)arg;
	int id = tctl->thread_id + 2;
	uint16_t sec = tctl->sleep_time;

	printf("Thread %d: %"PRIu64"\n", id, tid);
	sleep(sec);

	free(arg);
	return NULL;
}

int main(int argc, char **argv)
{
	int c, rc = 0;
	uint16_t sleep_time = DEFAULT_THREAD_SLEEP_SEC;
	size_t t_cnt = DEFAULT_NUM_THREADS;

	static const struct option longopts[] = {
		{ "sleep",	1, NULL, 's' },
		{ "help",       0, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};

	while((c = getopt_long(argc, argv, "hs:", longopts, NULL)) != -1) {
		switch(c) {
		case 's':
			sleep_time = strtou16_or_err(optarg, "invalid --sleep argument");
			break;
		case 'h':
			usage();
			break;
		default:
			err(EXIT_FAILURE, "try --help");
		}
	}

	if (argc - optind > 1)
		errx(EXIT_FAILURE, "too many arguments");

	if (optind < argc)
		t_cnt = strtou16_or_err(argv[optind], "invalid <thread count> argument");

	printf("Thread 1: %d\n", getpid());

	pthread_t *threads = xcalloc(t_cnt, sizeof(pthread_t));

	for (size_t i = 0; i < t_cnt; i++) {
		struct thread_ctl *tctl = xmalloc(sizeof(struct thread_ctl));

		tctl->thread_id = i;
		tctl->sleep_time = sleep_time;

		rc = pthread_create(&threads[i], NULL, thread_func, tctl);
		if (rc)
			errx(EXIT_FAILURE, "failed to create thread %zu: %s", i, strerror(rc));
	}

	for (size_t i = 0; i < t_cnt; i++) {
		rc = pthread_join(threads[i], NULL);
		if (rc)
			errx(EXIT_FAILURE, "failed to join thread %zu: %s", i, strerror(rc));
	}

	free(threads);
	return EXIT_SUCCESS;
}
