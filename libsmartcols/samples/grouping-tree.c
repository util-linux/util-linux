/*
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "libsmartcols.h"

static struct libscols_line *add_line(struct libscols_table *tb,
		struct libscols_line *parent, const char *name, const char *data)
{
	struct libscols_line *ln = scols_table_new_line(tb, parent);

	if (!ln || scols_line_set_data(ln, 0, name)
	    || scols_line_set_data(ln, 1, data))
		err(EXIT_FAILURE, "failed to create line");
	return ln;
}

int main(int argc, char **argv)
{
	struct libscols_table *tb;
	struct libscols_column *cl;
	struct libscols_line *member, *ln, *parent;
	int wrap = argc == 2 && strcmp(argv[1], "--wrap") == 0;
	int rc;

	if (argc > 2 || (argc == 2 && !wrap))
		errx(EXIT_FAILURE, "usage: %s [--wrap]", argv[0]);

	scols_init_debug(0);
	tb = scols_new_table();
	if (!tb)
		err(EXIT_FAILURE, "failed to create table");

	scols_table_enable_ascii(tb, 1);
	if (!scols_table_new_column(tb, "NAME", 0, SCOLS_FL_TREE))
		err(EXIT_FAILURE, "failed to create column");
	cl = scols_table_new_column(tb, "DATA", 0, wrap ? SCOLS_FL_WRAP : 0);
	if (!cl)
		err(EXIT_FAILURE, "failed to create column");
	if (wrap) {
		scols_column_set_wrapfunc(cl, scols_wrapnl_chunksize,
				scols_wrapnl_nextchunk, NULL);
		scols_column_set_safechars(cl, "\n");
	}

	member = add_line(tb, NULL, "disk-a", NULL);
	ln = add_line(tb, NULL, "disk-b", NULL);
	if (scols_table_group_lines(tb, member, ln, 0))
		errx(EXIT_FAILURE, "failed to create group");

	/* A tree below the first group child must end before the next child. */
	parent = add_line(tb, NULL, "raid-a", NULL);
	if (scols_line_link_group(parent, member, 0))
		errx(EXIT_FAILURE, "failed to link group");
	add_line(tb, parent, "part-a", "first");
	ln = add_line(tb, parent, "part-b", NULL);
	add_line(tb, ln, "volume", wrap ? "one\ntwo" : "one");

	/* The next child has no tree; the last child has a final tree leaf. */
	ln = add_line(tb, NULL, "raid-b", "next");
	if (scols_line_link_group(ln, member, 0))
		errx(EXIT_FAILURE, "failed to link group");
	parent = add_line(tb, NULL, "raid-c", NULL);
	if (scols_line_link_group(parent, member, 0))
		errx(EXIT_FAILURE, "failed to link group");
	add_line(tb, parent, "last", "end");

	rc = scols_print_table(tb);
	scols_unref_table(tb);
	return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
