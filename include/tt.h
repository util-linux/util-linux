/*
 * Prints table or tree. See lib/table.c for more details and example.
 *
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#ifndef UTIL_LINUX_TT_H
#define UTIL_LINUX_TT_H

#include "list.h"

enum {
	TT_FL_TRUNCATE   = (1 << 1),
	TT_FL_TREE       = (1 << 2),
	TT_FL_RAW        = (1 << 3),
	TT_FL_ASCII      = (1 << 4),
	TT_FL_NOHEADINGS = (1 << 5)
};

struct tt {
	int	ncols;		/* number of columns */
	int	termwidth;	/* terminal width */
	int	flags;

	struct list_head	tb_columns;
	struct list_head	tb_lines;

	const struct tt_symbols	*symbols;
};

struct tt_column {
	const char *name;	/* header */
	int	seqnum;

	int	width;		/* real column width */
	int	width_min;	/* minimal width (width of header) */
	double	width_hint;	/* hint (N < 1 is in percent of termwidth) */

	int	flags;

	struct list_head	cl_columns;
};

struct tt_line {
	struct tt	*table;
	char const	**data;

	struct list_head	ln_lines;	/* table lines */

	struct list_head	ln_branch;	/* begin of branch (head of ln_children) */
	struct list_head	ln_children;

	struct tt_line	*parent;
};

extern struct tt *tt_new_table(int flags);
extern void tt_free_table(struct tt *tb);
extern int tt_print_table(struct tt *tb);

extern struct tt_column *tt_define_column(struct tt *tb, const char *name,
						double whint, int flags);

extern struct tt_column *tt_get_column(struct tt *tb, int colnum);

extern struct tt_line *tt_add_line(struct tt *tb, struct tt_line *parent);

extern int tt_line_set_data(struct tt_line *ln, int colnum, const char *data);

#endif /* UTIL_LINUX_TT_H */
