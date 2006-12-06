/*
 * blockdev.c --- Do various simple block device ioctls from the command line
 * aeb, 991028
 */

#include <stdio.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "nls.h"

/* Since it is impossible to include <linux/fs.h>, let us
   give the ioctls explicitly. */

#ifndef BLKROSET
#define BLKROSET   _IO(0x12,93)
#define BLKROGET   _IO(0x12,94)
#define BLKRRPART  _IO(0x12,95)
#define BLKGETSIZE _IO(0x12,96)
#define BLKFLSBUF  _IO(0x12,97)
#define BLKRASET   _IO(0x12,98)
#define BLKRAGET   _IO(0x12,99)
#define BLKSSZGET  _IO(0x12,104)
#endif

const char *progname;

struct bdc {
	char *name;
	char *iocname;
	long ioc;
	int argtype;
#define ARGNONE	0
#define ARGINTA	1
#define	ARGINTP	2
#define ARGINTG	3
#define ARGLINTG 4
	long argval;
	char *argname;
	char *help;
} bdcms[] = {
#ifdef BLKROSET
	{ "--setro", "BLKROSET", BLKROSET, ARGINTP, 1, NULL, N_("set read-only") },
	{ "--setrw", "BLKROSET", BLKROSET, ARGINTP, 0, NULL, N_("set read-write") },
#endif
#ifdef BLKROGET
	{ "--getro", "BLKROGET", BLKROGET, ARGINTG, -1, NULL, N_("get read-only") },
#endif
#ifdef BLKSSZGET
	{ "--getss", "BLKSSZGET", BLKSSZGET, ARGINTG, -1, NULL, N_("get sectorsize") },
#endif
#ifdef BLKGETSIZE
	{ "--getsize", "BLKGETSIZE", BLKGETSIZE, ARGLINTG, -1, NULL, N_("get size") },
#endif
#ifdef BLKRASET
	{ "--setra", "BLKRASET", BLKRASET, ARGINTA, 0, "READAHEAD", N_("set readahead") },
#endif
#ifdef BLKRAGET
	{ "--getra", "BLKRAGET", BLKRAGET, ARGLINTG, -1, NULL, N_("get readahead") },
#endif
#ifdef BLKFLSBUF
	{ "--flushbufs", "BLKFLSBUF", BLKFLSBUF, ARGNONE, 0, NULL, N_("flush buffers") },
#endif
#ifdef BLKRRPART
	{ "--rereadpt", "BLKRRPART", BLKRRPART, ARGNONE, 0, NULL,
	  N_("reread partition table") },
#endif
};

#define SIZE(a)	(sizeof(a)/sizeof((a)[0]))

static void
usage(void) {
	int i;
	fprintf(stderr, _("Usage: %s [-V] [-v|-q] commands devices\n"), progname);
	fprintf(stderr, _("Available commands:\n"));
	for (i = 0; i < SIZE(bdcms); i++) {
		fprintf(stderr, "\t%s", bdcms[i].name);
		if (bdcms[i].argname)
			fprintf(stderr, " %s", bdcms[i].argname);
		if (bdcms[i].help)
			fprintf(stderr, "\t(%s)", _(bdcms[i].help));
		fprintf(stderr, "\n");
	}
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
		printf("%s from %s\n", progname, util_linux_version);
		exit(0);
	}

	/* do each of the commands on each of the devices */
	/* devices start after last command */
	for (d = 1; d < argc; d++) {
		j = find_cmd(argv[d]);
		if (j >= 0) {
			if (bdcms[j].argtype == ARGINTA)
				d++;
			continue;
		}
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
	}
	return 0;
}

void
do_commands(int fd, char **argv, int d) {
	int res, i, j;
	int iarg;
	long larg;
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

		j = find_cmd(argv[i]);
		if (j == -1) {
			fprintf(stderr, _("%s: Unknown command: %s\n"), progname, argv[i]);
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
		case ARGINTP:
		case ARGINTG:
			iarg = bdcms[j].argval;
			res = ioctl(fd, bdcms[j].ioc, &iarg);
			break;
		case ARGLINTG:
			larg = bdcms[j].argval;
			res = ioctl(fd, bdcms[j].ioc, &larg);
			break;
		}
		if (res == -1) {
			perror(bdcms[j].iocname);
			if (verbose)
				printf("%s failed.\n", _(bdcms[j].help));
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
		default:
			if (verbose)
				printf("%s succeeded.\n", _(bdcms[j].help));
			break;
		}
	}
}
