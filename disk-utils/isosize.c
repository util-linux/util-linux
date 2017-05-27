/*
 * isosize.c - Andries Brouwer, 000608
 *
 * use header info to find size of iso9660 file system
 * output a number - useful in scripts
 *
 * Synopsis:
 *    isosize [-x] [-d <num>] <filename>
 *        where "-x" gives length in sectors and sector size while
 *              without this argument the size is given in bytes
 *        without "-x" gives length in bytes unless "-d <num>" is
 *		given. In the latter case the length in bytes divided
 *		by <num> is given
 *
 *  Version 2.03 2000/12/21
 *     - add "-d <num>" option and use long long to fix things > 2 GB
 *  Version 2.02 2000/10/11
 *     - error messages on IO failures [D. Gilbert]
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "nls.h"
#include "c.h"
#include "strutils.h"
#include "closestream.h"

static int is_iso(int fd)
{
	char label[8];
	if (pread(fd, &label, 8, 0x8000) == -1)
		return 1;
	return memcmp(&label, &"\1CD001\1", 8);
}

static int isonum_721(unsigned char *p)
{
	return ((p[0] & 0xff)
		| ((p[1] & 0xff) << 8));
}

static int isonum_722(unsigned char *p)
{
	return ((p[1] & 0xff)
		| ((p[0] & 0xff) << 8));
}

static int isonum_723(unsigned char *p, int xflag)
{
	int le = isonum_721(p);
	int be = isonum_722(p + 2);
	if (xflag && le != be)
		/* translation is useless */
		warnx("723error: le=%d be=%d", le, be);
	return (le);
}

static int isonum_731(unsigned char *p)
{
	return ((p[0] & 0xff)
		| ((p[1] & 0xff) << 8)
		| ((p[2] & 0xff) << 16)
		| ((p[3] & 0xff) << 24));
}

static int isonum_732(unsigned char *p)
{
	return ((p[3] & 0xff)
		| ((p[2] & 0xff) << 8)
		| ((p[1] & 0xff) << 16)
		| ((p[0] & 0xff) << 24));
}

static int isonum_733(unsigned char *p, int xflag)
{
	int le = isonum_731(p);
	int be = isonum_732(p + 4);
	if (xflag && le != be)
		/* translation is useless */
		warnx("733error: le=%d be=%d", le, be);
	return (le);
}

static void isosize(int argc, char *filenamep, int xflag, long divisor)
{
	int fd, nsecs, ssize;
	unsigned char volume_space_size[8];
	unsigned char logical_block_size[4];

	if ((fd = open(filenamep, O_RDONLY)) < 0)
		err(EXIT_FAILURE, _("cannot open %s"), filenamep);
	if (is_iso(fd))
		warnx(_("%s: might not be an ISO filesystem"), filenamep);

	if (pread(fd, volume_space_size, sizeof(volume_space_size), 0x8050) <= 0 ||
	    pread(fd, logical_block_size, sizeof(logical_block_size), 0x8080) <= 0) {
		if (errno)
			err(EXIT_FAILURE, _("read error on %s"), filenamep);
		errx(EXIT_FAILURE, _("read error on %s"), filenamep);
	}

	nsecs = isonum_733(volume_space_size, xflag);
	/* isonum_723 returns nowadays always 2048 */
	ssize = isonum_723(logical_block_size, xflag);

	if (1 < argc)
		printf("%s: ", filenamep);
	if (xflag) {
		printf(_("sector count: %d, sector size: %d\n"), nsecs, ssize);
	} else {
		long long product = nsecs;

		if (divisor == 0)
			printf("%lld\n", product * ssize);
		else if (divisor == ssize)
			printf("%d\n", nsecs);
		else
			printf("%lld\n", (product * ssize) / divisor);
	}

	close(fd);
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out,
		_(" %s [options] <iso9660_image_file>\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Show the length of an ISO-9660 filesystem.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -d, --divisor=<number>  divide the amount of bytes by <number>\n"), out);
	fputs(_(" -x, --sectors           show sector count and size\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("isosize(8)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int j, ct, opt, xflag = 0;
	long divisor = 0;

	static const struct option longopts[] = {
		{"divisor", required_argument, NULL, 'd'},
		{"sectors", no_argument,       NULL, 'x'},
		{"version", no_argument,       NULL, 'V'},
		{"help",    no_argument,       NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((opt = getopt_long(argc, argv, "d:xVh", longopts, NULL)) != -1)
		switch (opt) {
		case 'd':
			divisor =
			    strtol_or_err(optarg,
					  _("invalid divisor argument"));
			break;
		case 'x':
			xflag = 1;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout);
		default:
			errtryhelp(EXIT_FAILURE);
		}

	ct = argc - optind;

	if (ct <= 0)
		usage(stderr);

	for (j = optind; j < argc; j++)
		isosize(ct, argv[j], xflag, divisor);

	return EXIT_SUCCESS;
}
