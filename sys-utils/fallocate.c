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


static int skip_hole(int fd, off_t *off)
{
	off_t newoff;

	errno = 0;
	newoff	= lseek(fd, *off, SEEK_DATA);

	/* ENXIO means that there is no more data -- probably sparse hole at
	 * the end of the file */
	if (newoff < 0 && errno == ENXIO)
		return 1;

	if (newoff > *off) {
		*off = newoff;
		return 0;	/* success */
	}
	return -1;		/* no hole */
}

static void dig_holes(int fd, off_t off, off_t len)
{
	off_t end = len ? off + len : 0;
	off_t hole_start = 0, hole_sz = 0;
	uintmax_t ct = 0;
	size_t bufsz;
	char *buf, *empty;
	struct stat st;
	int sparse = 0;

	if (fstat(fd, &st) != 0)
		err(EXIT_FAILURE, _("stat failed %s"), filename);
	bufsz = st.st_blksize;

	if (st.st_blocks * 512 < st.st_size) {
		if (verbose)
			fprintf(stdout, _("%s: already has holes.\n"), filename);
		sparse = 1;
	}
	if (lseek(fd, off, SEEK_SET) < 0)
		err(EXIT_FAILURE, _("seek on %s failed"), filename);

	buf = xmalloc(bufsz);
	empty = xcalloc(1, bufsz);

#if defined(POSIX_FADV_SEQUENTIAL) && defined(POSIX_FADV_NOREUSE) && defined(HAVE_POSIX_FADVISE)
	posix_fadvise(fd, off, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE);
#endif

	while (end == 0 || off < end) {
		ssize_t rsz;

		rsz = pread(fd, buf, bufsz, off);
		if (rsz < 0 && errno)
			err(EXIT_FAILURE, _("%s: read failed"), filename);
		if (end && rsz > 0 && off + rsz > end)
			rsz = end - off;
		if (rsz <= 0)
			break;

		if (memcmp(buf, empty, rsz) == 0) {
			if (!hole_sz) {				/* new hole detected */
				if (sparse) {
					int rc = skip_hole(fd, &off);
					if (rc == 0)
						continue;	/* hole skipped */
					else if (rc == 1)
						break;		/* end of file */
				}
				hole_start = off;
			}
			hole_sz += rsz;
		 } else if (hole_sz) {
			xfallocate(fd, FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE,
				   hole_start, hole_sz);
			ct += hole_sz;
			hole_sz = hole_start = 0;
		}
		off += rsz;
	}

	if (hole_sz) {
		xfallocate(fd, FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE,
				hole_start, hole_sz);
		ct += hole_sz;
	}

	free(buf);
	free(empty);

	if (verbose) {
		char *str = size_to_human_string(SIZE_SUFFIX_3LETTER | SIZE_SUFFIX_SPACE, ct);
		fprintf(stdout, _("%s: %s (%ju bytes) converted to sparse holes.\n"),
				filename, str, ct);
		free(str);
	}
}

int main(int argc, char **argv)
{
	int	c;
	int	fd;
	int	mode = 0;
	int	dig = 0;
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
			dig = 1;
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
	if (dig) {
		if (mode != 0)
			errx(EXIT_FAILURE, _("Can't use -p or -n with --dig-holes"));
		if (length == -2LL)
			length = 0;
		if (length < 0)
			errx(EXIT_FAILURE, _("invalid length value specified"));
	} else {
		if (length == -2LL)
			errx(EXIT_FAILURE, _("no length argument specified"));
		if (length <= 0)
			errx(EXIT_FAILURE, _("invalid length value specified"));
	}
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

	if (dig)
		dig_holes(fd, offset, length);
	else
		xfallocate(fd, mode, offset, length);

	if (close_fd(fd) != 0)
		err(EXIT_FAILURE, _("write failed: %s"), filename);

	return EXIT_SUCCESS;
}
