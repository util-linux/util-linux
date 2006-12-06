/* fdformat.c  -  Low-level formats a floppy disk - Werner Almesberger */

/* 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 * 1999-03-20 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 & - more i18n/nls translatable strings marked
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fd.h>

#include "nls.h"

struct floppy_struct param;

#define SECTOR_SIZE 512
#define PERROR(msg) { perror(msg); exit(1); }

static void format_disk(int ctrl, char *name)
{
    struct format_descr descr;
    int track;

    printf(_("Formatting ... "));
    fflush(stdout);
    if (ioctl(ctrl,FDFMTBEG,NULL) < 0) PERROR("\nioctl(FDFMTBEG)");
    for (track = 0; track < param.track; track++) {
	descr.track = track;
	descr.head = 0;
	if (ioctl(ctrl,FDFMTTRK,(long) &descr) < 0)
	  PERROR("\nioctl(FDFMTTRK)");

	printf("%3d\b\b\b",track);
	fflush(stdout);
	if (param.head == 2) {
	    descr.head = 1;
	    if (ioctl(ctrl,FDFMTTRK,(long) &descr) < 0)
	      PERROR("\nioctl(FDFMTTRK)");
	}
    }
    if (ioctl(ctrl,FDFMTEND,NULL) < 0) PERROR("\nioctl(FDFMTEND)");
    printf(_("done\n"));
}


static void verify_disk(char *name)
{
    unsigned char *data;
    int fd,cyl_size,cyl,count;

    cyl_size = param.sect*param.head*512;
    if ((data = (unsigned char *) malloc(cyl_size)) == NULL) PERROR("malloc");
    printf(_("Verifying ... "));
    fflush(stdout);
    if ((fd = open(name,O_RDONLY)) < 0) PERROR(name);
    for (cyl = 0; cyl < param.track; cyl++) {
	int read_bytes;

	printf("%3d\b\b\b",cyl);
	fflush(stdout);
	read_bytes = read(fd,data,cyl_size);
	if(read_bytes != cyl_size) {
	    if(read_bytes < 0)
		    perror(_("Read: "));
	    fprintf(stderr,
		    _("Problem reading cylinder %d, expected %d, read %d\n"),
		    cyl, cyl_size, read_bytes);
	    exit(1);
	}
	for (count = 0; count < cyl_size; count++)
	    if (data[count] != FD_FILL_BYTE) {
		printf(_("bad data in cyl %d\nContinuing ... "),cyl);
		fflush(stdout);
		break;
	    }
    }
    printf(_("done\n"));
    if (close(fd) < 0) PERROR("close");
}


static void usage(char *name)
{
    char *this;

    if ((this = strrchr(name,'/')) != NULL) name = this+1;
    fprintf(stderr,_("usage: %s [ -n ] device\n"),name);
    exit(1);
}


int main(int argc,char **argv)
{
    int ctrl;
    int verify;
    struct stat st;
    char *progname, *p;

    progname = argv[0];
    if ((p = strrchr(progname, '/')) != NULL)
	    progname = p+1;

    setlocale(LC_ALL, "");
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);

    if (argc == 2 &&
	(!strcmp(argv[1], "-V") || !strcmp(argv[1], "--version"))) {
	    printf(_("%s from %s\n"), progname, util_linux_version);
	    exit(0);
    }

    verify = 1;
    if (argc > 1 && argv[1][0] == '-') {
	if (argv[1][1] != 'n') usage(progname);
	verify = 0;
	argc--;
	argv++;
    }
    if (argc != 2) usage(progname);
    if (stat(argv[1],&st) < 0) PERROR(argv[1]);
    if (!S_ISBLK(st.st_mode)) {
	fprintf(stderr,_("%s: not a block device\n"),argv[1]);
	exit(1);
	/* do not test major - perhaps this was an USB floppy */
    }
    if (access(argv[1],W_OK) < 0) PERROR(argv[1]);

    ctrl = open(argv[1],O_WRONLY);
    if (ctrl < 0)
	    PERROR(argv[1]);
    if (ioctl(ctrl,FDGETPRM,(long) &param) < 0) 
	    PERROR(_("Could not determine current format type"));
    printf(_("%s-sided, %d tracks, %d sec/track. Total capacity %d kB.\n"),
	   (param.head == 2) ? _("Double") : _("Single"),
	   param.track, param.sect,param.size >> 1);
    format_disk(ctrl, argv[1]);
    close(ctrl);

    if (verify)
	    verify_disk(argv[1]);
    return 0;
}
