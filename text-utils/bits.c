/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (c) 2024 Robin Jarry
 *
 * bits - convert bit masks from/to various formats
 */

#include <errno.h>
#include <getopt.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "c.h"
#include "closestream.h"
#include "cpuset.h"
#include "nls.h"
#include "strutils.h"
#include "strv.h"
#include "xalloc.h"

static void parse_mask_or_list(const char *cmdline_arg,
		cpu_set_t *all_bits, size_t width)
{
	cpu_set_t *bits, *copy;
	char bitwise_op = '|';
	const char *arg;
	size_t size, n;

	arg = cmdline_arg;

	/* strip optional operator first */
	if (startswith(arg, "&")) {
		bitwise_op = '&';
		arg++;
	} else if (startswith(arg, "^")) {
		bitwise_op = '^';
		arg++;
	} else if (startswith(arg, "~")) {
		bitwise_op = '~';
		arg++;
	} else if (startswith(arg, "|")) {
		arg++;
	}

	bits = cpuset_alloc(width, &size, NULL);
	if (bits == NULL)
		errx(EXIT_FAILURE, _("error: cannot allocate bit mask"));

	if (startswith(arg, ",") || startswith(arg, "0x")) {
		if (startswith(arg, ","))
			arg++;
		if (cpumask_parse(arg, bits, size) < 0)
			errx(EXIT_FAILURE, _("error: invalid bit mask: %s"), cmdline_arg);
	} else {
		if (cpulist_parse(arg, bits, size, 1) < 0)
			errx(EXIT_FAILURE, _("error: invalid bit list: %s"), cmdline_arg);
	}

	/* truncate all bits beyond the requested mask size */
	for (n = cpuset_nbits(size) - 1; n >= width; n--)
		CPU_CLR_S(n, size, bits);

	copy = cpuset_alloc(width, &size, NULL);
	if (copy == NULL)
		errx(EXIT_FAILURE, _("error: cannot allocate bit mask"));
	memcpy(copy, all_bits, size);

	switch (bitwise_op) {
	case '&':
		CPU_AND_S(size, all_bits, copy, bits);
		break;
	case '|':
		CPU_OR_S(size, all_bits, copy, bits);
		break;
	case '^':
		CPU_XOR_S(size, all_bits, copy, bits);
		break;
	case '~':
		for (n = 0; n < width; n++) {
			if (CPU_ISSET_S(n, size, bits))
				CPU_CLR_S(n, size, all_bits);
		}
		break;
	}

	cpuset_free(bits);
	cpuset_free(copy);
}

static size_t num_digits(size_t value)
{
	size_t digits = 1;
	while (value > 9) {
		digits++;
		value /= 10;
	}
	return digits;
}

enum output_mode {
	MODE_BINARY,
	MODE_GROUPED_MASK,
	MODE_LIST,
	MODE_MASK,
};

static void print_bits(cpu_set_t *bits, size_t width, enum output_mode mode)
{
	const size_t size = CPU_ALLOC_SIZE(width);
	bool started = false;
	char *buf = NULL;
	size_t buf_size;
	ssize_t n = 0;

	if (CPU_COUNT_S(size, bits) == 0) {
		switch (mode) {
		case MODE_MASK:
			printf("0x0\n");
			break;
		case MODE_GROUPED_MASK:
			printf("0\n");
			break;
		case MODE_BINARY:
			printf("0b0\n");
			break;
		case MODE_LIST:
			break;
		}
		return;
	}

	switch (mode) {
	case MODE_MASK:
		/* fit 4 bits per character plus terminating nul byte */
		buf_size = ((cpuset_nbits(size) + 3) / 4) + 1;
		buf = xmalloc(buf_size);
		cpumask_create(buf, buf_size, bits, size);

		/* strip leading zeroes */
		while (buf[n] == '0')
			n++;
		printf("0x%s\n", buf + n);
		break;

	case MODE_GROUPED_MASK:
		/* fit 4 bits per character plus terminating nul byte */
		buf_size = ((cpuset_nbits(size) + 3) / 4) + 1;
		buf = xmalloc(buf_size);
		cpumask_create(buf, buf_size, bits, size);

		/* strip leading zeroes */
		while (buf[n] == '0')
			n++;

		while (buf[n] != '\0') {
			if (started && (n % 8) == 0)
				printf(",");
			if (buf[n] != '0')
				started = true;
			printf("%c", buf[n]);
			n++;
		}
		printf("\n");
		break;

	case MODE_BINARY:
		printf("0b");
		for (n = width - 1; n >= 0; n--) {
			if (started && ((n + 1) % 4) == 0)
				printf("_");
			if (CPU_ISSET_S(n, size, bits)) {
				started = true;
				printf("1");
			} else if (started) {
				printf("0");
			}
		}
		printf("\n");
		break;

	case MODE_LIST:
		/* Maximum number of digits (larger bit number) plus 1
		 * to account for a separating comma, times the number of bits
		 * set to 1. */
		buf_size = (num_digits(width - 1) + 1) * CPU_COUNT_S(size, bits);
		buf = xmalloc(buf_size);
		cpulist_create(buf, buf_size, bits, size);
		printf("%s\n", buf);
		break;
	}

	free(buf);
}

static void __attribute__((__noreturn__)) usage(void)
{
	fputs(USAGE_HEADER, stdout);
	fprintf(stdout, _(" %s [options] [<mask_or_list>...]\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, stdout);
	fputsln(_("Convert bit masks from/to various formats."), stdout);

	fputs(USAGE_ARGUMENTS, stdout);
	fputsln(_(" <mask_or_list>      bits specified as a hex mask (e.g. 0xeec2)\n"
		  "                       or as a comma-separated list of bit IDs"),
		stdout);
	fputs(USAGE_SEPARATOR, stdout);
	fputsln(_(" If not specified, arguments will be read from stdin."),
		stdout);

	fputs(USAGE_OPTIONS, stdout);
	fprintf(stdout, USAGE_HELP_OPTIONS(21));
	fputsln(_(" -w <num>, --width <num>\n"
	          "                     maximum width of bit masks (default 8192)"),
		stdout);

	fputs(_("\nOutput modes:\n"), stdout);
	fputsln(_(" -m, --mask          display bits as a hex mask value (default)"),
		stdout);
	fputsln(_(" -g, --grouped-mask  display bits as a hex mask value in 32bit\n"
		  "                       comma separated groups"), stdout);
	fputsln(_(" -b, --binary        display bits as a binary mask value"),
		stdout);
	fputsln(_(" -l, --list          display bits as a compressed list of bit IDs"),
		stdout);

	fprintf(stdout, USAGE_MAN_TAIL("bits(1)"));
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	enum output_mode mode = MODE_MASK;
	char **stdin_lines = NULL;
	cpu_set_t *bits = NULL;
	size_t width = 8192;
	size_t alloc_size;
	int c;

#define FLAGS "Vhw:mgbl"
	static const struct option longopts[] = {
		{ "version",      no_argument,       NULL, 'V' },
		{ "help",         no_argument,       NULL, 'h' },
		{ "width",        required_argument, NULL, 'w' },
		{ "mask",         no_argument,       NULL, 'm' },
		{ "grouped-mask", no_argument,       NULL, 'g' },
		{ "binary",       no_argument,       NULL, 'b' },
		{ "list",         no_argument,       NULL, 'l' },
		{ NULL,           0,                 NULL,  0  }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	while ((c = getopt_long(argc, argv, FLAGS, longopts, NULL)) != -1) {
		switch (c) {
		case 'm':
			mode = MODE_MASK;
			break;
		case 'g':
			mode = MODE_GROUPED_MASK;
			break;
		case 'b':
			mode = MODE_BINARY;
			break;
		case 'l':
			mode = MODE_LIST;
			break;
		case 'w':
			/* allow up to 128k masks */
			width = str2unum_or_err(optarg,
				10, _("invalid --width"), 128 * 1024);
			break;
		case 'V':
			print_version(EXIT_SUCCESS);
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	argc -= optind;
	argv += optind;
	if (argc == 0) {
		/* no arguments provided, read lines from stdin */
		char buf[LINE_MAX];

		while (fgets(buf, sizeof(buf), stdin)) {
			/* strip LF, CR, CRLF, LFCR */
			rtrim_whitespace((unsigned char *)buf);
			if (strv_push(&stdin_lines, xstrdup(buf)) < 0)
				errx(EXIT_FAILURE, _("cannot allocate memory"));
		}

		argc = strv_length(stdin_lines);
		argv = stdin_lines;
	}

	bits = cpuset_alloc(width, &alloc_size, NULL);
	if (bits == NULL)
		errx(EXIT_FAILURE, _("cannot allocate memory"));

	/* start off with all bits set to 0 */
	memset(bits, 0, alloc_size);

	for (; argc > 0; argc--, argv++)
		parse_mask_or_list(*argv, bits, width);

	strv_free(stdin_lines);

	print_bits(bits, width, mode);

	cpuset_free(bits);

	return EXIT_SUCCESS;
}
