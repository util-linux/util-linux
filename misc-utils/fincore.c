/*
 * fincore - count pages of file contents in core
 *
 * Copyright (C) 2017 Red Hat, Inc. All rights reserved.
 * Written by Masatake YAMATO <yamato@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "c.h"
#include "nls.h"
#include "closestream.h"

/* For large files, mmap is called in iterative way.
   Window is the unit of vma prepared in each mmap
   calling.

   Window size depends on page size.
   e.g. 128MB on x86_64. ( = N_PAGES_IN_WINDOW * 4096 ). */
#define N_PAGES_IN_WINDOW (32 * 1024)

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	const char *p = program_invocation_short_name;

	if (!*p)
		p = "fincore";

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] file...\n"), program_invocation_short_name);
	fputs(USAGE_OPTIONS, out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("fincore(1)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void report_count (const char *name, off_t file_size, off_t count_incore)
{
	printf ("%-10lu %-10lu %s\n", count_incore, file_size, name);
}

static void report_failure (const char *name)
{
	printf ("%-10s %-10ld %s\n", "failed", -1L, name);
}

static int do_mincore (void *window, const size_t len,
		       const char *name, const int pagesize,
		       off_t *count_incore)
{
	static unsigned char vec[N_PAGES_IN_WINDOW];
	int n = (len / pagesize) + ((len % pagesize)? 1: 0);

	if (mincore (window, len, vec) < 0) {
		warn(_("failed to do mincore: %s"), name);
		return -errno;
	}

	while (n > 0)
	{
		if (vec[--n] & 0x1)
		{
			vec[n] = 0;
			(*count_incore)++;
		}
	}

	return 0;
}

static int fincore_fd (int fd,
		       const char *name, const int pagesize,
		       off_t file_size,
		       off_t *count_incore)
{
	size_t window_size = N_PAGES_IN_WINDOW * pagesize;
	off_t  file_offset;
	void  *window = NULL;
	int rc = 0;
	int warned_once = 0;

	for (file_offset = 0; file_offset < file_size; file_offset += window_size) {
		size_t len;

		len = file_size - file_offset;
		if (len >= window_size)
			len = window_size;

		window = mmap(window, len, PROT_NONE, MAP_PRIVATE, fd, file_offset);
		if (window == MAP_FAILED) {
			if (!warned_once) {
				rc = -EINVAL;
				warn(_("failed to do mmap: %s"), name);
				warned_once = 1;
			}
			break;
		}

		rc = do_mincore (window, len, name, pagesize, count_incore);
		if (rc)
			break;

		munmap (window, len);
	}

	return rc;
}

static int fincore_name (const char *name,
			 const int pagesize, struct stat *sb,
			 off_t *count_incore)
{
	int fd;
	int rc = 0;

	if ((fd = open (name, O_RDONLY)) < 0) {
		warn(_("failed to open: %s"), name);
		return 0;
	}

	if (fstat (fd, sb) < 0) {
		warn(_("failed to do fstat: %s"), name);
		return -errno;
	}

	/* Empty file is no error */
	if (sb->st_size)
		rc = fincore_fd (fd, name, pagesize, sb->st_size, count_incore);

	close (fd);
	return rc;
}

int main(int argc, char ** argv)
{
	int c;
	int pagesize;
	int rc = EXIT_SUCCESS;

	static const struct option longopts[] = {
		{ "version",    no_argument, NULL, 'V' },
		{ "help",	no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long (argc, argv, "Vh", longopts, NULL)) != -1) {
		switch (c) {
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout);
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (optind == argc) {
		warnx(_("no file specified"));
		errtryhelp(EXIT_FAILURE);
	}


	pagesize = getpagesize();

	for(; optind < argc; optind++) {
		char *name = argv[optind];
		struct stat sb;
		off_t count_incore = 0;

		if (fincore_name (name, pagesize, &sb, &count_incore) == 0)
			report_count (name, sb.st_size, count_incore);
		else {
			report_failure (name);
			rc = EXIT_FAILURE;
		}
	}

	return rc;
}
