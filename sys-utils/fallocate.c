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
    (!defined(FALLOC_FL_KEEP_SIZE) || !defined(FALLOC_FL_PUNCH_HOLE) || \
     !defined(FALLOC_FL_COLLAPSE_RANGE) || !defined(FALLOC_FL_ZERO_RANGE))
# include <linux/falloc.h>	/* non-libc fallback for FALLOC_FL_* flags */
#endif


#ifndef FALLOC_FL_KEEP_SIZE
# define FALLOC_FL_KEEP_SIZE		0x1
#endif

#ifndef FALLOC_FL_PUNCH_HOLE
# define FALLOC_FL_PUNCH_HOLE		0x2
#endif

#ifndef FALLOC_FL_COLLAPSE_RANGE
# define FALLOC_FL_COLLAPSE_RANGE	0x8
#endif

#ifndef FALLOC_FL_ZERO_RANGE
# define FALLOC_FL_ZERO_RANGE		0x10
#endif

#include "nls.h"
#include "strutils.h"
#include "c.h"
#include "closestream.h"
#include "xalloc.h"
#include "optutils.h"

static int verbose;
static char *filename;

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] <filename>\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Preallocate space to, or deallocate space from a file.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -c, --collapse-range remove a range from the file\n"), out);
	fputs(_(" -d, --dig-holes      detect zeroes and replace with holes\n"), out);
	fputs(_(" -l, --length <num>   length for range operations, in bytes\n"), out);
	fputs(_(" -n, --keep-size      maintain the apparent size of the file\n"), out);
	fputs(_(" -o, --offset <num>   offset for range operations, in bytes\n"), out);
	fputs(_(" -p, --punch-hole     replace a range with a hole (implies -n)\n"), out);
	fputs(_(" -z, --zero-range     zero and ensure allocation of a range\n"), out);
	fputs(_(" -v, --verbose        verbose mode\n"), out);

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

/* The real buffer size has to be bufsize + sizeof(uintptr_t) */
static int is_nul(void *buf, size_t bufsize)
{
	typedef uintptr_t word;
	void const *vp;
	char const *cbuf = buf, *cp;
	word const *wp = buf;

	/* set sentinel */
	memset((char *) buf + bufsize, '\1', sizeof(word));

	/* Find first nonzero *word*, or the word with the sentinel.  */
	while (*wp++ == 0)
		continue;

	/* Find the first nonzero *byte*, or the sentinel.  */
	vp = wp - 1;
	cp = vp;

	while (*cp++ == 0)
		continue;

	  return cbuf + bufsize < cp;
}

static void dig_holes(int fd, off_t off, off_t len)
{
	off_t end = len ? off + len : 0;
	off_t hole_start = 0, hole_sz = 0;
	uintmax_t ct = 0;
	size_t  bufsz;
	char *buf;
	struct stat st;
#if defined(POSIX_FADV_SEQUENTIAL) && defined(HAVE_POSIX_FADVISE)
	off_t cache_start = off;
	/*
	 * We don't want to call POSIX_FADV_DONTNEED to discard cached
	 * data in PAGE_SIZE steps. IMHO it's overkill (too many syscalls).
	 *
	 * Let's assume that 1MiB (on system with 4K page size) is just
	 * a good compromise.
	 *					    -- kzak Feb-2014
	 */
	const size_t cachesz = getpagesize() * 256;
#endif

	if (fstat(fd, &st) != 0)
		err(EXIT_FAILURE, _("stat failed %s"), filename);

	bufsz = st.st_blksize;

	if (lseek(fd, off, SEEK_SET) < 0)
		err(EXIT_FAILURE, _("seek on %s failed"), filename);

	/* buffer + extra space for is_nul() sentinel */
	buf = xmalloc(bufsz + sizeof(uintptr_t));
#if defined(POSIX_FADV_SEQUENTIAL) && defined(HAVE_POSIX_FADVISE)
	posix_fadvise(fd, off, 0, POSIX_FADV_SEQUENTIAL);
#endif

	while (end == 0 || off < end) {
		ssize_t rsz;

		rsz = pread(fd, buf, bufsz, off);
		if (rsz < 0 && errno)
			err(EXIT_FAILURE, _("%s: read failed"), filename);
		if (end && rsz > 0 && off > end - rsz)
			rsz = end - off;
		if (rsz <= 0)
			break;

		if (is_nul(buf, rsz)) {
			if (!hole_sz) {				/* new hole detected */
				int rc = skip_hole(fd, &off);
				if (rc == 0)
					continue;	/* hole skipped */
				else if (rc == 1)
					break;		/* end of file */
				hole_start = off;
			}
			hole_sz += rsz;
		 } else if (hole_sz) {
			xfallocate(fd, FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE,
				   hole_start, hole_sz);
			ct += hole_sz;
			hole_sz = hole_start = 0;
		}

#if defined(POSIX_FADV_DONTNEED) && defined(HAVE_POSIX_FADVISE)
		/* discard cached data */
		if (off - cache_start > (off_t) cachesz) {
			size_t clen = off - cache_start;

			clen = (clen / cachesz) * cachesz;
			posix_fadvise(fd, cache_start, clen, POSIX_FADV_DONTNEED);
			cache_start = cache_start + clen;
		}
#endif
		off += rsz;
	}

	if (hole_sz) {
		xfallocate(fd, FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE,
				hole_start, hole_sz);
		ct += hole_sz;
	}

	free(buf);

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
	    { "help",           0, 0, 'h' },
	    { "version",        0, 0, 'V' },
	    { "keep-size",      0, 0, 'n' },
	    { "punch-hole",     0, 0, 'p' },
	    { "collapse-range", 0, 0, 'c' },
	    { "dig-holes",      0, 0, 'd' },
	    { "zero-range",     0, 0, 'z' },
	    { "offset",         1, 0, 'o' },
	    { "length",         1, 0, 'l' },
	    { "verbose",        0, 0, 'v' },
	    { NULL,             0, 0, 0 }
	};

	static const ul_excl_t excl[] = {	/* rows and cols in in ASCII order */
		{ 'c', 'd', 'p', 'z' },
		{ 'c', 'n' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "hvVncpdzl:o:", longopts, NULL))
			!= -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch(c) {
		case 'h':
			usage(stdout);
			break;
		case 'c':
			mode |= FALLOC_FL_COLLAPSE_RANGE;
			break;
		case 'd':
			dig = 1;
			break;
		case 'l':
			length = cvtnum(optarg);
			break;
		case 'n':
			mode |= FALLOC_FL_KEEP_SIZE;
			break;
		case 'o':
			offset = cvtnum(optarg);
			break;
		case 'p':
			mode |= FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE;
			break;
		case 'z':
			mode |= FALLOC_FL_ZERO_RANGE;
			break;
		case 'v':
			verbose++;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		default:
			usage(stderr);
			break;
		}
	}

	if (optind == argc)
		errx(EXIT_FAILURE, _("no filename specified"));

	filename = argv[optind++];

	if (optind != argc)
		errx(EXIT_FAILURE, _("unexpected number of arguments"));

	if (dig) {
		/* for --dig-holes the default is analyze all file */
		if (length == -2LL)
			length = 0;
		if (length < 0)
			errx(EXIT_FAILURE, _("invalid length value specified"));
	} else {
		/* it's safer to require the range specification (--length --offset) */
		if (length == -2LL)
			errx(EXIT_FAILURE, _("no length argument specified"));
		if (length <= 0)
			errx(EXIT_FAILURE, _("invalid length value specified"));
	}
	if (offset < 0)
		errx(EXIT_FAILURE, _("invalid offset value specified"));

	/* O_CREAT makes sense only for the default fallocate(2) behavior
	 * when mode is no specified and new space is allocated */
	fd = open(filename, O_RDWR | (!dig && !mode ? O_CREAT : 0),
		  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
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
