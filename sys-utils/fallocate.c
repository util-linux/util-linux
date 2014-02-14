/*
 * fallocate - utility to use the fallocate system call
 *
 * Copyright (C) 2008-2009 Red Hat, Inc. All rights reserved.
 * Written by Eric Sandeen <sandeen@redhat.com>
 *            Karel Zak <kzak@redhat.com>
 *
 * cvtnum routine taken from xfsprogs,
 * Copyright (c) 2003-2005 Silicon Graphics, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <string.h>

#ifndef HAVE_FALLOCATE
# include <sys/syscall.h>
#endif

#if defined(HAVE_LINUX_FALLOC_H) && \
    (!defined(FALLOC_FL_KEEP_SIZE) || !defined(FALLOC_FL_PUNCH_HOLE))
# include <linux/falloc.h>	/* non-libc fallback for FALLOC_FL_* flags */
#endif

#ifndef FALLOC_FL_KEEP_SIZE
# define FALLOC_FL_KEEP_SIZE 1
#endif

#ifndef FALLOC_FL_PUNCH_HOLE
# define FALLOC_FL_PUNCH_HOLE 2
#endif

#include "nls.h"
#include "strutils.h"
#include "c.h"
#include "closestream.h"
#include "xalloc.h"

static int verbose;
static char *filename;

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] <filename>\n"), program_invocation_short_name);
	fputs(USAGE_OPTIONS, out);
	fputs(_(" -d, --dig-holes     detect and dig holes\n"), out);
	fputs(_(" -l, --length <num>  length of the (de)allocation, in bytes\n"), out);
	fputs(_(" -n, --keep-size     don't modify the length of the file\n"), out);
	fputs(_(" -o, --offset <num>  offset of the (de)allocation, in bytes\n"), out);
	fputs(_(" -p, --punch-hole    punch holes in the file\n"), out);
	fputs(_(" -v, --verbose       verbose mode\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("fallocate(1)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static loff_t cvtnum(char *s)
{
	uintmax_t x;

	if (strtosize(s, &x))
		return -1LL;

	return x;
}

static void xfallocate(int fd, int mode, off_t offset, off_t length)
{
	int error;

#ifdef HAVE_FALLOCATE
	error = fallocate(fd, mode, offset, length);
#else
	error = syscall(SYS_fallocate, fd, mode, offset, length);
#endif
	/*
	 * EOPNOTSUPP: The FALLOC_FL_KEEP_SIZE is unsupported
	 * ENOSYS: The filesystem does not support sys_fallocate
	 */
	if (error < 0) {
		if ((mode & FALLOC_FL_KEEP_SIZE) && errno == EOPNOTSUPP)
			errx(EXIT_FAILURE, _("keep size mode (-n option) unsupported"));
		err(EXIT_FAILURE, _("fallocate failed"));
	}
}

/*
 * Look for chunks of '\0's with size hole_size and when we find them, dig a
 * hole on that offset with that size
 */
static void detect_holes(int fd, size_t hole_size)
{
	void *zeros;
	ssize_t bufsz = hole_size;
	void *buf;
	off_t offset, end;

	/* Create a buffer of '\0's to compare against */
	/* XXX: Use mmap() with MAP_PRIVATE so Linux can avoid this allocation */
	zeros = mmap(NULL, hole_size, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (zeros == MAP_FAILED)
		err(EXIT_FAILURE, _("mmap failed"));

	/* buffer to read the file */
	buf = xmalloc(bufsz);

	end = lseek(fd, 0, SEEK_END);
	if (end < 0)
		err(EXIT_FAILURE, _("seek failed"));

	for (offset = 0; offset + (ssize_t) hole_size <= end; offset += bufsz) {
		/* Try to read hole_size bytes */
		bufsz = pread(fd, buf, hole_size, offset);
		if (bufsz == -1)
			err(EXIT_FAILURE, _("read failed"));

		/* Always use bufsz, as we may read less than hole_size bytes */
		if (memcmp(buf, zeros, bufsz))
			continue;

		if (verbose)
			fprintf(stdout, "%s: detected hole at offset %ju (size %ju)\n",
				filename, (uintmax_t) offset, (uintmax_t) bufsz);

		xfallocate(fd, FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE,
				offset, bufsz);
	}

	if (munmap(zeros, hole_size))
		err(EXIT_FAILURE, _("munmap failed"));
	free(buf);
}

int main(int argc, char **argv)
{
	int	c;
	int	fd;
	int	mode = 0;
	int	dig_holes = 0;
	loff_t	length = -2LL;
	loff_t	offset = 0;

	static const struct option longopts[] = {
	    { "help",       0, 0, 'h' },
	    { "version",    0, 0, 'V' },
	    { "keep-size",  0, 0, 'n' },
	    { "punch-hole", 0, 0, 'p' },
	    { "dig-holes",  0, 0, 'd' },
	    { "offset",     1, 0, 'o' },
	    { "length",     1, 0, 'l' },
	    { "verbose",    0, 0, 'v' },
	    { NULL,         0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "hvVnpdl:o:", longopts, NULL)) != -1) {
		switch(c) {
		case 'h':
			usage(stdout);
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'p':
			mode |= FALLOC_FL_PUNCH_HOLE;
			/* fall through */
		case 'n':
			mode |= FALLOC_FL_KEEP_SIZE;
			break;
		case 'd':
			dig_holes = 1;
			break;
		case 'l':
			length = cvtnum(optarg);
			break;
		case 'o':
			offset = cvtnum(optarg);
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage(stderr);
			break;
		}
	}
	if (dig_holes && mode != 0)
		errx(EXIT_FAILURE, _("Can't use -p or -n with --dig-holes"));
	if (dig_holes && offset != 0)
		errx(EXIT_FAILURE, _("Can't use -o with --dig-holes"));
	if (length == -2LL && dig_holes)
		length = 32 * 1024;
	if (length == -2LL && !dig_holes)
		errx(EXIT_FAILURE, _("no length argument specified"));
	if (length <= 0)
		errx(EXIT_FAILURE, _("invalid length value specified"));
	if (offset < 0)
		errx(EXIT_FAILURE, _("invalid offset value specified"));
	if (optind == argc)
		errx(EXIT_FAILURE, _("no filename specified."));

	filename = argv[optind++];

	if (optind != argc) {
		warnx(_("unexpected number of arguments"));
		usage(stderr);
	}

	fd = open(filename, O_RDWR|O_CREAT, 0644);
	if (fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), filename);

	if (dig_holes)
		detect_holes(fd, length);
	else {
		if (verbose)
			fprintf(stdout, "%s: fallocate offset=%ju, length=%ju\n",
					filename, (uintmax_t) offset,
					(uintmax_t) length);
		xfallocate(fd, mode, offset, length);
	}

	if (close_fd(fd) != 0)
		err(EXIT_FAILURE, _("write failed: %s"), filename);

	return EXIT_SUCCESS;
}
