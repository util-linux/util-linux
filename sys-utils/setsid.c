/*
 * setsid.c -- execute a command in a new session
 * Rick Sladkey <jrs@world.std.com>
 * In the public domain.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s program [arg ...]\n",
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
