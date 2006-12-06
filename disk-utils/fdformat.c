/* fdformat.c  -  Low-level formats a floppy disk. */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <linux/fd.h>
#include <linux/fs.h>

static int ctrl;
struct floppy_struct param;

#define FLOPPY_MAJOR 2
#define SECTOR_SIZE 512
#define PERROR(msg) { perror(msg); exit(1); }

static void format_disk(char *name)
{
    struct format_descr descr;
    int track;

    printf("Formatting ... ");
    fflush(stdout);
    if (ioctl(ctrl,FDFMTBEG,NULL) < 0) PERROR("\nioctl(FDFMTBEG)");
    for (track = 0; track < param.track; track++) {
	descr.track = track;
	descr.head = 0;
	if (ioctl(ctrl,FDFMTTRK,(int) &descr) < 0) PERROR("\nioctl(FDFMTTRK)");
	printf("%3d\b\b\b",track);
	fflush(stdout);
	if (param.head == 2) {
	    descr.head = 1;
	    if (ioctl(ctrl,FDFMTTRK,(int) &descr) < 0)
		PERROR("\nioctl(FDFMTTRK)");
	}
    }
    if (ioctl(ctrl,FDFMTEND,NULL) < 0) PERROR("\nioctl(FDFMTEND)");
    printf("done\n");
}


static void verify_disk(char *name)
{
    unsigned char *data;
    int fd,cyl_size,cyl,count;

    cyl_size = param.sect*param.head*512;
    if ((data = (unsigned char *) malloc(cyl_size)) == NULL) PERROR("malloc");
    printf("Verifying ... ");
    fflush(stdout);
    if ((fd = open(name,O_RDONLY)) < 0) PERROR(name);
    for (cyl = 0; cyl < param.track; cyl++) {
	printf("%3d\b\b\b",cyl);
	fflush(stdout);
	if (read(fd,data,cyl_size) != cyl_size) PERROR("read");
	for (count = 0; count < cyl_size; count++)
	    if (data[count] != FD_FILL_BYTE) {
		printf("bad data in cyl %d\nContinuing ... ",cyl);
		fflush(stdout);
		break;
	    }
    }
    printf("done\n");
    if (close(fd) < 0) PERROR("close");
}


static void usage(char *name)
{
    char *this;

    if (this = strrchr(name,'/')) name = this+1;
    fprintf(stderr,"usage: %s [ -n ] device\n",name);
    exit(1);
}


int main(int argc,char **argv)
{
    int verify;
    char *name;
    struct stat st;

    name = argv[0];
    verify = 1;
    if (argc > 1 && argv[1][0] == '-') {
	if (argv[1][1] != 'n') usage(name);
	verify = 0;
	argc--;
	argv++;
    }
    if (argc != 2) usage(name);
    if (stat(argv[1],&st) < 0) PERROR(argv[1]);
    if (!S_ISBLK(st.st_mode) || MAJOR(st.st_rdev) != FLOPPY_MAJOR) {
	fprintf(stderr,"%s: not a floppy device\n",argv[1]);
	exit(1);
    }
    if (access(argv[1],W_OK) < 0) PERROR(argv[1]);
    if ((ctrl = open(argv[1],3)) < 0) PERROR(argv[1]);
    if (ioctl(ctrl,FDGETPRM,(int) &param) < 0) PERROR("ioctl(FDGETPRM)");
    printf("%sle-sided, %d tracks, %d sec/track. Total capacity %d kB.\n",
      param.head ? "Doub" : "Sing",param.track,param.sect,param.size >> 1);
    format_disk(argv[1]);
    if (verify) verify_disk(argv[1]);
    return 0;
}
