/*
 * chroot.c -- change root directory and execute a command there
 * Rick Sladkey <jrs@world.std.com>
 * In the public domain.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s directory program [arg ...]\n",
			argv[0]);
		exit(1);
	}
	if (chroot(argv[1]) < 0) {
		perror("chroot");
		exit(1);
	}
	execvp(argv[2], argv + 2);
	perror("execvp");
	exit(1);
}
