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
1999-02-22 Arkadiusz Mi¶kiewicz <misiek@misiek.eu.org>
- added Native Language Support

-------------------------------------------------------------------------

*/

#include <stdio.h>
#include "nls.h"

/* rdev.c  -  query/set root device. */

static void
usage(void) {

    puts(_("usage: rdev [ -rsv ] [ -o OFFSET ] [ IMAGE [ VALUE [ OFFSET ] ] ]"));
    puts(_("  rdev /dev/fd0  (or rdev /linux, etc.) displays the current ROOT device"));
    puts(_("  rdev /dev/fd0 /dev/hda2         sets ROOT to /dev/hda2"));
    puts(_("  rdev -R /dev/fd0 1              set the ROOTFLAGS (readonly status)"));
    puts(_("  rdev -s /dev/fd0 /dev/hda2      set the SWAP device"));
    puts(_("  rdev -r /dev/fd0 627            set the RAMDISK size"));
    puts(_("  rdev -v /dev/fd0 1              set the bootup VIDEOMODE"));
    puts(_("  rdev -o N ...                   use the byte offset N"));
    puts(_("  rootflags ...                   same as rdev -R"));
    puts(_("  swapdev ...                     same as rdev -s"));
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


static void die(char *msg)
{
    perror(msg);
    exit(1);
}


static char *find_dev(int number)
{
    DIR *dp;
    struct dirent *dir;
    static char name[PATH_MAX+1];
    struct stat s;

    if (!number) return "Boot device";
    if ((dp = opendir("/dev")) == NULL) die("opendir /dev");
    strcpy(name,"/dev/");
    while ((dir = readdir(dp)) != NULL) {
	strcpy(name+5,dir->d_name);
	if (stat(name,&s) < 0)
	    continue;
	if ((s.st_mode & S_IFMT) == S_IFBLK && s.st_rdev == number)
	    return name;
    }
    sprintf(name,"0x%04x",number);
    return name;
}

/* enum { RDEV, SDEV, RAMSIZE, VIDMODE }; */
enum { RDEV, VIDMODE, RAMSIZE, SDEV, __syssize__, ROOTFLAGS };
char *cmdnames[6] = { "rdev", "vidmode",  "ramsize", "swapdev", 
		      "", "rootflags"};
char *desc[6] = { "Root device", "Video mode",  "Ramsize",  "Swap device",
		  "", "Root flags"};
#define shift(n) argv+=n,argc-=n

int main(int argc,char **argv)
{
    int image,offset,dev_nr, i, newoffset=-1;
    char *device, *ptr;
    struct stat s;
    int cmd = 0;

    setlocale(LC_ALL, "");
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);

    device = NULL;
    if ((ptr = strrchr(argv[0],'/')) != NULL)
    	ptr++;
    else
    	ptr = argv[0];
    for (i=0; i<=5; i++)
    	if (!strcmp(ptr,cmdnames[i]))
	    break;
    cmd = i;
    if (cmd>5)
    	cmd=RDEV;
    offset = DEFAULT_OFFSET-cmd*2;

    while (argc > 1)
    { 
    	if (argv[1][0] != '-')
   	    break;
	else
	    switch (argv[1][1])
	    {
		case 'R':
			cmd=ROOTFLAGS;
		        offset = DEFAULT_OFFSET-cmd*2;
	    		shift(1);
			break;
	    	case 'r': 
	    		cmd=RAMSIZE;
		        offset = DEFAULT_OFFSET-cmd*2;
	    		shift(1);
			break;
		case 'v':
	    		cmd=VIDMODE;
		        offset = DEFAULT_OFFSET-cmd*2;
	    		shift(1);
			break;
		case 's':
	    		cmd=SDEV;
		        offset = DEFAULT_OFFSET-cmd*2;
	    		shift(1);
			break;
		case 'o':
			if (argv[1][2])
			{
				newoffset=atoi(argv[1]+2);
				shift(1);
				break;
			} else if (argc > 2) {
				newoffset=atoi(argv[2]);
				shift(2);
				break;
			}
			/* Fall through. . . */
		default:
			usage();
	     }
    }
    if (newoffset >= 0)
	offset = newoffset;

    if  ((cmd==RDEV) && (argc == 1 || argc > 4)) {
	if (stat("/",&s) < 0) die("/");
	printf("%s /\n",find_dev(s.st_dev));
	exit(0);
    } else if ((cmd != RDEV) && (argc == 1 || argc > 4)) usage();

    if ((cmd==RDEV) || (cmd==SDEV))
    {	
	    if (argc == 4) {
		device = argv[2];
		offset = atoi(argv[3]);
	    }
	    else {
		if (argc == 3) {
		    if (isdigit(*argv[2])) offset = atoi(argv[2]);
		    else device = argv[2];
		}
	    }
    }
    else
    {
    	if (argc>=3)
		device = argv[2];
    	if (argc==4)
		offset = atoi(argv[3]);
    }
    if (device) {
    	if ((cmd==SDEV) || (cmd==RDEV))
	{	if (stat(device,&s) < 0) die(device);
	} else
		s.st_rdev=atoi(device);
	if ((image = open(argv[1],O_WRONLY)) < 0) die(argv[1]);
	if (lseek(image,offset,0) < 0) die("lseek");
	if (write(image,(char *)&s.st_rdev,2) != 2) die(argv[1]);
	if (close(image) < 0) die("close");
    }
    else {
	if ((image = open(argv[1],O_RDONLY)) < 0) die(argv[1]);
	if (lseek(image,offset,0) < 0) die("lseek");
	dev_nr = 0;
	if (read(image,(char *)&dev_nr,2) != 2) die(argv[1]);
	if (close(image) < 0) die("close");
	printf(desc[cmd]);
	if ((cmd==SDEV) || (cmd==RDEV))
		printf(" %s\n", find_dev(dev_nr));
	else
		printf(" %d\n", dev_nr);
    }
    return 0;
}


