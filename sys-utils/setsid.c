/*
 * setsid.c -- execute a command in a new session
 * Rick Sladkey <jrs@world.std.com>
 * In the public domain.
 *
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@misiek.eu.org>
 * - added Native Language Support
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "nls.h"

int main(int argc, char *argv[])
{
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	
	if (argc < 2) {
		fprintf(stderr, _("usage: %s program [arg ...]\n"),
			argv[0]);
		exit(1);
	}
	if (setsid() < 0) {
		perror("setsid");
		exit(1);
	}
	execvp(argv[1], argv + 1);
	perror("execvp");
	exit(1);
}
