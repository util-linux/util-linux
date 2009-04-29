/*
 * blockdev.c --- Do various simple block device ioctls from the command line
 * aeb, 991028
 */

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "nls.h"
#include "blkdev.h"

const char *progname;

struct bdc {
	char *name;
	char *iocname;
	long ioc;
	int argtype;
#define ARGNONE	0
#define ARGINTA	1
#define ARGINTAP 2
#define	ARGINTP	3
#define ARGINTG	4
#define ARGLINTG 5
#define ARGLLINTG 6
#define ARGLU 7
#define ARGLLU 8
	long argval;
	char *argname;
	char *help;
} bdcms[] = {
	{ "--setro", "BLKROSET", BLKROSET, ARGINTP, 1, NULL, N_("set read-only") },
	{ "--setrw", "BLKROSET", BLKROSET, ARGINTP, 0, NULL, N_("set read-write") },
	{ "--getro", "BLKROGET", BLKROGET, ARGINTG, -1, NULL, N_("get read-only") },
	{ "--getss", "BLKSSZGET", BLKSSZGET, ARGINTG, -1, NULL, N_("get sectorsize") },
	{ "--getbsz", "BLKBSZGET", BLKBSZGET, ARGINTG, -1, NULL, N_("get blocksize") },
	{ "--setbsz", "BLKBSZSET", BLKBSZSET, ARGINTAP, 0, "BLOCKSIZE", N_("set blocksize") },
	{ "--getsize", "BLKGETSIZE", BLKGETSIZE, ARGLU, -1, NULL, N_("get 32-bit sector count") },
	{ "--getsize64", "BLKGETSIZE64", BLKGETSIZE64, ARGLLU, -1, NULL, N_("get size in bytes") },
	{ "--setra", "BLKRASET", BLKRASET, ARGINTA, 0, "READAHEAD", N_("set readahead") },
	{ "--getra", "BLKRAGET", BLKRAGET, ARGLINTG, -1, NULL, N_("get readahead") },
	{ "--setfra", "BLKFRASET", BLKFRASET, ARGINTA, 0, "FSREADAHEAD", N_("set filesystem readahead") },
	{ "--getfra", "BLKFRAGET", BLKFRAGET, ARGLINTG, -1, NULL, N_("get filesystem readahead") },
	{ "--flushbufs", "BLKFLSBUF", BLKFLSBUF, ARGNONE, 0, NULL, N_("flush buffers") },
	{ "--rereadpt", "BLKRRPART", BLKRRPART, ARGNONE, 0, NULL,
	  N_("reread partition table") },
};

#define SIZE(a)	(sizeof(a)/sizeof((a)[0]))

static void
usage(void) {
	int i;
	fputc('\n', stderr);
	fprintf(stderr, _("Usage:\n"));
	fprintf(stderr, "  %s -V\n", progname);
	fprintf(stderr, _("  %s --report [devices]\n"), progname);
	fprintf(stderr, _("  %s [-v|-q] commands devices\n"), progname);
	fputc('\n', stderr);

	fprintf(stderr, _("Available commands:\n"));
	fprintf(stderr, "\t%-30s %s\n", "--getsz",
			_("get size in 512-byte sectors"));
	for (i = 0; i < SIZE(bdcms); i++) {
		if (bdcms[i].argname)
			fprintf(stderr, "\t%s %-*s %s\n", bdcms[i].name,
					(int) (29 - strlen(bdcms[i].name)),
					bdcms[i].argname, _(bdcms[i].help));
		else
			fprintf(stderr, "\t%-30s %s\n", bdcms[i].name,
					_(bdcms[i].help));
	}
	fputc('\n', stderr);
	exit(1);
}

static int
find_cmd(char *s) {
	int j;

	for (j = 0; j < SIZE(bdcms); j++)
		if (!strcmp(s, bdcms[j].name))
			return j;
	return -1;
}

void do_commands(int fd, char **argv, int d);
void report_header(void);
void report_device(char *device, int quiet);
void report_all_devices(void);

int
main(int argc, char **argv) {
	int fd, d, j, k;
	char *p;

	/* egcs-2.91.66 is buggy and says:
	   blockdev.c:93: warning: `d' might be used uninitialized */
	d = 0;

	progname = argv[0];
	if ((p = strrchr(progname, '/')) != NULL)
		progname = p+1;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	if (argc < 2)
		usage();

	/* -V not together with commands */
	if (!strcmp(argv[1], "-V") || !strcmp(argv[1], "--version")) {
		printf("%s (%s)\n", progname, PACKAGE_STRING);
		exit(0);
	}

	/* --report not together with other commands */
	if (!strcmp(argv[1], "--report")) {
		report_header();
		if (argc > 2) {
			for (d = 2; d < argc; d++)
				report_device(argv[d], 0);
		} else {
			report_all_devices();
		}
		exit(0);
	}

	/* do each of the commands on each of the devices */
	/* devices start after last command */
	for (d = 1; d < argc; d++) {
		j = find_cmd(argv[d]);
		if (j >= 0) {
			if (bdcms[j].argtype == ARGINTA ||
			    bdcms[j].argtype == ARGINTAP)
				d++;
			continue;
		}
		if (!strcmp(argv[d], "--getsz"))
			continue;
		if (!strcmp(argv[d], "--")) {
			d++;
			break;
		}
		if (argv[d][0] != '-')
			break;
	}

	if (d >= argc)
		usage();

	for (k = d; k < argc; k++) {
		fd = open(argv[k], O_RDONLY, 0);
		if (fd < 0) {
			perror(argv[k]);
			exit(1);
		}
		do_commands(fd, argv, d);
		close(fd);
	}
	return 0;
}

void
do_commands(int fd, char **argv, int d) {
	int res, i, j;
	int iarg;
	long larg;
	long long llarg;
	unsigned long lu;
	unsigned long long llu;
	int verbose = 0;

	for (i = 1; i < d; i++) {
		if (!strcmp(argv[i], "-v")) {
			verbose = 1;
			continue;
		}
		if (!strcmp(argv[i], "-q")) {
			verbose = 0;
			continue;
		}

		if (!strcmp(argv[i], "--getsz")) {
			res = blkdev_get_sectors(fd, &llu);
			if (res == 0)
				printf("%lld\n", llu);
			else
				exit(1);
			continue;
		}

		j = find_cmd(argv[i]);
		if (j == -1) {
			fprintf(stderr, _("%s: Unknown command: %s\n"),
				progname, argv[i]);
			usage();
		}

		switch(bdcms[j].argtype) {
		default:
		case ARGNONE:
			res = ioctl(fd, bdcms[j].ioc, 0);
			break;
		case ARGINTA:
			if (i == d-1) {
				fprintf(stderr, _("%s requires an argument\n"),
					bdcms[j].name);
				usage();
			}
			iarg = atoi(argv[++i]);
			res = ioctl(fd, bdcms[j].ioc, iarg);
			break;
		case ARGINTAP:
			if (i == d-1) {
				fprintf(stderr, _("%s requires an argument\n"),
					bdcms[j].name);
				usage();
			}
			iarg = atoi(argv[++i]);
			res = ioctl(fd, bdcms[j].ioc, &iarg);
			break;
		case ARGINTP:
		case ARGINTG:
			iarg = bdcms[j].argval;
			res = ioctl(fd, bdcms[j].ioc, &iarg);
			break;
		case ARGLINTG:
			larg = bdcms[j].argval;
			res = ioctl(fd, bdcms[j].ioc, &larg);
			break;
		case ARGLLINTG:
			llarg = bdcms[j].argval;
			res = ioctl(fd, bdcms[j].ioc, &llarg);
			break;
		case ARGLU:
			lu = bdcms[j].argval;
			res = ioctl(fd, bdcms[j].ioc, &lu);
			break;
		case ARGLLU:
			llu = bdcms[j].argval;
			res = ioctl(fd, bdcms[j].ioc, &llu);
			break;

		}
		if (res == -1) {
			perror(bdcms[j].iocname);
			if (verbose)
				printf(_("%s failed.\n"), _(bdcms[j].help));
			exit(1);
		}
		switch(bdcms[j].argtype) {
		case ARGINTG:
			if (verbose)
				printf("%s: %d\n", _(bdcms[j].help), iarg);
			else
				printf("%d\n", iarg);
			break;
		case ARGLINTG:
			if (verbose)
				printf("%s: %ld\n", _(bdcms[j].help), larg);
			else
				printf("%ld\n", larg);
			break;
		case ARGLLINTG:
			if (verbose)
				printf("%s: %lld\n", _(bdcms[j].help), llarg);
			else
				printf("%lld\n", llarg);
			break;
		case ARGLU:
			if (verbose)
				printf("%s: %lu\n", _(bdcms[j].help), lu);
			else
				printf("%lu\n", lu);
			break;
		case ARGLLU:
			if (verbose)
				printf("%s: %llu\n", _(bdcms[j].help), llu);
			else
				printf("%llu\n", llu);
			break;

		default:
			if (verbose)
				printf(_("%s succeeded.\n"), _(bdcms[j].help));
			break;
		}
	}
}

#define PROC_PARTITIONS "/proc/partitions"

void
report_all_devices(void) {
	FILE *procpt;
	char line[200];
	char ptname[200];
	char device[210];
	int ma, mi, sz;

	procpt = fopen(PROC_PARTITIONS, "r");
	if (!procpt) {
		fprintf(stderr, _("%s: cannot open %s\n"),
			progname, PROC_PARTITIONS);
		exit(1);
	}

	while (fgets(line, sizeof(line), procpt)) {
		if (sscanf (line, " %d %d %d %200[^\n ]",
			    &ma, &mi, &sz, ptname) != 4)
			continue;

		sprintf(device, "/dev/%s", ptname);
		report_device(device, 1);
	}

	fclose(procpt);
}

void
report_device(char *device, int quiet) {
	int fd;
	int ro, ssz, bsz;
	long ra;
	unsigned long long bytes;
	struct hd_geometry g;

	fd = open(device, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		if (!quiet)
			fprintf(stderr, _("%s: cannot open %s\n"),
				progname, device);
		return;
	}

	ro = ssz = bsz = 0;
	g.start = ra = 0;
	if (ioctl (fd, BLKROGET, &ro) == 0 &&
	    ioctl (fd, BLKRAGET, &ra) == 0 &&
	    ioctl (fd, BLKSSZGET, &ssz) == 0 &&
	    ioctl (fd, BLKBSZGET, &bsz) == 0 &&
	    ioctl (fd, HDIO_GETGEO, &g) == 0 &&
	    blkdev_get_size (fd, &bytes) == 0) {
		printf("%s %5ld %5d %5d %10ld %15lld   %s\n",
		       ro ? "ro" : "rw", ra, ssz, bsz, g.start, bytes, device);
	} else {
		if (!quiet)
			fprintf(stderr, _("%s: ioctl error on %s\n"),
				progname, device);
	}

	close(fd);
}

void
report_header() {
	printf(_("RO    RA   SSZ   BSZ   StartSec            Size   Device\n"));
}
