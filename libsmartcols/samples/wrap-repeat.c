#include <stdio.h>
#include <stdlib.h>

#include "libsmartcols.h"

int main(void)
{
	struct libscols_table *tb;
	struct libscols_line *ln;
	struct libscols_column *cl;

	scols_init_debug(0);

	tb = scols_new_table();
	scols_table_enable_maxout(tb, 1);

	cl = scols_table_new_column(tb, "AAA", 0, 0);
	cl = scols_table_new_column(tb, "BBB", 0, 0);
	cl = scols_table_new_column(tb, "CCC", 0, 0);
	cl = scols_table_new_column(tb, "XXX", 0, 0);
	scols_column_set_flags(cl, SCOLS_FL_WRAP);

	ln = scols_table_new_line(tb, NULL);
	scols_line_set_data(ln, 0, "aaa");
	scols_line_set_data(ln, 1, "4");
	scols_line_set_data(ln, 2, "Foo bar foo.");
	scols_line_set_data(ln, 3, "Adcvfr tgbnhy ujmkifas rqweerq adfs eqeqw kjkliobb u8888 kjhjkh.");

	/* repeat 2x */
	scols_table_print_range(tb, ln, ln);
	printf("\n");
	scols_table_print_range(tb, ln, ln);
	printf("\n");

	scols_unref_table(tb);
	return EXIT_SUCCESS;
}
