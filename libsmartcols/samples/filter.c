/*
 * Copyright (C) 2023 Karel Zak <kzak@redhat.com>
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


enum { COL_NAME, COL_NUM, COL_FLOAT, COL_STRING };

/* add columns to the @tb */
static void setup_columns(struct libscols_table *tb)
{
	struct libscols_column *col;

	col = scols_table_new_column(tb, "NAME", 0, 0);
	if (!col)
		goto fail;
	scols_column_set_json_type(col, SCOLS_JSON_STRING);

	col = scols_table_new_column(tb, "NUM", 0, 0);
	if (!col)
		goto fail;
	scols_column_set_json_type(col, SCOLS_JSON_NUMBER);

	col = scols_table_new_column(tb, "FLOAT", 0, 0);
	if (!col)
		goto fail;
	scols_column_set_json_type(col, SCOLS_JSON_FLOAT);

	col = scols_table_new_column(tb, "STRING", 0, 0);
	if (!col)
		goto fail;
	scols_column_set_json_type(col, SCOLS_JSON_STRING);

	return;
fail:
	scols_unref_table(tb);
	err(EXIT_FAILURE, "failed to create output columns");
}

static struct libscols_line *add_line(struct libscols_table *tb, int n, int empty)
{
	char *data = NULL;
	struct libscols_line *ln = scols_table_new_line(tb, NULL);
	if (!ln)
		err(EXIT_FAILURE, "failed to create output line");

	if (empty != 1) {
		xasprintf(&data, "#%d", n);
		if (scols_line_refer_data(ln, COL_NAME, data))
			goto fail;
	}
	if (empty != 2) {
		xasprintf(&data, "%d", n);
		if (scols_line_refer_data(ln, COL_NUM, data))
			goto fail;
	}
	if (empty != 3) {
		xasprintf(&data, "%d.%d", n, n);
		if (scols_line_refer_data(ln, COL_FLOAT, data))
			goto fail;
	}
	if (empty != 4) {
		xasprintf(&data, "str%dstr", n);
		if (scols_line_refer_data(ln, COL_STRING, data))
			goto fail;
	}
	return ln;
fail:
	scols_unref_table(tb);
	err(EXIT_FAILURE, "failed to create output line");
}

static struct libscols_filter *init_filter(struct libscols_table *tb, const char *query, int dump)
{
	struct libscols_iter *itr;
	struct libscols_filter *f = scols_new_filter(NULL);
	const char *name = NULL;
	int rc = 0;

	if (!f)
		err(EXIT_FAILURE, "failed to allocate filter");
	if (scols_filter_parse_string(f, query) != 0)
		errx(EXIT_FAILURE, "failed to parse filter: %s",
				scols_filter_get_errmsg(f));

	itr = scols_new_iter(SCOLS_ITER_FORWARD);
	if (!itr)
		err(EXIT_FAILURE, "failed to allocate iterator");

	while (scols_filter_next_holder(f, itr, &name, 0) == 0) {
		struct libscols_column *col;

		col = scols_table_get_column_by_name(tb, name);
		if (!col) {
			warnx("unknown column '%s' in filter", name);
			rc++;
			continue;
		}
		scols_filter_assign_column(f, itr, name, col);
	}

	scols_free_iter(itr);
	if (dump)
		scols_dump_filter(f, stdout);
	if (rc) {
		scols_unref_filter(f);
		f = NULL;
	}
	return f;
}

int main(int argc, char *argv[])
{
	struct libscols_table *tb;
	struct libscols_filter *fltr = NULL, *hlight = NULL;
	int c, i, json = 0, dump = 0;
	int rc = 0;
	char *f_query = NULL, *h_query = NULL;

	static const struct option longopts[] = {
		{ "json",      0, NULL, 'J' },
		{ "dump",      0, NULL, 'D' },
		{ "filter",    1, NULL, 'Q' },
		{ "highlight", 1, NULL, 'H' },
		{ "help",      0, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};

	setlocale(LC_ALL, "");	/* just to have enable UTF8 chars */

	scols_init_debug(0);

	tb = scols_new_table();
	if (!tb)
		err(EXIT_FAILURE, "failed to create output table");

	while((c = getopt_long(argc, argv, "H:DhJQ:", longopts, NULL)) != -1) {
		switch(c) {
		case 'h':
			printf("%s --help | --filter | --dump | --json]\n", program_invocation_short_name);
			break;
		case 'D':
			dump = 1;
			break;
		case 'J':
			json = 1;
			break;
		case 'Q':
			f_query = optarg;
			break;
		case 'H':
			h_query = optarg;
			break;
		}
	}

	scols_table_enable_json(tb, json);
	setup_columns(tb);

	if (f_query) {
		fltr = init_filter(tb, f_query, dump);
		if (!fltr)
			goto done;
	}
	if (h_query) {
		hlight = init_filter(tb, h_query, dump);
		if (!hlight)
			goto done;
		scols_table_enable_colors(tb, isatty(STDOUT_FILENO));
	}

	for (i = 0; i < 10; i++) {
		struct libscols_line *ln = add_line(tb, i + 1, i % 4);
		int rc, status = 0;

		if (fltr) {
			rc = scols_line_apply_filter(ln, fltr, &status);
			if (rc)
				goto done;
			if (status == 0) {
				scols_table_remove_line(tb, ln);
				continue;
			}
		}
		if (hlight) {
			rc = scols_line_apply_filter(ln, hlight, &status);
			if (rc)
				goto done;
			if (status)
				scols_line_set_color(ln, "red");
		}
	}

	scols_print_table(tb);
done:
	scols_unref_table(tb);
	scols_unref_filter(fltr);
	scols_unref_filter(hlight);
	return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
