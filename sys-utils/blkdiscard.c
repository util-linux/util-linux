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
#define BLKDISCARD	_IO(0x12,119)
#endif

#ifndef BLKSECDISCARD
#define BLKSECDISCARD	_IO(0x12,125)
#endif

#define print_stats(path, stats) \
	printf(_("%s: Discarded %" PRIu64 " bytes from the " \
		 "offset %" PRIu64"\n"), path, stats[1], stats[0]);

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] <device>\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Discard the content of sectors on a device.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -o, --offset <num>  offset in bytes to discard from\n"
		" -l, --length <num>  length of bytes to discard from the offset\n"
		" -p, --step <num>    size of the discard iterations within the offset\n"
		" -s, --secure        perform secure discard\n"
		" -v, --verbose       print aligned length and offset\n"),
		out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("blkdiscard(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	char *path;
	int c, fd, verbose = 0, secure = 0, secsize;
	uint64_t end, blksize, step, range[2], stats[2];
	struct stat sb;
	struct timeval now, last;

	static const struct option longopts[] = {
	    { "help",      0, 0, 'h' },
	    { "version",   0, 0, 'V' },
	    { "offset",    1, 0, 'o' },
	    { "length",    1, 0, 'l' },
	    { "step",      1, 0, 'p' },
	    { "secure",    0, 0, 's' },
	    { "verbose",   0, 0, 'v' },
	    { NULL,        0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	range[0] = 0;
	range[1] = ULLONG_MAX;
	step = 0;

	while ((c = getopt_long(argc, argv, "hVsvo:l:p:", longopts, NULL)) != -1) {
		switch(c) {
		case 'h':
			usage(stdout);
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
			secure = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage(stderr);
			break;
		}
	}

	if (optind == argc)
		errx(EXIT_FAILURE, _("no device specified"));

	path = argv[optind++];

	if (optind != argc) {
		warnx(_("unexpected number of arguments"));
		usage(stderr);
	}

	fd = open(path, O_WRONLY);
	if (fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), path);

	if (fstat(fd, &sb) == -1)
		err(EXIT_FAILURE, _("stat failed %s"), path);
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

	for (range[0] = range[0]; range[0] < end; range[0] += range[1]) {
		if (range[0] + range[1] > end)
			range[1] = end - range[0];

		if (secure) {
			if (ioctl(fd, BLKSECDISCARD, &range))
				err(EXIT_FAILURE, _("%s: BLKSECDISCARD ioctl failed"), path);
		} else {
			if (ioctl(fd, BLKDISCARD, &range))
				err(EXIT_FAILURE, _("%s: BLKDISCARD ioctl failed"), path);
		}

		/* reporting progress */
		if (verbose && step) {
			gettime_monotonic(&now);
			if (last.tv_sec < now.tv_sec) {
				print_stats(path, stats);
				stats[0] = range[0], stats[1] = 0;
				last = now;
			}
		}

		stats[1] += range[1];
	}

	if (verbose)
		print_stats(path, stats);

	close(fd);
	return EXIT_SUCCESS;
}
