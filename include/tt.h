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
	/*
	 * Global flags
	 */
	TT_FL_RAW         = (1 << 1),
	TT_FL_ASCII       = (1 << 2),
	TT_FL_NOHEADINGS  = (1 << 3),
	TT_FL_EXPORT      = (1 << 4),
	TT_FL_MAX	  = (1 << 5),	/* maximalize column width if possible */

	/*
	 * Column flags
	 */
	TT_FL_TRUNC       = (1 << 10),	/* truncate fields data if necessary */
	TT_FL_TREE        = (1 << 11),	/* use tree "ascii art" */
	TT_FL_RIGHT	  = (1 << 12),	/* align to the right */
	TT_FL_STRICTWIDTH = (1 << 13),	/* don't reduce width if column is empty */
	TT_FL_NOEXTREMES  = (1 << 14),   /* ignore extreme fields when count column width*/

	TT_FL_FREEDATA	  = (1 << 15),	/* free() data in tt_free_table() */
};

struct tt {
	FILE	*out;		/* output stream */
	size_t	ncols;		/* number of columns */
	size_t	termwidth;	/* terminal width */
	size_t  termreduce;	/* reduce the original termwidth */
	int	is_term;	/* is a tty? */
	int	flags;
	int	first_run;

	struct list_head	tb_columns;
	struct list_head	tb_lines;

	const struct tt_symbols	*symbols;
};

struct tt_column {
	const char *name;	/* header */
	size_t	seqnum;

	size_t	width;		/* real column width */
	size_t	width_min;	/* minimal width (usually header width) */
	size_t  width_max;	/* maximal width */
	size_t  width_avg;	/* average width, used to detect extreme fields */
	double	width_hint;	/* hint (N < 1 is in percent of termwidth) */

	int	flags;
	int	is_extreme;

	struct list_head	cl_columns;
};

struct tt_line {
	struct tt	*table;
	char		**data;
	void		*userdata;
	size_t		data_sz;		/* strlen of all data */

	struct list_head	ln_lines;	/* table lines */

	struct list_head	ln_branch;	/* begin of branch (head of ln_children) */
	struct list_head	ln_children;

	struct tt_line	*parent;
};

extern struct tt *tt_new_table(int flags);
extern int tt_get_flags(struct tt *tb);
extern void tt_set_flags(struct tt *tb, int flags);
extern void tt_set_termreduce(struct tt *tb, size_t re);
extern void tt_free_table(struct tt *tb);
extern void tt_remove_lines(struct tt *tb);
extern int tt_print_table(struct tt *tb);
extern int tt_print_table_to_string(struct tt *tb, char **data);
extern void tt_set_stream(struct tt *tb, FILE *out);

extern struct tt_column *tt_define_column(struct tt *tb, const char *name,
						double whint, int flags);

extern struct tt_column *tt_get_column(struct tt *tb, size_t colnum);

extern struct tt_line *tt_add_line(struct tt *tb, struct tt_line *parent);

extern int tt_line_set_data(struct tt_line *ln, int colnum, char *data);
extern int tt_line_set_userdata(struct tt_line *ln, void *data);

extern void tt_fputs_quoted(const char *data, FILE *out);
extern void tt_fputs_nonblank(const char *data, FILE *out);

extern size_t tb_get_nlines(struct tt *tb);

static inline int tt_is_empty(struct tt *tb)
{
	return !tb || list_empty(&tb->tb_lines);
}

#endif /* UTIL_LINUX_TT_H */
