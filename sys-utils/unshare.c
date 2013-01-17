/*
 * unshare(1) - command-line interface for unshare(2)
 *
 * Copyright (C) 2009 Mikhail Gusarov <dottedmag@dottedmag.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <errno.h>
#include <getopt.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "nls.h"
#include "c.h"
#include "closestream.h"
#include "namespace.h"

static void usage(int status)
{
	FILE *out = status == EXIT_SUCCESS ? stdout : stderr;

	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] <program> [args...]\n"),	program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -m, --mount       unshare mounts namespace\n"
		" -u, --uts         unshare UTS namespace (hostname etc)\n"
		" -i, --ipc         unshare System V IPC namespace\n"
		" -n, --net         unshare network namespace\n"
		" -p, --pid         unshare pid namespace\n"
		" -U, --user        unshare user namespace\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("unshare(1)"));

	exit(status);
}

int main(int argc, char *argv[])
{
	static const struct option longopts[] = {
		{ "help", no_argument, 0, 'h' },
		{ "version", no_argument, 0, 'V'},
		{ "mount", no_argument, 0, 'm' },
		{ "uts", no_argument, 0, 'u' },
		{ "ipc", no_argument, 0, 'i' },
		{ "net", no_argument, 0, 'n' },
		{ "pid", no_argument, 0, 'p' },
		{ "user", no_argument, 0, 'U' },
		{ NULL, 0, 0, 0 }
	};

	int unshare_flags = 0;

	int c;

	setlocale(LC_MESSAGES, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while((c = getopt_long(argc, argv, "hVmuinpU", longopts, NULL)) != -1) {
		switch(c) {
		case 'h':
			usage(EXIT_SUCCESS);
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'm':
			unshare_flags |= CLONE_NEWNS;
			break;
		case 'u':
			unshare_flags |= CLONE_NEWUTS;
			break;
		case 'i':
			unshare_flags |= CLONE_NEWIPC;
			break;
		case 'n':
			unshare_flags |= CLONE_NEWNET;
			break;
		case 'p':
			unshare_flags |= CLONE_NEWPID;
			break;
		case 'U':
			unshare_flags |= CLONE_NEWUSER;
			break;
		default:
			usage(EXIT_FAILURE);
		}
	}

	if(optind >= argc)
		usage(EXIT_FAILURE);

	if(-1 == unshare(unshare_flags))
		err(EXIT_FAILURE, _("unshare failed"));

	execvp(argv[optind], argv + optind);

	err(EXIT_FAILURE, _("exec %s failed"), argv[optind]);
}
