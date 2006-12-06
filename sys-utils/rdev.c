/*

  rdev.c  -  query/set root device.

-------------------------------------------------------------------------

Date: Sun, 27 Dec 1992 15:55:31 +0000
Subject: Re: rdev
From: almesber@nessie.cs.id.ethz.ch (Werner Almesberger)
To: Rik Faith <faith@cs.unc.edu>

There are quite a few versions of rdev:

  - the original rootdev that only printed the current root device, by
    Linus.
  - rdev that does what rootdev did and that also allows you to change
    the root (and swap) device, by me.
  - rdev got renamed to setroot and I think even to rootdev on various
    distributions.
  - Peter MacDonald added video mode and RAM disk setting and included
    this version on SLS, called rdev again. I've attached his rdev.c to
    this mail.
    
-------------------------------------------------------------------------
    
Date: 11 Mar 92 21:37:37 GMT
Subject: rdev - query/set root device
From: almesber@nessie.cs.id.ethz.ch (Werner Almesberger)
Organization: Swiss Federal Institute of Technology (ETH), Zurich, CH

With all that socket, X11, disk driver and FS hacking going on, apparently
nobody has found time to address one of the minor nuisances of life: set-
ting the root FS device is still somewhat cumbersome. I've written a little
utility which can read and set the root device in boot images:

rdev accepts an optional offset argument, just in case the address should
ever move from 508. If called without arguments, rdev outputs an mtab line
for the current root FS, just like /etc/rootdev does.

ramsize sets the size of the ramdisk.  If size is zero, no ramdisk is used.

vidmode sets the default video mode at bootup time.  -1 uses default video
mode, -2 uses menu.

-------------------------------------------------------------------------

Sun Dec 27 10:42:16 1992: Minor usage changes, faith@cs.unc.edu.
Tue Mar 30 09:31:52 1993: rdev -Rn to set root readonly flag, sct@dcs.ed.ac.uk
Wed Jun 22 21:12:29 1994: Applied patches from Dave
                          (gentzel@nova.enet.dec.com) to prevent dereferencing
			  the NULL pointer, faith@cs.unc.edu
1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
- added Native Language Support

-------------------------------------------------------------------------

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nls.h"

/* rdev.c  -  query/set root device. */

static void
usage(void) {

    puts(_("usage: rdev [ -rv ] [ -o OFFSET ] [ IMAGE [ VALUE [ OFFSET ] ] ]"));
    puts(_("  rdev /dev/fd0  (or rdev /linux, etc.) displays the current ROOT device"));
    puts(_("  rdev /dev/fd0 /dev/hda2         sets ROOT to /dev/hda2"));
    puts(_("  rdev -R /dev/fd0 1              set the ROOTFLAGS (readonly status)"));
    puts(_("  rdev -r /dev/fd0 627            set the RAMDISK size"));
    puts(_("  rdev -v /dev/fd0 1              set the bootup VIDEOMODE"));
    puts(_("  rdev -o N ...                   use the byte offset N"));
    puts(_("  rootflags ...                   same as rdev -R"));
    puts(_("  ramsize ...                     same as rdev -r"));
    puts(_("  vidmode ...                     same as rdev -v"));
    puts(_("Note: video modes are: -3=Ask, -2=Extended, -1=NormalVga, 1=key1, 2=key2,..."));
    puts(_("      use -R 1 to mount root readonly, -R 0 for read/write."));
    exit(-1);
}

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#define DEFAULT_OFFSET 508


static void
die(char *msg) {
	perror(msg);
	exit(1);
}

/* Earlier rdev fails on /dev/ida/c0d0p1 so we allow for
   recursion in /dev. -- Paul Clements */
/* In fact devfs needs deep recursion. */

static int
find_dev_recursive(char *dirnamebuf, int number) {
	DIR *dp;
	struct dirent *dir;
	struct stat s;
	int dirnamelen = 0;

	if ((dp = opendir(dirnamebuf)) == NULL)
		die("opendir");
	dirnamelen = strlen(dirnamebuf);
	while ((dir = readdir(dp)) != NULL) {
		if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
			continue;
		if (dirnamelen + 1 + strlen(dir->d_name) > PATH_MAX)
			continue;
		dirnamebuf[dirnamelen] = '/';
		strcpy(dirnamebuf+dirnamelen+1, dir->d_name);
		if (lstat(dirnamebuf, &s) < 0)
			continue;
		if ((s.st_mode & S_IFMT) == S_IFBLK && s.st_rdev == number)
			return 1;
		if ((s.st_mode & S_IFMT) == S_IFDIR &&
		    find_dev_recursive(dirnamebuf, number))
			return 1;
	}
	dirnamebuf[dirnamelen] = 0;
	closedir(dp);
	return 0;
}

static char *
find_dev(int number) {
	static char name[PATH_MAX+1];

	if (!number)
		return "Boot device";
	strcpy(name, "/dev");
	if (find_dev_recursive(name, number))
		return name;
	sprintf(name, "0x%04x", number);
	return name;
}

/* The enum values are significant, things are stored in this order,
   see bootsect.S */
enum { RDEV, VIDMODE, RAMSIZE, __swapdev__, __syssize__, ROOTFLAGS };
char *cmdnames[6] = { "rdev", "vidmode",  "ramsize", "", 
		      "", "rootflags"};
char *desc[6] = { "Root device", "Video mode",  "Ramsize",  "",
		  "", "Root flags"};
#define shift(n) argv+=n,argc-=n

int
main(int argc, char **argv) {
	int image, offset, dev_nr, i, newoffset=-1;
	char *ptr;
	unsigned short val, have_val;
	struct stat s;
	int cmd;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* use the command name to figure out what we have to do - ugly */
	cmd = RDEV;
	if ((ptr = strrchr(argv[0],'/')) != NULL)
		ptr++;
	else
		ptr = argv[0];
	for (i=0; i<=5; i++) {
		if (!strcmp(ptr,cmdnames[i])) {
			cmd = i;
			break;
		}
	}

	while (argc > 1) { 
		if (argv[1][0] != '-')
			break;
		switch (argv[1][1]) {
		case 'R':
			cmd = ROOTFLAGS;
			shift(1);
			break;
		case 'r': 
			cmd = RAMSIZE;
			shift(1);
			break;
		case 'v':
			cmd = VIDMODE;
			shift(1);
			break;
		case 'o':
			if (argv[1][2]) {
				newoffset = atoi(argv[1]+2);
				shift(1);
				break;
			} else if (argc > 2) {
				newoffset = atoi(argv[2]);
				shift(2);
				break;
			}
			/* Fall through. . . */
		default:
			usage();
		}
	}

	/* Here the only sensible way of using rdev */
	if (argc == 1) {
		if (cmd == RDEV) {
			if (stat("/",&s) < 0) die("/");
			printf("%s /\n", find_dev(s.st_dev));
			exit(0);
		}
		usage();
	}

	if (argc > 4)
		usage();

	/* Ancient garbage.. */
	offset = DEFAULT_OFFSET-cmd*2;
	if (newoffset >= 0)
		offset = newoffset;
	if (argc == 4)
		offset = atoi(argv[3]);

	have_val = 0;

	if (argc >= 3) {
		if (cmd == RDEV) {
			if (isdigit(*argv[2])) {
				/* earlier: specify offset */
				/* now: specify major,minor */
				char *p;
				unsigned int ma,mi;
				if ((p = strchr(argv[2], ',')) == NULL)
					die(_("missing comma"));
				ma = atoi(argv[2]);
				mi = atoi(p+1);
				val = ((ma<<8) | mi);
			} else {
				char *device = argv[2];
				if (stat(device,&s) < 0)
					die(device);
				val = s.st_rdev;
			}
		} else {
			val = atoi(argv[2]);
		}
		have_val = 1;
	}

	if (have_val) {
		if ((image = open(argv[1],O_WRONLY)) < 0) die(argv[1]);
		if (lseek(image,offset,0) < 0) die("lseek");
		if (write(image,(char *)&val,2) != 2) die(argv[1]);
		if (close(image) < 0) die("close");
	} else {
		if ((image = open(argv[1],O_RDONLY)) < 0) die(argv[1]);
		if (lseek(image,offset,0) < 0) die("lseek");
		dev_nr = 0;
		if (read(image,(char *)&dev_nr,2) != 2) die(argv[1]);
		if (close(image) < 0) die("close");
		fputs(desc[cmd], stdout);
		if (cmd == RDEV)
			printf(" %s\n", find_dev(dev_nr));
		else
			printf(" %d\n", dev_nr);
	}
	return 0;
}
