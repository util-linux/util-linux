/*
 * Having the wrong rootdevice listed in mtab is slightly inconvenient.
 * Try to guess what it could be...
 * In case /proc/cmdline exists, and has the format
 *	stuff root=R more stuff...
 * and we think we understand the number R, decode it as a root device.
 *
 * Another possibility:
 * Erik Andersen writes:
 *  I did a similar find_real_root_device_name() in busybox some time back.  
 *  Basically, do a stat("/", &rootstat) then walk /dev stat'ing each file
 *  and if (statbuf.st_rdev == rootstat.st_rdev) then you have a match.
 *  Works fine.
 */
#include <stdio.h>
#include <string.h>
#include "mount_guess_rootdev.h"

#define PROC_CMDLINE	"/proc/cmdline"

static char *
rootdev(char *p) {
	unsigned long devno;
	char *ep;
	char *type = "hd";
	char let;
	int ma, mi;
	char devname[32];

	devno = strtoul(p, &ep, 16);
	if ((ep == p+3 || ep == p+4) && (*ep == ' ' || *ep == 0)) {
		ma = (devno >> 8);
		mi = (devno & 0xff);
		switch(ma) {
		case 8:
			type = "sd";
			let = 'a'+(mi/16);
			mi = mi%16;
			break;
		case 3:
			let = 'a'; break;
		case 0x16:
			let = 'c'; break;
		case 0x21:
			let = 'e'; break;
		case 0x22:
			let = 'g'; break;
		case 0x38:
			let = 'i'; break;
		case 0x39:
			let = 'k'; break;
		default:
			return NULL;
		}
		if (mi & 0x40) {
			mi -= 0x40;
			let++;
		}
		if (mi == 0)
			sprintf(devname, "/dev/%s%c", type, let);
		else
			sprintf(devname, "/dev/%s%c%d", type, let, mi);
		return xstrdup(devname);
	}
	return NULL;
}

char *
mount_guess_rootdev() {
	FILE *cf;
	char line[1024];
	char *p, *ret = NULL;

	cf = fopen(PROC_CMDLINE, "r");
	if (cf) {
		if (fgets(line, sizeof(line), cf)) {
			for (p = line; *p; p++) {
				if (!strncmp(p, " root=", 6)) {
					ret = rootdev(p+6);
					break;
				}
			}
		}
		fclose(cf);
	}
	return ret;
}

#if 0
main(){
	char *p = mount_guess_rootdev();
	if (!p)
		p = "/dev/root";
	printf("%s\n", p);
}
#endif
