/* makehole.c - original by HJ Lu */

/* Patched by faith@cs.unc.edu, Wed Oct 6 18:01:39 1993 based on
   information from Michael Bischoff <mbi@mo.math.nat.tu-bs.de> (Fri, 18
   Jun 93 10:10:19 +0200).  */

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <a.out.h>

#define	BUFSIZE	1024
#undef DEBUG

void usage(char *name, char *message)
{
    if (message)
	fprintf(stderr, "%s: %s\n", name, message);

    if (errno)
	perror(name);

    fprintf(stderr, "Usage:%s Imagefile\n", name);
    exit(1);
}

int ishole(char *buf, int size)
{
    int i;

    for (i = 0; i < size; i++)
	if (buf[i])
	    return 0;

    return 1;
}

void main(int argc, char *argv[])
{
    char buf[BUFSIZE];
    char tmp_file[64];
    int fdin, fdout;
    int ret;
    int abs_offset;
    int hole;
    struct exec *header = (struct exec *) buf;

#ifndef DEBUG
    if (geteuid()) {
	fprintf(stderr, "%s: must be root to run!\n", *argv);
	exit(1);
    }
#endif

    switch (argc) {
    case 2:
	break;
    default:
	usage(*argv, NULL);
    }

    errno = 0;

    sprintf( tmp_file, "hole%d", getpid() );
    if (tmp_file == NULL) {
	usage(*argv, "Unable to get a temporary image filename!");
    }
#ifdef DEBUG
    else {
	fprintf(stderr, "Temparory image file: %s\n", tmp_file);
    }
#endif

    errno = 0;
    fdin = open(argv[1], O_RDONLY);
    if (fdin == -1) {
	usage(*argv, "unable to open file.");
    }
    fprintf(stderr, "Making holes in %s...\n", argv[1]);

    errno = 0;

    if ((ret = read(fdin, header, BUFSIZE)) != BUFSIZE
	|| N_MAGIC(*header) != ZMAGIC) {
	usage(*argv, "file must be pure executable.");
    }

    fdout = creat(tmp_file, 0555);
    if (fdout == -1) {
	perror("Unable to create the temparory image file!");
	exit(1);
    }
    if (write(fdout, header, ret) != ret) {
	perror("Fail to write header to the temparory image file!");
	unlink(tmp_file);
	exit(1);
    }
    abs_offset = ret;
    hole = 0;
    while ((ret = read(fdin, buf, BUFSIZE)) > 0) {
	abs_offset += ret;
	if (ishole(buf, ret)) {
#ifdef DEBUG
	    fprintf(stderr, "There is a %d byte hole from 0x%x to 0x%x.\n", ret, abs_offset - ret, abs_offset);
#endif
	    hole += ret;
	    if (lseek(fdout, abs_offset, SEEK_SET) != abs_offset) {
		perror("Fail to make a hole in the temparory image file!");
		unlink(tmp_file);
		exit(1);
	    }
	} else {
#ifdef DEBUG
	    fprintf(stderr, "Writing %d bytes from 0x%x to 0x%x.\n", ret, abs_offset - ret, abs_offset);
#endif
	    if (write(fdout, buf, ret) != ret) {
		perror("Fail to write the temparory image file!");
		unlink(tmp_file);
		exit(1);
	    }
	}
    }

    if (ftruncate(fdout, abs_offset)) {
	perror("Fail to truncate the temparory image file!");
	unlink(tmp_file);
	exit(1);
    }
    close(fdout);
    close(fdin);

    if (rename(tmp_file, argv[1])) {
	perror("Fail to rename the temparory image file to the old image file!");
	unlink(tmp_file);
	exit(1);
    }
    fprintf(stderr, "There are %d byte holes out of %d bytes in `%s'.\n", hole, abs_offset, argv[1]);
}
