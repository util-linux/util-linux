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
#include "optutils.h"

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
	{ "wrapnl",	SCOLS_FL_WRAP },
	{ "none",	0 }
};

static long name_to_flag(const char *name, size_t namesz)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(flags); i++) {
		const char *cn = flags[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return flags[i].mask;
	}
	warnx("unknown flag: %s", name);
	return -1;
}

static int parse_column_flags(char *str)
{
	unsigned long num_flags = 0;

	if (string_to_bitmask(str, &num_flags, name_to_flag))
		err(EXIT_FAILURE, "failed to parse column flags");

	return num_flags;
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
			int num_flags = parse_column_flags(line);
			if (scols_column_set_flags(cl, num_flags))
				goto fail;
			if (strcmp(line, "wrapnl") == 0) {
				scols_column_set_wrapfunc(cl,
						scols_wrapnl_chunksize,
						scols_wrapnl_nextchunk,
						NULL);
				scols_column_set_safechars(cl, "\n");
			}
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

	free(line);
	return cl;
fail:
	free(line);
	scols_unref_column(cl);
	return NULL;
}

static int parse_column_data(FILE *f, struct libscols_table *tb, int col)
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

		scols_line_set_data(ln, col, str);
	}

	free(str);
	return 0;

}

static struct libscols_line *get_line_with_id(struct libscols_table *tb,
						int col_id, const char *id)
{
	struct libscols_line *ln;
	struct libscols_iter *itr = scols_new_iter(SCOLS_ITER_FORWARD);

	while (scols_table_next_line(tb, itr, &ln) == 0) {
		struct libscols_cell *ce = scols_line_get_cell(ln, col_id);
		const char *data = ce ? scols_cell_get_data(ce) : NULL;

		if (data && strcmp(data, id) == 0)
			break;
	}

	scols_free_iter(itr);
	return ln;
}

static void compose_tree(struct libscols_table *tb, int parent_col, int id_col)
{
	struct libscols_line *ln;
	struct libscols_iter *itr = scols_new_iter(SCOLS_ITER_FORWARD);

	while (scols_table_next_line(tb, itr, &ln) == 0) {
		struct libscols_line *parent = NULL;
		struct libscols_cell *ce = scols_line_get_cell(ln, parent_col);
		const char *data = ce ? scols_cell_get_data(ce) : NULL;

		if (data)
			parent = get_line_with_id(tb, id_col, data);
		if (parent)
			scols_line_add_child(parent, ln);
	}

	scols_free_iter(itr);
}


static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fprintf(out,
		"\n %s [options] <column-data-file> ...\n\n", program_invocation_short_name);

	fputs(" -m, --maxout                   fill all terminal width\n", out);
	fputs(" -c, --column <file>            column definition\n", out);
	fputs(" -n, --nlines <num>             number of lines\n", out);
	fputs(" -J, --json                     JSON output format\n", out);
	fputs(" -r, --raw                      RAW output format\n", out);
	fputs(" -E, --export                   use key=\"value\" output format\n", out);
	fputs(" -C, --colsep <str>             set columns separator\n", out);
	fputs(" -w, --width <num>              hardcode terminal width\n", out);
	fputs(" -p, --tree-parent-column <n>   parent column\n", out);
	fputs(" -i, --tree-id-column <n>       id column\n", out);
	fputs(" -h, --help                     this help\n", out);
	fputs("\n", out);

	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	struct libscols_table *tb;
	int c, n, nlines = 0;
	int parent_col = -1, id_col = -1;

	static const struct option longopts[] = {
		{ "maxout", 0, NULL, 'm' },
		{ "column", 1, NULL, 'c' },
		{ "nlines", 1, NULL, 'n' },
		{ "width",  1, NULL, 'w' },
		{ "tree-parent-column", 1, NULL, 'p' },
		{ "tree-id-column",	1, NULL, 'i' },
		{ "json",   0, NULL, 'J' },
		{ "raw",    0, NULL, 'r' },
		{ "export", 0, NULL, 'E' },
		{ "colsep",  1, NULL, 'C' },
		{ "help",   0, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};

	static const ul_excl_t excl[] = {       /* rows and cols in ASCII order */
		{ 'E', 'J', 'r' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");	/* just to have enable UTF8 chars */
	scols_init_debug(0);

	tb = scols_new_table();
	if (!tb)
		err(EXIT_FAILURE, "failed to create output table");

	while((c = getopt_long(argc, argv, "hCc:Ei:Jmn:p:rw:", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

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
		case 'p':
			parent_col = strtou32_or_err(optarg, "failed to parse tree PARENT column");
			break;
		case 'i':
			id_col = strtou32_or_err(optarg, "failed to parse tree ID column");
			break;
		case 'J':
			scols_table_enable_json(tb, 1);
			scols_table_set_name(tb, "testtable");
			break;
		case 'm':
			scols_table_enable_maxout(tb, TRUE);
			break;
		case 'r':
			scols_table_enable_raw(tb, TRUE);
			break;
		case 'E':
			scols_table_enable_export(tb, TRUE);
			break;
		case 'C':
			scols_table_set_column_separator(tb, optarg);
			break;
		case 'n':
			nlines = strtou32_or_err(optarg, "failed to parse number of lines");
			break;
		case 'w':
			scols_table_set_termforce(tb, SCOLS_TERMFORCE_ALWAYS);
			scols_table_set_termwidth(tb, strtou32_or_err(optarg, "failed to parse terminal width"));
			break;
		case 'h':
			usage();
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (nlines <= 0)
		errx(EXIT_FAILURE, "--nlines not set");

	for (n = 0; n < nlines; n++) {
		struct libscols_line *ln = scols_new_line();

		if (!ln || scols_table_add_line(tb, ln))
			err(EXIT_FAILURE, "failed to add a new line");

		scols_unref_line(ln);
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

	if (scols_table_is_tree(tb) && parent_col >= 0 && id_col >= 0)
		compose_tree(tb, parent_col, id_col);

	scols_table_enable_colors(tb, isatty(STDOUT_FILENO));

	scols_print_table(tb);
	scols_unref_table(tb);
	return EXIT_SUCCESS;
}
