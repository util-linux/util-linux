/*
 * Copyright (C) 2016 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <getopt.h>

#include "c.h"
#include "nls.h"
#include "strutils.h"
#include "xalloc.h"

#include "libsmartcols.h"

struct column_flag {
	const char *name;
	int mask;
};

static const struct column_flag flags[] = {
	{ "trunc",	SCOLS_FL_TRUNC },
	{ "tree",	SCOLS_FL_TREE },
	{ "right",	SCOLS_FL_RIGHT },
	{ "strictwidth",SCOLS_FL_STRICTWIDTH },
	{ "noextremes", SCOLS_FL_NOEXTREMES },
	{ "hidden",	SCOLS_FL_HIDDEN },
	{ "wrap",	SCOLS_FL_WRAP },
	{ "wrapnl",	SCOLS_FL_WRAPNL },
	{ "none",	0 }
};

static long name_to_flag(const char *name, long unsigned int namesz)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(flags); i++) {
		const char *cn = flags[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return flags[i].mask;
	}
	warnx(_("unknown flag: %s"), name);
	return -1;
}

static int parse_column_flags(char *str)
{
	unsigned long flags = 0;

	if (string_to_bitmask(str, &flags, name_to_flag))
		err(EXIT_FAILURE, "failed to parse column flags");

	return flags;
}

static struct libscols_column *parse_column(FILE *f)
{
	size_t len = 0;
	char *line = NULL;
	int nlines = 0;

	struct libscols_column *cl = NULL;

	while (getline(&line, &len, f) != -1) {

		char *p = strrchr(line, '\n');
		if (p)
			*p = '\0';

		switch (nlines) {
		case 0: /* NAME */
		{
			struct libscols_cell *hr;

			cl = scols_new_column();
			if (!cl)
				goto fail;
			hr = scols_column_get_header(cl);
			if (!hr || scols_cell_set_data(hr, line))
				goto fail;
			break;
		}
		case 1: /* WIDTH-HINT */
		{
			double whint = strtod_or_err(line, "failed to parse column whint");
			if (scols_column_set_whint(cl, whint))
				goto fail;
			break;
		}
		case 2: /* FLAGS */
		{
			int flags = parse_column_flags(line);
			if (scols_column_set_flags(cl, flags))
				goto fail;
			break;
		}
		case 3: /* COLOR */
			if (scols_column_set_color(cl, line))
				goto fail;
			break;
		default:
			break;
		}

		nlines++;
	}

	return cl;
fail:
	scols_unref_column(cl);
	return NULL;
}

static int parse_column_data(FILE *f, struct libscols_table *tb, int column)
{
	size_t len = 0, nlines = 0;
	int i;
	char *str = NULL;

	while ((i = getline(&str, &len, f)) != -1) {

		struct libscols_line *ln;
		char *p = strrchr(str, '\n');
		if (p)
			*p = '\0';

		while ((p = strrchr(str, '\\')) && *(p + 1) == 'n') {
			*p = '\n';
			memmove(p + 1, p + 2, i - (p + 2 - str));
		}

		ln = scols_table_get_line(tb, nlines++);
		if (!ln)
			break;
		scols_line_set_data(ln, column, str);
	}

	return 0;

}

int main(int argc, char *argv[])
{
	struct libscols_table *tb;
	int c, n, nlines = 0;

	static const struct option longopts[] = {
		{ "maxout", 0, 0, 'm' },
		{ "column", 1, 0, 'c' },
		{ "nlines", 1, 0, 'n' },
		{ "width",  1, 0, 'w' },
		{ NULL, 0, 0, 0 },
	};

	setlocale(LC_ALL, "");	/* just to have enable UTF8 chars */

	scols_init_debug(0);

	tb = scols_new_table();
	if (!tb)
		err(EXIT_FAILURE, "failed to create output table");

	while((c = getopt_long(argc, argv, "c:mn:w:", longopts, NULL)) != -1) {
		switch(c) {
		case 'c': /* add column from file */
		{
			struct libscols_column *cl;
			FILE *f = fopen(optarg, "r");

			if (!f)
				err(EXIT_FAILURE, "%s: open failed", optarg);
			cl = parse_column(f);
			if (cl && scols_table_add_column(tb, cl))
				err(EXIT_FAILURE, "%s: failed to add column", optarg);
			scols_unref_column(cl);
			fclose(f);
			break;
		}
		case 'm':
			scols_table_enable_maxout(tb, TRUE);
			break;
		case 'n':
			nlines = strtou32_or_err(optarg, "failed to parse number of lines");
			break;
		case 'w':
			scols_table_set_termforce(tb, SCOLS_TERMFORCE_ALWAYS);
			scols_table_set_termwidth(tb, strtou32_or_err(optarg, "failed to parse terminal width"));
			break;
		}
	}

	if (nlines <= 0)
		errx(EXIT_FAILURE, "--nlines not set");

	for (n = 0; n < nlines; n++) {
		struct libscols_line *ln = scols_new_line();

		if (!ln || scols_table_add_line(tb, ln))
			err(EXIT_FAILURE, "failed to add a new line");
	}

	n = 0;

	while (optind < argc) {
		FILE *f = fopen(argv[optind], "r");

		if (!f)
			err(EXIT_FAILURE, "%s: open failed", argv[optind]);

		parse_column_data(f, tb, n);
		optind++;
		n++;
	}

	scols_table_enable_colors(tb, isatty(STDOUT_FILENO));

	scols_print_table(tb);
	scols_unref_table(tb);
	return EXIT_SUCCESS;
}
