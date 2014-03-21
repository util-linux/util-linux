/*
 * smartcolsP.h - private library header file
 *
 * Copyright (C) 2014 Ondrej Oprala <ooprala@redhat.com>
 * Copyright (C) 2014 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#ifndef _LIBSMARTCOLS_PRIVATE_H
#define _LIBSMARTCOLS_PRIVATE_H

#include "c.h"
#include "list.h"
#include "colors.h"
#include "libsmartcols.h"

#define CONFIG_LIBSMARTCOLS_ASSERT

#ifdef CONFIG_LIBSMARTCOLS_ASSERT
# include <assert.h>
#else
# define assert(x)
#endif

/*
 * Generic iterator
 */
struct libscols_iter {
	struct list_head        *p;		/* current position */
	struct list_head        *head;		/* start position */
	int			direction;	/* SCOLS_ITER_{FOR,BACK}WARD */
};

/*
 * Tree symbols
 */
struct libscols_symbols {
	int	refcount;
	char	*branch;
	char	*vert;
	char	*right;
};

/*
 * Table cells
 */
struct libscols_cell {
	char	*data;
	char	*color;

	unsigned int is_ref;	/* data is reference to foreign pointer */
};


/*
 * Table column
 */
struct libscols_column {
	int	refcount;	/* reference counter */
	size_t	seqnum;		/* column index */

	size_t	width;		/* real column width */
	size_t	width_min;	/* minimal width (usually header width) */
	size_t  width_max;	/* maximal width */
	size_t  width_avg;	/* average width, used to detect extreme fields */
	double	width_hint;	/* hint (N < 1 is in percent of termwidth) */

	int	is_extreme;
	char	*color;		/* default column color */

	struct libscols_cell	header;
	struct list_head	cl_columns;

	unsigned int 	trunc		:1; /* truncate fields data if necessary */
	unsigned int 	tree		:1; /* use tree "ascii art" */
	unsigned int 	right		:1; /* align to the right */
	unsigned int 	strict_width	:1; /* don't reduce width if column is empty */
	unsigned int 	no_extremes	:1; /* ignore extreme fields when count column width*/
};

/*
 * Table line
 */
struct libscols_line {
	int	refcount;
	size_t	seqnum;

	void	*userdata;
	char	*color;		/* default line color */

	struct libscols_cell	*cells;		/* array with data */
	size_t			ncells;		/* number of cells */

	struct list_head	ln_lines;	/* table lines */
	struct list_head	ln_branch;	/* begin of branch (head of ln_children) */
	struct list_head	ln_children;

	struct libscols_line	*parent;
};

/*
 * The table
 */
struct libscols_table {
	int	refcount;
	size_t	ncols;		/* number of columns */
	size_t	nlines;		/* number of lines */
	size_t	termwidth;	/* terminal width */
	size_t  termreduce;	/* extra blank space */
	int	is_term;	/* is a tty? */
	FILE	*out;		/* output stream */

	struct list_head	tb_columns;
	struct list_head	tb_lines;
	struct libscols_symbols	*symbols;

	/* flags */
	unsigned int	colors_wanted	:1;
	unsigned int 	raw		:1;
	unsigned int 	ascii		:1;
	unsigned int 	no_headings	:1;
	unsigned int 	export		:1;
	unsigned int 	max		:1;
	unsigned int 	tree		:1;
};

#define IS_ITER_FORWARD(_i)	((_i)->direction == SCOLS_ITER_FORWARD)
#define IS_ITER_BACKWARD(_i)	((_i)->direction == SCOLS_ITER_BACKWARD)

#define SCOLS_ITER_INIT(itr, list) \
	do { \
		(itr)->p = IS_ITER_FORWARD(itr) ? \
				(list)->next : (list)->prev; \
		(itr)->head = (list); \
	} while(0)

#define SCOLS_ITER_ITERATE(itr, res, restype, member) \
	do { \
		res = list_entry((itr)->p, restype, member); \
		(itr)->p = IS_ITER_FORWARD(itr) ? \
				(itr)->p->next : (itr)->p->prev; \
	} while(0)

#endif /* _LIBSMARTCOLS_PRIVATE_H */
