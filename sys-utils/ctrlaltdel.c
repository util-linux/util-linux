/*
 * ctrlaltdel.c - Set the function of the Ctrl-Alt-Del combination
 * Created 4-Jul-92 by Peter Orbaek <poe@daimi.aau.dk>
 * ftp://ftp.daimi.aau.dk/pub/linux/poe/
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>

int reboot(int magic, int magictoo, int flag);

int
main(int argc, char *argv[]) {

	if(geteuid()) {
		fprintf(stderr, "You must be root to set the Ctrl-Alt-Del behaviour.\n");
		exit(1);
	}

	if(argc == 2 && !strcmp("hard", argv[1])) {
		if(reboot(0xfee1dead, 672274793, 0x89abcdef) < 0) {
			perror("ctrlaltdel: reboot");
			exit(1);
		}
	} else if(argc == 2 && !strcmp("soft", argv[1])) {
		if(reboot(0xfee1dead, 672274793, 0) < 0) {
			perror("ctrlaltdel: reboot");
			exit(1);
		}
	} else {
		fprintf(stderr, "Usage: ctrlaltdel hard|soft\n");
		exit(1);
	}
	exit(0);
}


