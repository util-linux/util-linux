/*
 * gen_uuid.c --- generate a DCE-compatible uuid
 *
 * Copyright (C) 1999, Andreas Dilger and Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include "uuid.h"
#include "nls.h"
#include "c.h"
#include "closestream.h"

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Create a new UUID value.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -r, --random     generate random-based uuid\n"
		" -t, --time       generate time-based uuid\n"
		" -V, --version    output version information and exit\n"
		" -h, --help       display this help and exit\n\n"), out);

	fprintf(out, USAGE_MAN_TAIL("uuidgen(1)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int
main (int argc, char *argv[])
{
	int    c;
	int    do_type = 0;
	char   str[37];
	uuid_t uu;

	static const struct option longopts[] = {
		{"random", no_argument, NULL, 'r'},
		{"time", no_argument, NULL, 't'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "rtVh", longopts, NULL)) != -1)
		switch (c) {
		case 't':
			do_type = UUID_TYPE_DCE_TIME;
			break;
		case 'r':
			do_type = UUID_TYPE_DCE_RANDOM;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout);
		default:
			usage(stderr);
		}

	switch (do_type) {
	case UUID_TYPE_DCE_TIME:
		uuid_generate_time(uu);
		break;
	case UUID_TYPE_DCE_RANDOM:
		uuid_generate_random(uu);
		break;
	default:
		uuid_generate(uu);
		break;
	}

	uuid_unparse(uu, str);

	printf("%s\n", str);

	return EXIT_SUCCESS;
}
