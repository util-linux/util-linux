/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2026 Masatake YAMATO <yamato@redhat.com>
 */

/* test_open_twice takes a file name.
 * It opens the file twice during its execution.
 * After opening the file for the first time, the program prints
 * its PID and the file descriptor, then waits for user input
 * using getchar():
 *
 *   PID FD0
 *
 * While the program is waiting, the user can perform arbitrary
 * operations. For example:
 * - deleting the file, and/or
 * - mounting another filesystem on the file path or its parent directory.
 *
 * After a character is given as input, the program calls open(2)
 * again with the same file name and prints the file descriptor
 * returned by the second open(2) call:
 *
 *   FD1
 *
 * The program waits for user input once more with getchar().
 * After another character is given, the program exits.
 */

#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void __attribute__((__noreturn__)) usage(const char *prog, FILE *fp, int eval)
{
	fputs("Usage :\n", fp);
	fprintf(fp, " %s FILE\n", prog);
	exit(eval);
}

int main(int argc, char **argv)
{
	const char *fname;
	int fd0, fd1;

	int c;
	static const struct option longopts[] = {
		{ "help",	no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};

	while ((c = getopt_long(argc, argv, "h", longopts, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage(argv[0], stdout, EXIT_SUCCESS);
		default:
			usage(argv[0], stderr, EXIT_FAILURE);
		}
	}

	if (optind == argc)
		errx(EXIT_FAILURE, "no file name given");

	if (argc - optind > 1)
		errx(EXIT_FAILURE, "too many arguments");

	fname = argv[optind];

	fd0 = open(fname, O_RDONLY);
	if (fd0 < 0)
		err(1, "error in open \"%s\" in the first time", fname);
	printf("%d %d\n", getpid(), fd0);
	fflush(stdout);

	getchar();
	fd1 = open(fname, O_RDONLY);
	if (fd1 < 0)
		err(1, "error in open \"%s\" in the second time", fname);
	printf("%d\n", fd1);
	fflush(stdout);

	getchar();
	return 0;
}
