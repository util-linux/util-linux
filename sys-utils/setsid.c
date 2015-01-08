/*
 * setsid.c -- execute a command in a new session
 * Rick Sladkey <jrs@world.std.com>
 * In the public domain.
 *
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 * 2001-01-18 John Fremlin <vii@penguinpowered.com>
 * - fork in case we are process group leader
 *
 * 2008-08-20 Daniel Kahn Gillmor <dkg@fifthhorseman.net>
 * - if forked, wait on child process and emit its return code.
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "c.h"
#include "nls.h"
#include "closestream.h"

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(
		" %s [options] <program> [arguments ...]\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Run a program in a new session.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -c, --ctty     set the controlling terminal to the current one\n"), out);
	fputs(_(" -w, --wait     wait program to exit, and use the same return\n"), out);

	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, USAGE_MAN_TAIL("setsid(1)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int ch;
	int ctty = 0;
	pid_t pid;
	int status = 0;

	static const struct option longopts[] = {
		{"ctty", no_argument, NULL, 'c'},
		{"wait", no_argument, NULL, 'w'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((ch = getopt_long(argc, argv, "+Vhcw", longopts, NULL)) != -1)
		switch (ch) {
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'c':
			ctty=1;
			break;
		case 'w':
			status = 1;
			break;
		case 'h':
			usage(stdout);
		default:
			usage(stderr);
		}

	if (argc < 2)
		usage(stderr);

	if (getpgrp() == getpid()) {
		pid = fork();
		switch (pid) {
		case -1:
			err(EXIT_FAILURE, _("fork"));
		case 0:
			/* child */
			break;
		default:
			/* parent */
			if (!status)
				return EXIT_SUCCESS;
			if (wait(&status) != pid)
				err(EXIT_FAILURE, "wait");
			if (WIFEXITED(status))
				return WEXITSTATUS(status);
			err(status, _("child %d did not exit normally"), pid);
		}
	}
	if (setsid() < 0)
		/* cannot happen */
		err(EXIT_FAILURE, _("setsid failed"));

	if (ctty) {
		if (ioctl(STDIN_FILENO, TIOCSCTTY, 1))
			err(EXIT_FAILURE, _("failed to set the controlling terminal"));
	}
	execvp(argv[optind], argv + optind);
	err(EXIT_FAILURE, _("failed to execute %s"), argv[optind]);
}
