/* Please use this file as a template when introducing new command to
 * util-linux package.
 * -- remove above */
/*
 * fixme-command-name - purpose of it
 *
 * Copyright (c) 20nn  Example Commercial, Inc
 * Written by Your Name <you@example.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <getopt.h>
#include <stdio.h>
#include <unistd.h>

#include "c.h"
#include "closestream.h"
#include "nls.h"

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] file...\n"), program_invocation_short_name);
	fputs(USAGE_OPTIONS, out);
	fputs(_(" -n, --no-argument       option does not use argument\n"), out);
	fputs(_(" -o, --optional[=<arg>]  option argument is optional\n"), out);
	fputs(_(" -r, --required <arg>    option requires an argument\n"), out);
	fputs(_(" -z                      no long option\n"), out);
	fputs(_("     --xyzzy             a long option only\n"), out);
	fputs(_(" -e, --extremely-long-long-option\n"), out);
	fputs(_("                         use next line for description when needed\n"), out);
	fputs(_(" -l, --long-explanation  an example of very verbose, and chatty option\n"), out);
	fputs(_("                           description on two, or multiple lines, where the\n"), out);
	fputs(_("                           consecutive lines are intended by two spaces\n"), out);
	fputs(_(" -f, --foobar            next option description resets indent\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("fixme-command-name(1)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int c;

	enum {
		OPT_XYZZY = CHAR_MAX + 1
	};
	static const struct option longopts[] = {
		{"no-argument", no_argument, NULL, 'n'},
		{"optional", optional_argument, NULL, 'o'},
		{"required", required_argument, NULL, 'r'},
		{"xyzzy", no_argument, NULL, OPT_XYZZY},
		{"extremely-long-long-option", no_argument, NULL, 'e'},
		{"long-explanation", no_argument, NULL, 'l'},
		{"foobar", no_argument, NULL, 'f'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "no::r:elfVh", longopts, NULL)) != -1)
		switch (c) {
		case 'n':
		case 'o':
		case 'r':
		case OPT_XYZZY:
		case 'e':
		case 'l':
		case 'f':
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout);
		default:
			usage(stderr);
		}

	return EXIT_SUCCESS;
}
