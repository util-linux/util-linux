/*
 * Copyright (C) 2010-2014 Karel Zak <kzak@redhat.com>
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
#include "randutils.h"

#include "libsmartcols.h"

static int opt_random;

enum { COL_NAME, COL_DATA, COL_LIKE };

/* add columns to the @tb */
static void setup_columns(struct libscols_table *tb)
{
	if (!scols_table_new_column(tb, "NAME", 0, SCOLS_FL_TREE))
		goto fail;
	if (!scols_table_new_column(tb, "DATA", 0, SCOLS_FL_WRAPNL))
		goto fail;
	if (!scols_table_new_column(tb, "LIKE", 0, SCOLS_FL_RIGHT))
		goto fail;
	return;
fail:
	scols_unref_table(tb);
	err(EXIT_FAILURE, "failed to create output columns");
}

static char *gen_text(const char *prefix, const char *sub_prefix, char *buf, size_t sz, int nl)
{
	int x = snprintf(buf, sz,  "%s-%s-", prefix, sub_prefix);
	int next_nl = -1;

	for ( ; (size_t)x < sz - 1; x++) {
		buf[x] = next_nl == 0 ? '\n' : *prefix;

		if (nl)
			next_nl--;
		if (nl && next_nl < 0)
			next_nl = opt_random ?
					(size_t) rand_get_number(1, sz / 2) :
					sz / 3;
	}

	buf[x++] = 'x';
	buf[x] = '\0';
	return buf;
}

static struct libscols_line * add_line(	struct libscols_table *tb,
					struct libscols_line *parent,
					const char *prefix)
{
	char buf[BUFSIZ];
	struct libscols_line *ln = scols_table_new_line(tb, parent);
	if (!ln)
		err(EXIT_FAILURE, "failed to create output line");

	if (scols_line_set_data(ln, COL_NAME, gen_text(prefix, "N", buf, 15, 0)))
		goto fail;
	if (scols_line_set_data(ln, COL_DATA, gen_text(prefix, "F", buf, 40, 1)))
		goto fail;
	if (scols_line_set_data(ln, COL_LIKE, "1"))
		goto fail;

	return ln;
fail:
	scols_unref_table(tb);
	err(EXIT_FAILURE, "failed to create output line");
}

int main(int argc, char *argv[])
{
	struct libscols_table *tb;
	struct libscols_line *ln, *xln;
	int c;

	static const struct option longopts[] = {
		{ "random",	0, 0, 'r' },
		{ NULL, 0, 0, 0 },
	};

	setlocale(LC_ALL, "");	/* just to have enable UTF8 chars */

	while((c = getopt_long(argc, argv, "r", longopts, NULL)) != -1) {
		switch(c) {
		case 'r':
			opt_random = 1;
			break;
		default:
			err(EXIT_FAILURE, "%s [-r|--random]\n", program_invocation_short_name);
		}
	}

	if (opt_random)
		xsrand();

	scols_init_debug(0);

	tb = scols_new_table();
	if (!tb)
		err(EXIT_FAILURE, "failed to create output table");

	scols_table_enable_colors(tb, isatty(STDOUT_FILENO));
	setup_columns(tb);

	ln = add_line(tb, NULL, "A");
	add_line(tb, ln, "aa");
	add_line(tb, ln, "ab");

	ln = add_line(tb, NULL, "B");
	xln = add_line(tb, ln, "ba");
	add_line(tb, xln, "baa");
	add_line(tb, xln, "bab");
	add_line(tb, ln, "bb");

	scols_print_table(tb);
	scols_unref_table(tb);
	return EXIT_SUCCESS;
}
