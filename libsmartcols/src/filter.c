#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "smartcolsP.h"

#include "filter-parser.h"
#include "filter-scanner.h"

struct libscols_filter *scols_new_filter()
{
	struct libscols_filter *fltr = calloc(1, sizeof(*fltr));

	return fltr;
}

static void reset_filter(struct libscols_filter *fltr)
{
	if (!fltr)
		return;
	filter_unref_node(fltr->root);
	fltr->root = NULL;

	if (fltr->src)
		fclose(fltr->src);
	fltr->src = NULL;
}

int scols_filter_parse_string(struct libscols_filter *fltr, const char *string)
{
	yyscan_t sc;
	int rc;

	reset_filter(fltr);

	fltr->src = fmemopen((void *) string, strlen(string) + 1, "r");
	if (!fltr->src)
		return -errno;

	yylex_init(&sc);
	yyset_in(fltr->src, sc);

	rc = yyparse(sc, fltr);
	yylex_destroy(sc);

	return rc;
}

/* TODO:

scols_dump_filter()
scols_line_apply_filter()
scols_table_apply_filter()
 */
