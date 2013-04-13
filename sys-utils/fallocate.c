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
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>

#ifndef HAVE_FALLOCATE
# include <sys/syscall.h>
#endif

#ifdef HAVE_LINUX_FALLOC_H
# include <linux/falloc.h>	/* for FALLOC_FL_* flags */
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

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] <filename>\n"), program_invocation_short_name);
	fputs(USAGE_OPTIONS, out);
	fputs(_(" -n, --keep-size     don't modify the length of the file\n"
		" -p, --punch-hole    punch holes in the file\n"
		" -o, --offset <num>  offset of the allocation, in bytes\n"
		" -l, --length <num>  length of the allocation, in bytes\n"), out);
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

int main(int argc, char **argv)
{
	char	*fname;
	int	c;
	int	error;
	int	fd;
	int	mode = 0;
	loff_t	length = -2LL;
	loff_t	offset = 0;

	static const struct option longopts[] = {
	    { "help",      0, 0, 'h' },
	    { "version",   0, 0, 'V' },
	    { "keep-size", 0, 0, 'n' },
	    { "punch-hole", 0, 0, 'p' },
	    { "offset",    1, 0, 'o' },
	    { "length",    1, 0, 'l' },
	    { NULL,        0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "hVnpl:o:", longopts, NULL)) != -1) {
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
		case 'l':
			length = cvtnum(optarg);
			break;
		case 'o':
			offset = cvtnum(optarg);
			break;
		default:
			usage(stderr);
			break;
		}
	}

	if (length == -2LL)
		errx(EXIT_FAILURE, _("no length argument specified"));
	if (length <= 0)
		errx(EXIT_FAILURE, _("invalid length value specified"));
	if (offset < 0)
		errx(EXIT_FAILURE, _("invalid offset value specified"));
	if (optind == argc)
		errx(EXIT_FAILURE, _("no filename specified."));

	fname = argv[optind++];

	if (optind != argc) {
		warnx(_("unexpected number of arguments"));
		usage(stderr);
	}

	fd = open(fname, O_WRONLY|O_CREAT, 0644);
	if (fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), fname);

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
			errx(EXIT_FAILURE,
				_("keep size mode (-n option) unsupported"));
		err(EXIT_FAILURE, _("%s: fallocate failed"), fname);
	}

	if (close_fd(fd) != 0)
		err(EXIT_FAILURE, _("write failed: %s"), fname);
	return EXIT_SUCCESS;
}
