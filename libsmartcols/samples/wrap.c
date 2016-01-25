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

#include "libsmartcols.h"


enum { COL_NAME, COL_DATA };

/* add columns to the @tb */
static void setup_columns(struct libscols_table *tb)
{
	if (!scols_table_new_column(tb, "NAME", 0, 0))
		goto fail;
	if (!scols_table_new_column(tb, "DATA", 0, SCOLS_FL_WRAP))
		goto fail;
	return;
fail:
	scols_unref_table(tb);
	err(EXIT_FAILURE, "faild to create output columns");
}

static void add_line(struct libscols_table *tb, const char *name, const char *data)
{
	struct libscols_line *ln = scols_table_new_line(tb, NULL);
	if (!ln)
		err(EXIT_FAILURE, "failed to create output line");

	if (scols_line_set_data(ln, COL_NAME, name))
		goto fail;
	if (scols_line_set_data(ln, COL_DATA, data))
		goto fail;
	return;
fail:
	scols_unref_table(tb);
	err(EXIT_FAILURE, "faild to create output line");
}

int main(int argc, char *argv[])
{
	struct libscols_table *tb;

	setlocale(LC_ALL, "");	/* just to have enable UTF8 chars */

	scols_init_debug(0);

	tb = scols_new_table();
	if (!tb)
		err(EXIT_FAILURE, "faild to create output table");

	scols_table_enable_colors(tb, 1);
	setup_columns(tb);

	add_line(tb, "keelboat", "riverine cargo-capable working boat, or a small to mid-sized recreational sailing yacht.");
	add_line(tb, "dinghy", "type of small boat, often carried or towed for use as a ship's boat by a larger vessel.");
	add_line(tb, "monohull", "type of boat having only one hull, unlike multihulled boats which can have two or more individual hulls connected to one another.");
	add_line(tb, "catamaran", "geometry-stabilized craft; that is, it derives its stability from its wide beam, rather than from a ballasted keel, like a monohull.");
	add_line(tb, "trimaran ", "multihull boat that comprises a main hull and two smaller outrigger hulls (or \"floats\") which are attached to the main hull with lateral beams.");

	scols_print_table(tb);
	scols_unref_table(tb);
	return EXIT_SUCCESS;
}
