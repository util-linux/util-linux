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

#include <sys/prctl.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include "strutils.h"

#define DEFAULT_CHILD_SLEEP_SEC 2
#define DEFAULT_FORK_DELAY_SEC 1

static void __attribute__((__noreturn__)) usage(void)
{
	printf("%s [options] <sleep time>\n\n", program_invocation_short_name);
	fputs("Options:\n", stdout);
	fputs(" -d, --delay <sec>  delay (in seconds) before forking\n", stdout);
	fputs(" -h, --help         show this usage information\n", stdout);

	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int c, status, rc = 0;
	uint16_t sleep_time = DEFAULT_CHILD_SLEEP_SEC;
	uint16_t fork_delay = DEFAULT_FORK_DELAY_SEC;
	pid_t pid = 0;

	static const struct option longopts[] = {
		{ "delay",       1, NULL, 'd' },
		{ "help",       0, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};

	while((c = getopt_long(argc, argv, "hd:", longopts, NULL)) != -1) {
		switch(c) {
		case 'd':
			fork_delay = strtou16_or_err(optarg, "invalid --delay argument");
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
		sleep_time = strtou16_or_err(argv[optind], "invalid <sleep time> argument");


	printf("Parent PID: %d\n", getpid());
	fflush(stdout);

	sleep(fork_delay);

	errno = 0;
	pid = fork();
	switch (pid) {
	case -1:
		err(EXIT_FAILURE, "failed to create child process");
	case 0:
		errno = 0;
		rc = prctl(PR_SET_PDEATHSIG, SIGTERM);
		if (rc < 0)
			err(EXIT_FAILURE, "prctl() failed");

		sleep(sleep_time);
		return EXIT_SUCCESS;
	default:
		printf("Child PID: %d\n", pid);
		fflush(stdout);

		errno = 0;
		rc = waitpid(pid, &status, 0);
		if (rc < 0 && errno != ECHILD) {
			err(EXIT_FAILURE, "waitpid() failed for child process %d", pid);
		}
	}
	return EXIT_SUCCESS;
}
