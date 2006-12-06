/*
    flock - acquires a file lock and executes a command with the lock held.
    Usage: flock [--shared | --timeout=seconds] lockfile program [args...]

    Written by Adam J. Richter
    Copyright (C) 2004 Yggdrasil Computing, Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <getopt.h>
#include <unistd.h>
#include <stdlib.h>		/* exit */
#include <signal.h>		/* kill */
#include <stdio.h>
#include "nls.h"

static int non_blocking = 0;
static int shared = LOCK_EX;

static const struct option options[] = {
	{"shared",	no_argument,		&shared,	LOCK_SH },
	{"timeout",	required_argument,	NULL,		't' },
	{NULL,		0,			NULL,		0  },
};

int main(int argc, char **argv)
{
	int fd;
	int opt;
	int pid;
	int child_status;
	int option_index;
	int timeout = 0;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	do {
		opt = getopt_long(argc, argv, "+", options, &option_index);
		switch(opt) {
		case '?':
			fprintf (stderr,
				 _("flock: unknown option, aborting.\n"));
			exit(1);
			break;
		case 't':
			timeout = atoi(optarg);
			if (timeout == 0)
				non_blocking |= LOCK_NB;
			break;
		default:
			break;
		}
	} while (opt != -1);

	argc -= optind;
	argv += optind;

	if (argc < 2) {
		fprintf(stderr,
			_("Usage flock [--shared | --timeout=seconds] "
			  "filename command {arg arg...}\n"));
		exit(2);
	}

	fd = open(argv[0], O_RDONLY);
	if (fd < 0) {
		perror(argv[0]);
		exit(3);
	}

	alarm(timeout);
	if (flock(fd, shared | non_blocking) != 0) {
		perror("flock");
		exit(4);
	}
	alarm(0);

	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(5);
	}
	if (pid == 0) {
		execvp(argv[1], argv+1);
		perror(argv[1]);
		exit(6);
	}
	waitpid(pid, &child_status, 0);

	/* flock(fd, LOCK_UN); */
	/* No need to explicitly release the flock, since we are just
	   going to exit now anyhow. */

	/* Lame attempt to simulate child's mode of death. */
	if (WIFSIGNALED(child_status))
		kill(0, WTERMSIG(child_status));

	return WEXITSTATUS(child_status);
}
