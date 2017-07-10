/*
 * blkdiscard.c -- discard the part (or whole) of the block device.
 *
 * Copyright (C) 2012 Red Hat, Inc. All rights reserved.
 * Written by Lukas Czerner <lczerner@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This program uses BLKDISCARD ioctl to discard part or the whole block
 * device if the device supports it. You can specify range (start and
 * length) to be discarded, or simply discard the whole device.
 */


#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <limits.h>
#include <getopt.h>
#include <time.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <linux/fs.h>

#include "nls.h"
#include "strutils.h"
#include "c.h"
#include "closestream.h"
#include "monotonic.h"

#ifndef BLKDISCARD
# define BLKDISCARD	_IO(0x12,119)
#endif

#ifndef BLKSECDISCARD
# define BLKSECDISCARD	_IO(0x12,125)
#endif

#ifndef BLKZEROOUT
# define BLKZEROOUT	_IO(0x12,127)
#endif

enum {
	ACT_DISCARD = 0,	/* default */
	ACT_ZEROOUT,
	ACT_SECURE
};

static void print_stats(int act, char *path, uint64_t stats[])
{
	switch (act) {
	case ACT_ZEROOUT:
		printf(_("%s: Zero-filled %" PRIu64 " bytes from the offset %" PRIu64"\n"), \
			path, stats[1], stats[0]);
		break;
	case ACT_SECURE:
	case ACT_DISCARD:
		printf(_("%s: Discarded %" PRIu64 " bytes from the offset %" PRIu64"\n"), \
			path, stats[1], stats[0]);
		break;
	}
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] <device>\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Discard the content of sectors on a device.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -o, --offset <num>  offset in bytes to discard from\n"), out);
	fputs(_(" -l, --length <num>  length of bytes to discard from the offset\n"), out);
	fputs(_(" -p, --step <num>    size of the discard iterations within the offset\n"), out);
	fputs(_(" -s, --secure        perform secure discard\n"), out);
	fputs(_(" -z, --zeroout       zero-fill rather than discard\n"), out);
	fputs(_(" -v, --verbose       print aligned length and offset\n"), out);

	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(21));

	printf(USAGE_MAN_TAIL("blkdiscard(8)"));
	exit(EXIT_SUCCESS);
}


int main(int argc, char **argv)
{
	char *path;
	int c, fd, verbose = 0, secsize;
	uint64_t end, blksize, step, range[2], stats[2];
	struct stat sb;
	struct timeval now, last;
	int act = ACT_DISCARD;

	static const struct option longopts[] = {
	    { "help",      no_argument,       NULL, 'h' },
	    { "version",   no_argument,       NULL, 'V' },
	    { "offset",    required_argument, NULL, 'o' },
	    { "length",    required_argument, NULL, 'l' },
	    { "step",      required_argument, NULL, 'p' },
	    { "secure",    no_argument,       NULL, 's' },
	    { "verbose",   no_argument,       NULL, 'v' },
	    { "zeroout",   no_argument,       NULL, 'z' },
	    { NULL, 0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	range[0] = 0;
	range[1] = ULLONG_MAX;
	step = 0;

	while ((c = getopt_long(argc, argv, "hVsvo:l:p:z", longopts, NULL)) != -1) {
		switch(c) {
		case 'h':
			usage();
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'l':
			range[1] = strtosize_or_err(optarg,
					_("failed to parse length"));
			break;
		case 'o':
			range[0] = strtosize_or_err(optarg,
					_("failed to parse offset"));
			break;
		case 'p':
			step = strtosize_or_err(optarg,
					_("failed to parse step"));
			break;
		case 's':
			act = ACT_SECURE;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'z':
			act = ACT_ZEROOUT;
			break;
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (optind == argc)
		errx(EXIT_FAILURE, _("no device specified"));

	path = argv[optind++];

	if (optind != argc) {
		warnx(_("unexpected number of arguments"));
		errtryhelp(EXIT_FAILURE);
	}

	fd = open(path, O_WRONLY);
	if (fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), path);

	if (fstat(fd, &sb) == -1)
		err(EXIT_FAILURE, _("stat of %s failed"), path);
	if (!S_ISBLK(sb.st_mode))
		errx(EXIT_FAILURE, _("%s: not a block device"), path);

	if (ioctl(fd, BLKGETSIZE64, &blksize))
		err(EXIT_FAILURE, _("%s: BLKGETSIZE64 ioctl failed"), path);
	if (ioctl(fd, BLKSSZGET, &secsize))
		err(EXIT_FAILURE, _("%s: BLKSSZGET ioctl failed"), path);

	/* check offset alignment to the sector size */
	if (range[0] % secsize)
		errx(EXIT_FAILURE, _("%s: offset %" PRIu64 " is not aligned "
			 "to sector size %i"), path, range[0], secsize);

	/* is the range end behind the end of the device ?*/
	if (range[0] > blksize)
		errx(EXIT_FAILURE, _("%s: offset is greater than device size"), path);
	end = range[0] + range[1];
	if (end < range[0] || end > blksize)
		end = blksize;

	range[1] = (step > 0) ? step : end - range[0];

	/* check length alignment to the sector size */
	if (range[1] % secsize)
		errx(EXIT_FAILURE, _("%s: length %" PRIu64 " is not aligned "
			 "to sector size %i"), path, range[1], secsize);

	stats[0] = range[0], stats[1] = 0;
	gettime_monotonic(&last);

	for (/* nothing */; range[0] < end; range[0] += range[1]) {
		if (range[0] + range[1] > end)
			range[1] = end - range[0];

		switch (act) {
		case ACT_ZEROOUT:
			if (ioctl(fd, BLKZEROOUT, &range))
				 err(EXIT_FAILURE, _("%s: BLKZEROOUT ioctl failed"), path);
			break;
		case ACT_SECURE:
			if (ioctl(fd, BLKSECDISCARD, &range))
				err(EXIT_FAILURE, _("%s: BLKSECDISCARD ioctl failed"), path);
			break;
		case ACT_DISCARD:
			if (ioctl(fd, BLKDISCARD, &range))
				err(EXIT_FAILURE, _("%s: BLKDISCARD ioctl failed"), path);
			break;
		}

		stats[1] += range[1];

		/* reporting progress at most once per second */
		if (verbose && step) {
			gettime_monotonic(&now);
			if (now.tv_sec > last.tv_sec &&
			    (now.tv_usec >= last.tv_usec || now.tv_sec > last.tv_sec + 1)) {
				print_stats(act, path, stats);
				stats[0] += stats[1], stats[1] = 0;
				last = now;
			}
		}
	}

	if (verbose && stats[1])
		print_stats(act, path, stats);

	close(fd);
	return EXIT_SUCCESS;
}
