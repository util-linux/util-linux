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
#include "debug.h"

#include "libsmartcols.h"

/* features */
#define CONFIG_LIBSMARTCOLS_ASSERT

#ifdef CONFIG_LIBSMARTCOLS_ASSERT
# include <assert.h>
#else
# define assert(x)
#endif

/*
 * Debug
 */
#define SCOLS_DEBUG_INIT	(1 << 1)
#define SCOLS_DEBUG_CELL	(1 << 2)
#define SCOLS_DEBUG_LINE	(1 << 3)
#define SCOLS_DEBUG_TAB		(1 << 4)
#define SCOLS_DEBUG_COL		(1 << 5)
#define SCOLS_DEBUG_BUFF	(1 << 6)
#define SCOLS_DEBUG_ALL		0xFFFF

UL_DEBUG_DECLARE_MASK(libsmartcols);
#define DBG(m, x)	__UL_DBG(libsmartcols, SCOLS_DEBUG_, m, x)
#define ON_DBG(m, x)	__UL_DBG_CALL(libsmartcols, SCOLS_DEBUG_, m, x)
#define DBG_FLUSH	__UL_DBG_FLUSH(libsmartcols, SCOLS_DEBUG_)

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
	void    *userdata;
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

	int	flags;
	int	is_extreme;
	char	*color;		/* default column color */

	int (*cmpfunc)(struct libscols_cell *,
		       struct libscols_cell *,
		       void *);			/* cells comparison function */
	void *cmpfunc_data;

	struct libscols_cell	header;
	struct list_head	cl_columns;
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

enum {
	SCOLS_FMT_HUMAN = 0,		/* default, human readable */
	SCOLS_FMT_RAW,			/* space separated */
	SCOLS_FMT_EXPORT		/* COLNAME="data" ... */
};

/*
 * The table
 */
struct libscols_table {
	int	refcount;
	size_t	ncols;		/* number of columns */
	size_t  ntreecols;	/* number of columns with SCOLS_FL_TREE */
	size_t	nlines;		/* number of lines */
	size_t	termwidth;	/* terminal width */
	size_t  termreduce;	/* extra blank space */
	FILE	*out;		/* output stream */

	char	*colsep;	/* column separator */
	char	*linesep;	/* line separator */

	struct list_head	tb_columns;
	struct list_head	tb_lines;
	struct libscols_symbols	*symbols;

	int	format;		/* SCOLS_FMT_* */

	/* flags */
	unsigned int	ascii		:1,	/* don't use unicode */
			colors_wanted	:1,	/* enable colors */
			is_term		:1,	/* isatty() */
			maxout		:1,	/* maximalize output */
			no_headings	:1;	/* don't print header */
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
