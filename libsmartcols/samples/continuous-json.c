/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2016 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2023 Thomas Weißschuh <thomas@t-8ch.de>
 */
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "c.h"
#include "strutils.h"
#include "xalloc.h"

#include "libsmartcols.h"

/* add columns to the @tb */
static void setup_columns(struct libscols_table *tb)
{
	scols_table_enable_maxout(tb, 1);
	if (!scols_table_new_column(tb, "COUNT", 0.1, SCOLS_FL_RIGHT))
		goto fail;
	if (!scols_table_new_column(tb, "TEXT", 0.9, 0))
		goto fail;
	return;
fail:
	scols_unref_table(tb);
	err(EXIT_FAILURE, "failed to create output columns");
}

static struct libscols_line *add_line(struct libscols_table *tb, int i)
{
	struct libscols_line *ln = scols_table_new_line(tb, NULL);

	if (!ln)
		err(EXIT_FAILURE, "failed to create output line");

	if (scols_line_sprintf(ln, 0, "%d", i))
		goto fail;

	if (scols_line_sprintf(ln, 1, "text%d", i))
		goto fail;

	return ln;
fail:
	scols_unref_table(tb);
	err(EXIT_FAILURE, "failed to create output line");
}

int main(int argc, char *argv[])
{
	struct libscols_table *tb;
	int i, batch = 0, count = 0;

	scols_init_debug(0);

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--batch") == 0 && i + 1 < argc)
			batch = strtos32_or_err(argv[++i], "failed to parse --batch");
	}

	tb = scols_new_table();
	if (!tb)
		err(EXIT_FAILURE, "failed to create output table");
	scols_table_enable_json(tb, 1);

	setup_columns(tb);

	if (batch) {
		/* print more lines by one call, the same way as findmnt --poll
		 * prints all changes reported by one poll() wakeup */
		for (i = 0; i < 2; i++) {
			int n;

			for (n = 0; n < batch; n++)
				add_line(tb, count++);

			scols_table_print_range(tb, NULL, NULL);
			fflush(scols_table_get_stream(tb));

			scols_table_remove_lines(tb);
		}
	} else {
		for (i = 0; i < 10; i++) {
			struct libscols_line *line;

			line = add_line(tb, i);

			/* print the line */
			scols_table_print_range(tb, line, NULL);

			fflush(scols_table_get_stream(tb));
		}
	}

	scols_unref_table(tb);
	return EXIT_SUCCESS;
}
