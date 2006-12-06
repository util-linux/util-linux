/* very primitive wrapper around the `delete partition' ioctl */
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#ifdef HAVE_LINUX_COMPILER_H
#include <linux/compiler.h>
#endif
#include <linux/blkpg.h>

int
main(int argc, char **argv){
	int fd;
	struct blkpg_ioctl_arg a;
	struct blkpg_partition p;

	if (argc != 3) {
		fprintf(stderr,
			"usage: %s diskdevice partitionnr\n",
			argv[0]);
		exit(1);
	}
	if ((fd = open(argv[1], O_RDONLY)) < 0) {
		perror(argv[1]);
		exit(1);
	}
	p.pno = atoi(argv[2]);
	p.start = 0;
	p.length = 0;
	p.devname[0] = 0;
	p.volname[0] = 0;
	a.op = BLKPG_DEL_PARTITION;
	a.flags = 0;
	a.datalen = sizeof(p);
	a.data = &p;

	if (ioctl(fd, BLKPG, &a) == -1) {
		perror("BLKPG");
		exit(1);
	}
	    
	return 0;
}
