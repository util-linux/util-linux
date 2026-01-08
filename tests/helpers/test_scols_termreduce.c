/*
 * test_scols_termreduce.c
 *
 * Regression test helper for libsmartcols.
 *
 * Verify that calling scols_table_reduce_termwidth() does not cause
 * cumulative width reduction when printing is repeated in the same process.
 *
 * This helper prints the same multiple times. All outputs must be identical:
 *   - All prints:   width = 40 - 4 = 36 (constant)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsmartcols/src/libsmartcols.h"

#define TEST_TERMWIDTH 40
#define TEST_TERMREDUCE 4
#define NUM_ITERATIONS 3

int main(void)
{
	struct libscols_table *tb;
	struct libscols_column *cl;
	struct libscols_line *ln;
	int i;
	const char *longstr =
		"THIS-IS-A-VERY-LONG-STRING-THAT-WOULD-BE-TRUNCATED";

	tb = scols_new_table();
	if (!tb)
		return EXIT_FAILURE;

	/* Make output deterministic */
	scols_table_set_termwidth(tb, TEST_TERMWIDTH);

	/* Force terminal mode */
	scols_table_set_termforce(tb, SCOLS_TERMFORCE_ALWAYS);

	/* Exercise termreduce handling */
	scols_table_reduce_termwidth(tb, TEST_TERMREDUCE);

	/* One truncated column */
	cl = scols_table_new_column(tb, "DATA", 0, SCOLS_FL_TRUNC);
	if (!cl)
		goto err;

	ln = scols_table_new_line(tb, NULL);
	if (!ln)
		goto err;

	if (scols_line_set_data(ln, 0, longstr) != 0)
		goto err;

	/* Print multiple times - output must be identical */
	for (i = 0; i < NUM_ITERATIONS; i++) {
		scols_print_table(tb);
	}

	scols_unref_table(tb);
	return EXIT_SUCCESS;

err:
	scols_unref_table(tb);
	return EXIT_FAILURE;
}
