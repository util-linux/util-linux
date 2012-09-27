/*
 * fstrim.c -- discard the part (or whole) of mounted filesystem.
 *
 * Copyright (C) 2010,2012 Red Hat, Inc. All rights reserved.
 * Written by Lukas Czerner <lczerner@redhat.com>
 *            Karel Zak <kzak@redhat.com>
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
 *
 * This program uses FITRIM ioctl to discard parts or the whole filesystem
 * online (mounted). You can specify range (start and length) to be
 * discarded, or simply discard whole filesystem.
 */
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <limits.h>
#include <getopt.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/fs.h>

#include "nls.h"
#include "strutils.h"
#include "c.h"
#include "closestream.h"
#include "blkdev.h"

#ifndef FITRIM
struct fstrim_range {
	uint64_t start;
	uint64_t len;
	uint64_t minlen;
};
# define FITRIM		_IOWR('X', 121, struct fstrim_range)
#endif

#ifndef BLKDISCARD
# define BLKDISCARD	_IO(0x12,119)
#endif
#ifndef BLKSECDISCARD
# define BLKSECDISCARD	_IO(0x12,125)
#endif

static int is_blk = 0;

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);

	if (is_blk) {
		fprintf(out, _(" %s [options] <device>\n"), program_invocation_short_name);
		fputs(USAGE_OPTIONS, out);
		fputs(_(" -s, --secure        perform secure discard\n"), out);
	} else {
		fprintf(out, _(" %s [options] <mountpoint>\n"), program_invocation_short_name);
		fputs(USAGE_OPTIONS, out);
		fputs(_(" -m, --minimum <num> minimum extent length to discard\n"), out);
	}
	fputs(_(" -o, --offset <num>  offset in bytes to discard from\n"
		" -l, --length <num>  length of bytes to discard from the offset\n"
		" -v, --verbose       print number of discarded bytes\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL(is_blk ? "blkdiscard(8)" : "fstrim(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void do_fstrim(const char *path, uint64_t off, uint64_t len, uint64_t minlen)
{
	int fd;
	struct stat sb;
	struct fstrim_range range;

	range.start = off;
	range.len = len;
	range.minlen = minlen;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), path);
	if (fstat(fd, &sb) == -1)
		err(EXIT_FAILURE, _("stat failed %s"), path);
	if (!S_ISDIR(sb.st_mode))
		errx(EXIT_FAILURE, _("%s: not a directory"), path);
	if (ioctl(fd, FITRIM, &range))
		err(EXIT_FAILURE, _("%s: FITRIM ioctl failed"), path);
	close(fd);
}

static void do_blkdiscard(const char *path, uint64_t off, uint64_t len, int sec)
{
	int fd, secsize;
	struct stat sb;
	uint64_t blksize, range[2], end;

	fd = open(path, O_WRONLY);
	if (fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), path);
	if (fstat(fd, &sb) == -1)
		err(EXIT_FAILURE, _("stat failed %s"), path);
	if (!S_ISBLK(sb.st_mode))
		errx(EXIT_FAILURE, _("%s: not a block device"), path);
	if (blkdev_get_size(fd, (unsigned long long *) &blksize) != 0)
		err(EXIT_FAILURE, _("%s: failed to get device size"), path);
	if (blkdev_get_sector_size(fd, &secsize) != 0)
		err(EXIT_FAILURE, _("%s: failed to get sector size"), path);

	/* align range to the sector size */
	range[0] = (off + secsize - 1) & ~(secsize - 1);
	range[1] = len & ~(secsize - 1);

	/* is the range end behind the end of the device ?*/
	end = range[0] + range[1];
	if (end < range[0] || end > blksize)
		range[1] = blksize - range[0];

	if (sec) {
		if (ioctl(fd, BLKSECDISCARD, &range))
			err(EXIT_FAILURE, _("%s: BLKSECDISCARD ioctl failed"), path);
	} else {
		if (ioctl(fd, BLKDISCARD, &range))
			err(EXIT_FAILURE, _("%s: BLKDISCARD ioctl failed"), path);
	}
	close(fd);
}

int main(int argc, char **argv)
{
	char *path;
	int c, verbose = 0, secure = 0;
	uint64_t len = UINT64_MAX, off = 0, minlen = 0;

	static const struct option fs_longopts[] = {
	    { "help",      0, 0, 'h' },
	    { "version",   0, 0, 'V' },
	    { "offset",    1, 0, 'o' },
	    { "length",    1, 0, 'l' },
	    { "minimum",   1, 0, 'm' },
	    { "verbose",   0, 0, 'v' },
	    { NULL,        0, 0, 0 }
	};
	static const struct option blk_longopts[] = {
	    { "help",      0, 0, 'h' },
	    { "version",   0, 0, 'V' },
	    { "offset",    1, 0, 'o' },
	    { "length",    1, 0, 'l' },
	    { "secure",    0, 0, 's' },
	    { "verbose",   0, 0, 'v' },
	    { NULL,        0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	if (strcmp(program_invocation_short_name, "blkdiscard") == 0)
		is_blk = 1;

	do {
		if (is_blk)
			c = getopt_long(argc, argv, "hVo:l:sv", blk_longopts, NULL);
		else
			c = getopt_long(argc, argv, "hVo:l:m:v", fs_longopts, NULL);
		if (c == -1)
			break;

		switch(c) {
		case 'h':
			usage(stdout);
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'l':
			len = strtosize_or_err(optarg,
					_("failed to parse length"));
			break;
		case 'o':
			off = strtosize_or_err(optarg,
					_("failed to parse offset"));
			break;
		case 'm':
			minlen = strtosize_or_err(optarg,
					_("failed to parse minimum extent length"));
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
	} while (1);

	if (optind == argc)
		errx(EXIT_FAILURE, is_blk ? _("no device specified.") :
					    _("no mountpoint specified"));
	path = argv[optind++];

	if (optind != argc) {
		warnx(_("unexpected number of arguments"));
		usage(stderr);
	}

	if (is_blk)
		do_blkdiscard(path, off, len, secure);
	else
		do_fstrim(path, off, len, minlen);

	if (verbose)
		/* TRANSLATORS: The standard value here is a very large number. */
		printf(_("%s: %" PRIu64 " bytes were trimmed\n"), path, len);

	return EXIT_SUCCESS;
}
