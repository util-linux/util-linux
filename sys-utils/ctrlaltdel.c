/*
 * ctrlaltdel.c - Set the function of the Ctrl-Alt-Del combination
 * Created 4-Jul-92 by Peter Orbaek <poe@daimi.aau.dk>
 * ftp://ftp.daimi.aau.dk/pub/linux/poe/
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "linux_reboot.h"
#include "nls.h"

int
main(int argc, char *argv[]) {

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	

	if(geteuid()) {
		fprintf(stderr,
		    _("You must be root to set the Ctrl-Alt-Del behaviour.\n"));
		exit(1);
	}

	if(argc == 2 && !strcmp("hard", argv[1])) {
		if(my_reboot(LINUX_REBOOT_CMD_CAD_ON) < 0) {
			perror("ctrlaltdel: reboot");
			exit(1);
		}
	} else if(argc == 2 && !strcmp("soft", argv[1])) {
		if(my_reboot(LINUX_REBOOT_CMD_CAD_OFF) < 0) {
			perror("ctrlaltdel: reboot");
			exit(1);
		}
	} else {
		fprintf(stderr, _("Usage: ctrlaltdel hard|soft\n"));
		exit(1);
	}
	exit(0);
}


