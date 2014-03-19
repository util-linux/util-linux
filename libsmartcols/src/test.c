/*
 * TT - Table or Tree, features:
 * - column width could be defined as absolute or relative to the terminal width
 * - allows to truncate or wrap data in columns
 * - prints tree if parent->child relation is defined
 * - draws the tree by ASCII or UTF8 lines (depends on terminal setting)
 *
 * Copyright (C) 2010-2014 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>

#include "c.h"
#include "nls.h"
#include "widechar.h"
#include "mbsalign.h"
#include "ttyutils.h"
#include "colors.h"

#include "libsmartcols.h"

enum { MYCOL_NAME, MYCOL_FOO, MYCOL_BAR, MYCOL_PATH };

int main(int argc, char *argv[])
{
	struct libscols_table *tb;
	struct libscols_column *cl;
	int flags = 0, notree = 0, clone = 0, i, color = 0;

	if (argc == 2 && !strcmp(argv[1], "--help")) {
		printf("%s [--ascii | --raw | --list | --clone | --clonetree]\n",
				program_invocation_short_name);
		return EXIT_SUCCESS;
	} else if (argc == 2 && !strcmp(argv[1], "--ascii")) {
		flags |= SCOLS_FL_ASCII;
	} else if (argc == 2 && !strcmp(argv[1], "--raw")) {
		flags |= SCOLS_FL_RAW;
		notree = 1;
	} else if (argc == 2 && !strcmp(argv[1], "--export")) {
		flags |= SCOLS_FL_EXPORT;
		notree = 1;
	} else if (argc == 2 && !strcmp(argv[1], "--list")) {
		notree = 1;
	} else if (argc == 2 && !strcmp(argv[1], "--color")) {
		notree = 1;
		color = 1;
	} else if (argc == 2 && !strcmp(argv[1], "--colortree")) {
		notree = 0;
		color = 1;
	} else if (argc == 2 && !strcmp(argv[1], "--clone")) {
		notree = 1;
		clone = 1;
	} else if (argc == 2 && !strcmp(argv[1], "--clonetree")) {
		notree = 0;
		clone = 1;
	}

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	tb = scols_new_table(flags, NULL);
	if (!tb)
		err(EXIT_FAILURE, "table initialization failed");

	cl = scols_table_new_column(tb, "NAME", 0.3, notree ? 0 : SCOLS_FL_TREE);
	if (color)
		scols_column_set_color(cl, UL_COLOR_RED);

	cl = scols_table_new_column(tb, "FOO", 0.3, SCOLS_FL_TRUNC);
	if (color) {
		struct libscols_cell *h = scols_column_get_header(cl);

		scols_column_set_color(cl, UL_COLOR_BOLD_GREEN);
		scols_cell_set_color(h, UL_COLOR_GREEN);
	}
	scols_table_new_column(tb, "BAR", 0.3, 0);
	scols_table_new_column(tb, "PATH", 0.3, 0);

	for (i = 0; i < 2; i++) {
		struct libscols_line *ln = scols_table_new_line(tb, NULL);
		struct libscols_line *pr, *root = ln;

		scols_line_set_data(ln, MYCOL_NAME, "AAA");
		scols_line_set_data(ln, MYCOL_FOO, "a-foo-foo");
		scols_line_set_data(ln, MYCOL_BAR, "barBar-A");
		scols_line_set_data(ln, MYCOL_PATH, "/mnt/AAA");

		pr = ln = scols_table_new_line(tb, root);
		scols_line_set_data(ln, MYCOL_NAME, "AAA.A");
		scols_line_set_data(ln, MYCOL_FOO, "a.a-foo-foo");
		scols_line_set_data(ln, MYCOL_BAR, "barBar-A.A");
		scols_line_set_data(ln, MYCOL_PATH, "/mnt/AAA/A");
		if (color)
			scols_line_set_color(ln, UL_COLOR_BOLD_YELLOW);

		ln = scols_table_new_line(tb, pr);
		scols_line_set_data(ln, MYCOL_NAME, "AAA.A.AAA");
		scols_line_set_data(ln, MYCOL_FOO, "a.a.a-foo-foo");
		scols_line_set_data(ln, MYCOL_BAR, "barBar-A.A.A");
		scols_line_set_data(ln, MYCOL_PATH, "/mnt/AAA/A/AAA");

		ln = scols_table_new_line(tb, root);
		scols_line_set_data(ln, MYCOL_NAME, "AAA.B");
		scols_line_set_data(ln, MYCOL_FOO, "a.b-foo-foo");
		scols_line_set_data(ln, MYCOL_BAR, "barBar-A.B");
		scols_line_set_data(ln, MYCOL_PATH, "/mnt/AAA/B");
		if (color)
			scols_cell_set_color(scols_line_get_cell(ln, MYCOL_FOO),
					     UL_COLOR_MAGENTA);

		ln = scols_table_new_line(tb, pr);
		scols_line_set_data(ln, MYCOL_NAME, "AAA.A.BBB");
		scols_line_set_data(ln, MYCOL_FOO, "a.a.b-foo-foo");
		scols_line_set_data(ln, MYCOL_BAR, "barBar-A.A.BBB");
		scols_line_set_data(ln, MYCOL_PATH, "/mnt/AAA/A/BBB");

		ln = scols_table_new_line(tb, pr);
		scols_line_set_data(ln, MYCOL_NAME, "AAA.A.CCC");
		scols_line_set_data(ln, MYCOL_FOO, "a.a.c-foo-foo");
		scols_line_set_data(ln, MYCOL_BAR, "barBar-A.A.CCC");
		scols_line_set_data(ln, MYCOL_PATH, "/mnt/AAA/A/CCC");

		ln = scols_table_new_line(tb, root);
		scols_line_set_data(ln, MYCOL_NAME, "AAA.C");
		scols_line_set_data(ln, MYCOL_FOO, "a.c-foo-foo");
		scols_line_set_data(ln, MYCOL_BAR, "barBar-A.C");
		scols_line_set_data(ln, MYCOL_PATH, "/mnt/AAA/C");
	}

	printf("\nColumns: %d, Lines: %d\n\n",
			scols_table_get_ncols(tb),
			scols_table_get_nlines(tb));

	if (clone) {
		struct libscols_table *xtb = scols_copy_table(tb);
		scols_print_table(xtb);
		fputs("\n\n", stdout);
		scols_unref_table(xtb);
	}

	scols_print_table(tb);
	scols_unref_table(tb);

	return EXIT_SUCCESS;
}
