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

#include "c.h"
#include "nls.h"
#include "blkdev.h"

const char *progname;


struct bdc {
	long		ioc;		/* ioctl code */
	const char	*iocname;	/* ioctl name (e.g. BLKROSET) */
	long		argval;		/* default argument */

	const char	*name;		/* --setfoo */
	const char	*argname;	/* argument name or NULL */

	const char	*help;

	int		argtype;
	int		flags;
};

/* command flags */
enum {
	FL_NOPTR	= (1 << 1),	/* does not assume pointer (ARG_INT only)*/
	FL_NORESULT	= (1 << 2)	/* does not return any data */
};

/* ioctl argument types */
enum {
	ARG_NONE,
	ARG_USHRT,
	ARG_INT,
	ARG_UINT,
	ARG_LONG,
	ARG_ULONG,
	ARG_LLONG,
	ARG_ULLONG
};

#define IOCTL_ENTRY( io )	.ioc = io, .iocname = # io

struct bdc bdcms[] =
{
	{
		IOCTL_ENTRY(BLKROSET),
		.name = "--setro",
		.argtype = ARG_INT,
		.argval = 1,
		.flags = FL_NORESULT,
		.help = N_("set read-only")
	},{
		IOCTL_ENTRY(BLKROSET),
		.name = "--setrw",
		.argtype = ARG_INT,
		.argval = 0,
		.flags = FL_NORESULT,
		.help = N_("set read-write")
	},{
		IOCTL_ENTRY(BLKROGET),
		.name = "--getro",
		.argtype = ARG_INT,
		.argval = -1,
		.help = N_("get read-only")
	},{
		IOCTL_ENTRY(BLKSSZGET),
		.name = "--getss",
		.argtype = ARG_INT,
		.argval = -1,
		.help = N_("get logical block (sector) size")
	},{
		IOCTL_ENTRY(BLKPBSZGET),
		.name = "--getpbsz",
		.argtype = ARG_UINT,
		.argval = -1,
		.help = N_("get physical block (sector) size")
	},{
		IOCTL_ENTRY(BLKIOMIN),
		.name = "--getiomin",
		.argtype = ARG_UINT,
		.argval = -1,
		.help = N_("get minimum I/O size")
	},{
		IOCTL_ENTRY(BLKIOOPT),
		.name = "--getioopt",
		.argtype = ARG_UINT,
		.argval = -1,
		.help = N_("get optimal I/O size")
	},{
		IOCTL_ENTRY(BLKALIGNOFF),
		.name = "--getalignoff",
		.argtype = ARG_INT,
		.argval = -1,
		.help = N_("get alignment offset")
	},{
		IOCTL_ENTRY(BLKSECTGET),
		.name = "--getmaxsect",
		.argtype = ARG_USHRT,
		.argval = -1,
		.help = N_("get max sectors per request")
	},{
		IOCTL_ENTRY(BLKBSZGET),
		.name = "--getbsz",
		.argtype = ARG_INT,
		.argval = -1,
		.help = N_("get blocksize")
	},{
		IOCTL_ENTRY(BLKBSZSET),
		.name = "--setbsz",
		.argname = "BLOCKSIZE",
		.argtype = ARG_INT,
		.flags = FL_NORESULT,
	        .help = N_("set blocksize")
	},{
		IOCTL_ENTRY(BLKGETSIZE),
		.name = "--getsize",
		.argtype = ARG_ULONG,
		.argval = -1,
		.help = N_("get 32-bit sector count")
	},{
		IOCTL_ENTRY(BLKGETSIZE64),
		.name = "--getsize64",
		.argtype = ARG_ULLONG,
		.argval = -1,
		.help = N_("get size in bytes")
	},{
		IOCTL_ENTRY(BLKRASET),
		.name = "--setra",
		.argname = "READAHEAD",
		.argtype = ARG_INT,
		.flags = FL_NOPTR | FL_NORESULT,
		.help = N_("set readahead")
	},{
		IOCTL_ENTRY(BLKRAGET),
		.name = "--getra",
		.argtype = ARG_LONG,
		.argval = -1,
		.help = N_("get readahead")
	},{
		IOCTL_ENTRY(BLKFRASET),
		.name = "--setfra",
		.argname = "FSREADAHEAD",
		.argtype = ARG_INT,
		.flags = FL_NOPTR | FL_NORESULT,
		.help = N_("set filesystem readahead")
	},{
		IOCTL_ENTRY(BLKFRAGET),
		.name = "--getfra",
		.argtype = ARG_LONG,
		.argval = -1,
		.help = N_("get filesystem readahead")
	},{
		IOCTL_ENTRY(BLKFLSBUF),
		.name = "--flushbufs",
		.help = N_("flush buffers")
	},{
		IOCTL_ENTRY(BLKRRPART),
		.name = "--rereadpt",
		.help = N_("reread partition table")
	}
};

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
	for (i = 0; i < ARRAY_SIZE(bdcms); i++) {
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

	for (j = 0; j < ARRAY_SIZE(bdcms); j++)
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
			if (bdcms[j].argname)
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
	unsigned int uarg;
	unsigned short huarg;
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
		case ARG_NONE:
			res = ioctl(fd, bdcms[j].ioc, 0);
			break;
		case ARG_USHRT:
			huarg = bdcms[j].argval;
			res = ioctl(fd, bdcms[j].ioc, &huarg);
			break;
		case ARG_INT:
			if (bdcms[j].argname) {
				if (i == d-1) {
					fprintf(stderr, _("%s requires an argument\n"),
						bdcms[j].name);
					usage();
				}
				iarg = atoi(argv[++i]);
			} else
				iarg = bdcms[j].argval;

			res = bdcms[j].flags & FL_NOPTR ?
					ioctl(fd, bdcms[j].ioc, iarg) :
					ioctl(fd, bdcms[j].ioc, &iarg);
			break;
		case ARG_UINT:
			uarg = bdcms[j].argval;
			res = ioctl(fd, bdcms[j].ioc, &uarg);
			break;
		case ARG_LONG:
			larg = bdcms[j].argval;
			res = ioctl(fd, bdcms[j].ioc, &larg);
			break;
		case ARG_LLONG:
			llarg = bdcms[j].argval;
			res = ioctl(fd, bdcms[j].ioc, &llarg);
			break;
		case ARG_ULONG:
			lu = bdcms[j].argval;
			res = ioctl(fd, bdcms[j].ioc, &lu);
			break;
		case ARG_ULLONG:
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

		if (bdcms[j].argtype == ARG_NONE ||
		    (bdcms[j].flags & FL_NORESULT)) {
			if (verbose)
				printf(_("%s succeeded.\n"), _(bdcms[j].help));
			continue;
		}

		if (verbose)
			printf("%s: ", _(bdcms[j].help));

		switch(bdcms[j].argtype) {
		case ARG_USHRT:
			printf("%hu\n", huarg);
			break;
		case ARG_INT:
			printf("%d\n", iarg);
			break;
		case ARG_UINT:
			printf("%u\n", uarg);
			break;
		case ARG_LONG:
			printf("%ld\n", larg);
			break;
		case ARG_LLONG:
			printf("%lld\n", llarg);
			break;
		case ARG_ULONG:
			printf("%lu\n", lu);
			break;
		case ARG_ULLONG:
			printf("%llu\n", llu);
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
