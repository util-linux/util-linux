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
	char *branch;
	char *vert;
	char *right;
};

/*
 * Table cells
 */
struct libscols_cell {
	char	*data;
	char	*color;
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

	struct libscols_cell	header;
	struct list_head	cl_columns;
};


#endif /* _LIBSMARTCOLS_PRIVATE_H */
