/*
 * copyfilerange - utility to use the copy_file_range(2) system call
 *
 * Copyright (C) 2025 Dick Marinus <dick@mrns.nl>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/stat.h>

#include "c.h"
#include "nls.h"
#include "closestream.h"
#include "xalloc.h"

static int verbose;

struct rangeitem {
	const char *in_filename;
	const char *out_filename;

	off_t in_st_size;

	int in_fd;
	int out_fd;

	off_t in_offset;
	off_t out_offset;

	uintmax_t length;
};

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out,
		_(" %1$s [options] [<source>] [<destination>] [<range>...]\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputsln(_("Copy file ranges from source to destination file."), out);

	fputs(USAGE_SEPARATOR, out);
	fputsln(_(" source                      source filename"), out);
	fputsln(_(" destination                 destination filename"), out);
	fputsln(_(" range                       source_offset:dest_offset:length, all values are in bytes"), out);
	fputsln(_("                             if length is set to 0 as much as available will be copied"), out);
	fputsln(_("                             when the offset is omitted the last file position is used"), out);

	fputs(USAGE_OPTIONS, out);
	fputsln(_(" --ranges, -r filename       read range(s) separated by newlines from filename"), out);
	fputsln(_(" --verbose, -v               verbose mode"), out);

	fputs(USAGE_SEPARATOR, out);
	fprintf(out, USAGE_HELP_OPTIONS(16));

	fprintf(out, USAGE_MAN_TAIL("copyfilerange(1)"));
	exit(EXIT_SUCCESS);
}

static int parse_range(const char *str, struct rangeitem *range)
{
	char *copy = NULL, *start = NULL, *token = NULL;
	int rc = 0;
	uintmax_t tmp;

	copy = xstrdup(str);
	if (!copy)
		return -1;

	start = copy;
	token = strchr(start, ':');
	if (!token)
		goto fail;
	*token = '\0';
	if (*start) {
		rc = strtosize(start, &tmp);
		if (rc || tmp >= SINT_MAX(off_t))
			goto fail;
		range->in_offset = (off_t)tmp;
	}

	start = token + 1;
	token = strchr(start, ':');
	if (!token)
		goto fail;
	*token = '\0';
	if (*start) {
		rc = strtosize(start, &tmp);
		if (rc || tmp >= SINT_MAX(off_t))
			goto fail;
		range->out_offset = (off_t)tmp;
	}

	start = token + 1;
	if (*start) {
		rc = strtosize(start, &tmp);
		if (rc || tmp >= SINT_MAX(off_t))
			goto fail;
		range->length = (off_t)tmp;
	} else
		range->length = 0;

	free(copy);
	return 0;

fail:
	free(copy);
	return -1;

}

static void copy_range(struct rangeitem *range) {
	uintmax_t remaining = range->length;

	if (range->in_offset > range->in_st_size)
		errx(EXIT_FAILURE, _("%s offset %"PRId64" beyond file size of %"PRId64""), range->in_filename, range->in_offset, range->in_st_size);

	while (remaining > 0) {
		const size_t chunk = remaining > SIZE_MAX ? SIZE_MAX : remaining;
		if (verbose)
			printf("copy_file_range %s to %s %"PRId64":%"PRId64":%zu\n", range->in_filename,
							range->out_filename, range->in_offset, range->out_offset, chunk);

		const ssize_t copied = copy_file_range(range->in_fd, &range->in_offset, range->out_fd,
																										&range->out_offset, chunk, 0);
		if (copied < 0)
			errx(EXIT_FAILURE, _("failed copy file range %"PRId64":%"PRId64":%ju from %s to %s with remaining %ju:%m\n"),
							range->in_offset, range->out_offset, range->length, range->in_filename, range->out_filename, remaining);
		if (copied == 0)
			break;

		remaining -= copied;
	}
}

static void handle_range(char* str, struct rangeitem *range)
{
	if (parse_range(str, range) != 0)
		errx(EXIT_FAILURE, _("invalid range format: %s\n"), str);

	if (!range->length)
		range->length = range->in_st_size - range->in_offset;

	copy_range(range);
}

static void handle_range_files(struct rangeitem *range, size_t nrange_files, char **range_files)
{
	for (size_t i = 0; i < nrange_files; i++) {
		FILE *f = fopen(range_files[i], "r");

		if (!f)
			err(EXIT_FAILURE, _("cannot open range file %s"), range_files[i]);

		char *line = NULL;
		size_t len = 0;

		while (getline(&line, &len, f) != -1) {
			ltrim_whitespace((unsigned char *) line);
			len = rtrim_whitespace((unsigned char *) line);
			if (len == 0)
				errx(EXIT_FAILURE, _("Empty line in range file %s is not allowed"), range_files[i]);
			handle_range(line, range);
		}

		free(range_files[i]);
		free(line);
		fclose(f);
	}
	free(range_files);
}

int main(const int argc, char **argv)
{
	char **range_files = NULL;
	size_t nrange_files = 0;
	struct stat sb;
	struct rangeitem range = {0};

	static const struct option longopts[] = {
		{ "ranges",      required_argument, NULL, 'r' },
		{ "verbose",     no_argument,       NULL, 'v' },
		{ "version",     no_argument,       NULL, 'V' },
		{ "help",        no_argument,       NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	int c;
	while ((c = getopt_long (argc, argv, "r:vVh", longopts, NULL)) != -1) {
		switch (c) {
		case 'r':
			if (!range_files)
				range_files = xmalloc(sizeof(char *) * argc);
			range_files[nrange_files++] = xstrdup(optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		}
	}

	int rem_optind;
	for (rem_optind = optind; rem_optind < argc; rem_optind++) {
		if (range.in_filename && range.out_filename)
			break;
		if (!range.in_filename)
			range.in_filename = argv[rem_optind];
		else if (!range.out_filename)
			range.out_filename = argv[rem_optind];
	}

	if (!range.in_filename)
		errx(EXIT_FAILURE, _("source file is required"));

	if (!range.out_filename)
		errx(EXIT_FAILURE, _("destination file is required"));

	range.in_fd = open(range.in_filename, O_RDONLY);
	if (range.in_fd < 0)
		err(EXIT_FAILURE, _("cannot open source %s"), range.in_filename);


	if (fstat(range.in_fd, &sb) == -1)
		err(EXIT_FAILURE, _("cannot determine size of source file %s"), range.in_filename);
	range.in_st_size = sb.st_size;

	range.out_fd = open(range.out_filename, O_WRONLY | O_CREAT, 0666);
	if (range.out_fd < 0)
		err(EXIT_FAILURE, _("cannot open destination %s"), argv[2]);

	if (rem_optind == argc && !nrange_files)
		errx(EXIT_FAILURE, _("nothing to do, no ranges supplied"));

	if (nrange_files)
		handle_range_files(&range, nrange_files, range_files);

	for (; rem_optind < argc; rem_optind++) {
		handle_range(argv[rem_optind], &range);
	}

	close(range.in_fd);
	close(range.out_fd);
	return 0;
}
