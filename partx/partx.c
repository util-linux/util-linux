/*
 * Given a block device and a partition table type,
 * try to parse the partition table, and list the
 * contents. Optionally add or remove partitions.
 *
 * [This is not an fdisk - adding and removing partitions
 * is not a change of the disk, but just telling the kernel
 * about presence and numbering of on-disk partitions.]
 *
 * Call:
 *	partx [-{l|a|d}] [--type TYPE] [--nr M-N] [partition] wholedisk
 * where TYPE is {dos|bsd|solaris|unixware|gpt}.
 *
 * Read wholedisk and add all partitions:
 *	partx -a wholedisk
 *
 * Subdivide a partition into slices (and delete or shrink the partition):
 * [Not easy: one needs the partition number of partition -
 *  that is the last 4 or 6 bits of the minor; it can also be found
 *  in /proc/partitions; but there is no good direct way.]
 *	partx -a partition wholedisk
 *
 * Delete all partitions from wholedisk:
 *	partx -d wholedisk
 *
 * Delete partitions M-N from wholedisk:
 *	partx -d --nr M-N wholedisk
 *
 * aeb, 2000-03-21 -- sah is 42 now
 */

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hdreg.h>        /* HDIO_GETGEO */
#ifdef HAVE_LINUX_COMPILER_H
#include <linux/compiler.h>
#endif
#include <linux/blkpg.h>
#define BLKGETSIZE _IO(0x12,96)    /* return device size */

#include "partx.h"
#include "crc32.h"
static void errmerge(int err, int m, char *msg1, char *msg2);

#define SIZE(a) (sizeof(a)/sizeof((a)[0]))

#define MAXTYPES	64
#define MAXSLICES	256

struct slice slices[MAXSLICES];

enum action { LIST, ADD, DELETE };

struct pt {
	char *type;
	ptreader *fn;
} pts[MAXTYPES];
int ptct;

static void
addpts(char *t, ptreader f)
{
	if (ptct >= MAXTYPES) {
		fprintf(stderr, "addpts: too many types\n");
		exit(1);
	}
	pts[ptct].type = t;
	pts[ptct].fn = f;
	ptct++;
}

static void
initpts(void)
{
	addpts("gpt", read_gpt_pt);
	addpts("dos", read_dos_pt);
	addpts("bsd", read_bsd_pt);
	addpts("solaris", read_solaris_pt);
	addpts("unixware", read_unixware_pt);
}

static char short_opts[] = "ladgvn:t:";
static const struct option long_opts[] = {
	{ "gpt",	no_argument,	        NULL,	'g' },
	{ "type",	required_argument,	NULL,	't' },
	{ "nr",		required_argument,	NULL,	'n' },
	{ NULL, 0, NULL, 0 }
};

/* Used in gpt.c */
int force_gpt=0;

int
main(int argc, char **argv){
        int fd, fd2, c, i, j, k, n;
	long size;
	struct hd_geometry g;
	struct slice all;
        struct blkpg_ioctl_arg a;
        struct blkpg_partition pt;
	struct pt *ptp;
	enum action what = LIST;
	char *p, *type, *diskdevice, *device;
	int lower, upper;
	int verbose = 0;
	int ret = 0;

	initpts();
	init_crc32();

	lower = upper = 0;
	type = device = diskdevice = NULL;

	while ((c = getopt_long (argc, argv, short_opts, long_opts, NULL))
	        != -1) switch(c) {
	case 'l':
		what = LIST; break;
	case 'a':
		what = ADD; break;
	case 'd':
		what = DELETE; break;
	case 'g':
		force_gpt = 1; break;
	case 'n':
		p = optarg;
		lower = atoi(p);
		p = index(p, '-');
		if (p)
			upper = atoi(p+1);
		else
			upper = lower;
		break;
	case 't':
		type = optarg;
		break;
	case 'v':
		verbose = 1;
		break;
	case '?':
	default:
		fprintf(stderr, "unknown option\n");
		exit(1);
	}

	if (optind == argc-2) {
		device = argv[optind];
		diskdevice = argv[optind+1];
	} else if (optind == argc-1) {
		diskdevice = device = argv[optind];
	} else {
		fprintf(stderr, "call: partx -opts [device] wholedisk\n");
		exit(1);
	}

	fd = open(diskdevice, O_RDONLY);
	if (fd == -1) {
		perror(diskdevice);
		exit(1);
	}

	/* remove the indicated partitions from the kernel partition tables */
	if (what == DELETE) {
		if (device != diskdevice) {
			fprintf(stderr,
				"call: partx -d [--nr M-N] wholedisk\n");
			exit(1);
		}

		if (!lower)
			lower = 1;

		while (upper == 0 || lower <= upper) {
			int err;

			pt.pno = lower;
			pt.start = 0;
			pt.length = 0;
			pt.devname[0] = 0;
			pt.volname[0] = 0;
			a.op = BLKPG_DEL_PARTITION;
			a.flags = 0;
			a.datalen = sizeof(pt);
			a.data = &pt;
			if (ioctl(fd, BLKPG, &a) == -1)
			    err = errno;
			else
			    err = 0;
			errmerge(err, lower,
				 "error deleting partition %d: ",
				 "error deleting partitions %d-%d: ");
			/* expected errors:
			   EBUSY: mounted or in use as swap
			   ENXIO: no such nonempty partition
			   EINVAL: not wholedisk, or bad pno
			   EACCES/EPERM: permission denied
			*/
			if (err && err != EBUSY && err != ENXIO) {
				ret = 1;
				break;
			}
			if (err == 0 && verbose)
				printf("deleted partition %d\n", lower);
			lower++;
		}
		errmerge(0, 0,
			 "error deleting partition %d: ",
			 "error deleting partitions %d-%d: ");
		return ret;
	}

	if (device != diskdevice) {
		fd2 = open(device, O_RDONLY);
		if (fd2 == -1) {
			perror(device);
			exit(1);
		}
	} else {
		fd2 = fd;
	}

	if (ioctl(fd, HDIO_GETGEO, &g)) {
		perror("HDIO_GETGEO");
		exit(1);
	}
	if (g.start != 0) {
		fprintf(stderr, "last arg is not the whole disk\n");
		fprintf(stderr, "call: partx -opts device wholedisk\n");
		exit(1);
	}

	if (ioctl(fd2, HDIO_GETGEO, &g)) {
		perror("HDIO_GETGEO");
		exit(1);
	}
	all.start = g.start;

	if(ioctl(fd2, BLKGETSIZE, &size)) {
		perror("BLKGETSIZE");
		exit(1);
	}
	all.size = size;

	if (verbose)
		printf("device %s: start %d size %d\n",
		       device, all.start, all.size);

	if (all.size == 0) {
		fprintf(stderr, "That disk slice has size 0\n");
		exit(0);
	}
	if (all.size == 2)
		all.size = 0;	/* probably extended partition */

	/* add the indicated partitions to the kernel partition tables */
	if (!lower)
		lower = 1;
	for (i = 0; i < ptct; i++) {
		ptp = &pts[i];
		if (!type || !strcmp(type, ptp->type)) {
			n = ptp->fn(fd, all, slices, SIZE(slices));
			if (n >= 0 && verbose)
			    printf("%s: %d slices\n", ptp->type, n);
			if (n > 0 && (verbose || what == LIST)) {
			    for (j=0; j<n; j++)
				printf("#%2d: %9d-%9d (%9d sectors, %6d MB)\n",
				       lower+j,
				       slices[j].start,
				       slices[j].start+slices[j].size-1,
				       slices[j].size,
				       (int)((512 * (long long) slices[j].size)
					/ 1000000));
			}
			if (n > 0 && what == ADD) {
			    /* test for overlap, as in the case of an
			       extended partition, and reduce size */
			    for (j=0; j<n; j++) {
				for (k=j+1; k<n; k++) {
				    if (slices[k].start > slices[j].start &&
					slices[k].start < slices[j].start +
					slices[j].size) {
					    slices[j].size = slices[k].start -
						slices[j].start;
					    if (verbose)
						printf("reduced size of "
						       "partition #%d to %d\n",
						       lower+j,
						       slices[j].size);
				    }
				}
			    }
			    for (j=0; j<n; j++) {
				pt.pno = lower+j;
				pt.start = 512 * (long long) slices[j].start;
				pt.length = 512 * (long long) slices[j].size;
				pt.devname[0] = 0;
				pt.volname[0] = 0;
				a.op = BLKPG_ADD_PARTITION;
				a.flags = 0;
				a.datalen = sizeof(pt);
				a.data = &pt;
				if (ioctl(fd, BLKPG, &a) == -1) {
				    perror("BLKPG");
				    fprintf(stderr,
					    "error adding partition %d\n",
					    lower+j);
				} else if (verbose)
				    printf("added partition %d\n", lower+j);
			    }
			}
		}
	}

	return 0;
}

static void *
xmalloc (size_t size) {
	void *t;

	if (size == 0)
		return NULL;
	t = malloc (size);
	if (t == NULL) {
		fprintf(stderr, "Out of memory\n");
		exit(1);
	}
	return t;
}

static int
sseek(int fd, unsigned int secnr) {
	long long in, out;
	in = ((long long) secnr << 9);
	out = 1;

	if ((out = lseek(fd, in, SEEK_SET)) != in)
	{
		fprintf(stderr, "lseek error\n");
		return -1;
	}
	return 0;
}

static
struct block {
	unsigned int secnr;
	char *block;
	struct block *next;
} *blockhead;

char *
getblock(int fd, unsigned int secnr) {
	struct block *bp;

	for (bp = blockhead; bp; bp = bp->next)
		if (bp->secnr == secnr)
			return bp->block;
	if (sseek(fd, secnr))
		return NULL;
	bp = xmalloc(sizeof(struct block));
	bp->secnr = secnr;
	bp->next = blockhead;
	blockhead = bp;
	bp->block = (char *) xmalloc(1024);
	if (read(fd, bp->block, 1024) != 1024) {
		fprintf(stderr, "read error, sector %d\n", secnr);
		bp->block = NULL;
	}
	return bp->block;
}

/* call with errno and integer m and error message */
/* merge to interval m-n */
static void
errmerge(int err, int m, char *msg1, char *msg2) {
	static int preverr, firstm, prevm;

	if (err != preverr) {
		if (preverr) {
			if (firstm == prevm)
				fprintf(stderr, msg1, firstm);
			else
				fprintf(stderr, msg2, firstm, prevm);
			errno = preverr;
			perror("BLKPG");
		}
		preverr = err;
		firstm = prevm = m;
	} else
		prevm = m;
}
