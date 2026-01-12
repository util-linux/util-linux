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
	fputsln(_(" --source, -s filename       source filename"), out);
	fputsln(_(" --destination, -d filename  destination filename"), out);
	fputsln(_(" --ranges, -r filename       read range(s) seperated by newlines from filename"), out);

	fputs(USAGE_SEPARATOR, out);
	fprintf(out, USAGE_HELP_OPTIONS(16));

	fprintf(out, USAGE_MAN_TAIL("copyfilerange(1)"));
	exit(EXIT_SUCCESS);
}

static inline int conv_str_to_offset(char **str, intmax_t **value)
{
	char *end = NULL;
	intmax_t tmp;
	errno = 0;

	tmp = strtoimax(*str, &end, 10);
	if (errno == 0 && ((tmp && tmp < 0) ||
			(tmp && tmp > SINT_MAX(off_t)))) {
		return -ERANGE;
	} else if (end && *end != '\0') {
		return -EINVAL;
	}
	**value = (off_t) tmp;

	return 0;
}

static int parse_range(const char *range_str, off_t *src_off, off_t *dst_off, size_t *len)
{
	char *copy = NULL, *start = NULL, *token = NULL, rc = 0;
	copy = xstrdup(range_str);
	if (!copy) return -1;

	start = copy;
	token = strchr(start, ':');
	if (!token) goto fail;
	*token = '\0';
	if (*start) {
		rc = conv_str_to_offset(&start, &src_off);
		if (rc) goto fail;
	}

	start = token + 1;
	token = strchr(start, ':');
	if (!token) goto fail;
	*token = '\0';
	if (*start) {
		rc = conv_str_to_offset(&start, &dst_off);
		if (rc) goto fail;
	}

	start = token + 1;
	if (*start) {
		uintmax_t tmp;
		char *end = NULL;
		tmp = strtoumax(start, &end, 10);
		if (tmp > SIZE_MAX
						|| errno != 0 || (end && *end != '\0'))
				goto fail;
		*len = (size_t) tmp;
	} else *len = 0;

	free(copy);
	return 0;

fail:
	free(copy);
	return -1;

}

static int handle_range(int fd_src, int fd_dst, off_t* src_off, off_t* dst_off, char* range)
{
	size_t len;
	int rc = 0;
	if (parse_range(range, src_off, dst_off, &len) != 0) {
		fprintf(stderr, _("invalid range format: %s\n"), range);
		return 1;
	}

	if (len == 0) {
		struct stat sb;
		if (fstat(fd_src, &sb) == -1)
			err(EXIT_FAILURE, _("cannot determine size of source file"));
		len = sb.st_size - *src_off;
	}

	size_t remaining = len;
	while (remaining > 0) {
		ssize_t copied = copy_file_range(fd_src, src_off, fd_dst, dst_off, remaining, 0);
		if (copied < 0) {
			fprintf(stderr, _("failed copy file range %s at source offset %"PRId64": %m\n"), range, *src_off);
			rc |= 2;
			break;
		}
		if (copied == 0) break;
		remaining -= copied;
	}
	return rc;
}

int main(int argc, char **argv)
{
	char **range_files = NULL;
	size_t nrange_files = 0;
	char *source_filename = NULL;
	char *destination_filename = NULL;
	int fd_src, fd_dst;
	int rc = 0;

	static const struct option longopts[] = {
		{ "source",      required_argument, NULL, 's' },
		{ "destination", required_argument, NULL, 'd' },
		{ "ranges",      required_argument, NULL, 'r' },
		{ "version",     no_argument,       NULL, 'V' },
		{ "help",        no_argument,       NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	int c;
	while ((c = getopt_long (argc, argv, "r:s:d:Vh", longopts, NULL)) != -1) {
		switch (c) {
		case 'r':
			if (!range_files)
				range_files = xmalloc(sizeof(char *) * argc);
			range_files[nrange_files++] = xstrdup(optarg);
			break;
		case 's':
			if (source_filename)
				errx(EXIT_FAILURE, _("only one source file is allowed (%s already supplied)"), source_filename);
			source_filename = xstrdup(optarg);
			break;
		case 'd':
			if (destination_filename)
				errx(EXIT_FAILURE, _("only one destination file is allowed (%s already supplied)"), destination_filename);
			destination_filename = xstrdup(optarg);
			break;
		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		}
	}

	int rem_optind;
	for (rem_optind = optind; rem_optind < argc; rem_optind++) {
		if (source_filename && destination_filename) break;
		if (!source_filename)
			source_filename = xstrdup(argv[rem_optind]);
		else if (!destination_filename)
			destination_filename = xstrdup(argv[rem_optind]);
	}

	if (!source_filename)
		errx(EXIT_FAILURE, _("source file is required"));

	if (!destination_filename)
		errx(EXIT_FAILURE, _("destination file is required"));

	fd_src = open(source_filename, O_RDONLY);
	if (fd_src < 0)
		err(EXIT_FAILURE, _("cannot open source %s"), argv[1]);
	free(source_filename);

	fd_dst = open(destination_filename, O_WRONLY | O_CREAT, 0666);
	if (fd_dst < 0)
		err(EXIT_FAILURE, _("cannot open destination %s"), argv[2]);
	free(destination_filename);

	off_t src_off, dst_off;

	for (size_t i = 0; i < nrange_files; i++) {
		FILE *f = NULL;

		if (!(f = fopen(range_files[i], "r")))
			err(EXIT_FAILURE, _("cannot open range file %s"), range_files[i]);
		free(range_files[i]);

		char *line = NULL;
		size_t len = 0;

		while (getline(&line, &len, f) != -1) {
			ltrim_whitespace((unsigned char *) line);
			len = rtrim_whitespace((unsigned char *) line);
			if (len == 0) {
				rc++;
				continue;
			}
			rc |= handle_range(fd_src, fd_dst, &src_off, &dst_off, line);
		}

		free(line);
		fclose(f);
	}
	free(range_files);

	for (; rem_optind < argc; rem_optind++) {
		rc |= handle_range(fd_src, fd_dst, &src_off, &dst_off, argv[rem_optind]);
	}

	close(fd_src);
	close(fd_dst);
	return rc;
}
