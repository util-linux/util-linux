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
#include "strutils.h"
#include "color-names.h"
#include "debug.h"

#include "libsmartcols.h"

/*
 * Debug
 */
#define SCOLS_DEBUG_HELP	(1 << 0)
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

#define UL_DEBUG_CURRENT_MASK	UL_DEBUG_MASK(libsmartcols)
#include "debugobj.h"

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
	char	*title_padding;
	char	*cell_padding;
};

/*
 * Table cells
 */
struct libscols_cell {
	char	*data;
	char	*color;
	void    *userdata;
	int	flags;
};

extern int scols_line_move_cells(struct libscols_line *ln, size_t newn, size_t oldn);

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
	size_t	width_treeart;	/* size of the tree ascii art */
	double	width_hint;	/* hint (N < 1 is in percent of termwidth) */

	int	json_type;	/* SCOLS_JSON_* */

	int	flags;
	int	is_extreme;
	char	*color;		/* default column color */
	char	*safechars;	/* do not encode this bytes */

	char	*pending_data;
	size_t	pending_data_sz;
	char	*pending_data_buf;

	int (*cmpfunc)(struct libscols_cell *,
		       struct libscols_cell *,
		       void *);			/* cells comparison function */
	void *cmpfunc_data;

	size_t (*wrap_chunksize)(const struct libscols_column *,
			const char *, void *);
	char *(*wrap_nextchunk)(const struct libscols_column *,
			char *, void *);
	void *wrapfunc_data;


	struct libscols_cell	header;
	struct list_head	cl_columns;

	struct libscols_table	*table;
};

#define colsep(tb)	((tb)->colsep ? (tb)->colsep : " ")
#define linesep(tb)	((tb)->linesep ? (tb)->linesep : "\n")

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
	struct list_head	ln_group;

	struct libscols_line	*parent;
};

enum {
	SCOLS_FMT_HUMAN = 0,		/* default, human readable */
	SCOLS_FMT_RAW,			/* space separated */
	SCOLS_FMT_EXPORT,		/* COLNAME="data" ... */
	SCOLS_FMT_JSON			/* http://en.wikipedia.org/wiki/JSON */
};

/*
 * The table
 */
struct libscols_table {
	int	refcount;
	char	*name;		/* optional table name (for JSON) */
	size_t	ncols;		/* number of columns */
	size_t  ntreecols;	/* number of columns with SCOLS_FL_TREE */
	size_t	nlines;		/* number of lines */
	size_t	termwidth;	/* terminal width (number of columns) */
	size_t  termheight;	/* terminal height  (number of lines) */
	size_t  termreduce;	/* extra blank space */
	int	termforce;	/* SCOLS_TERMFORCE_* */
	FILE	*out;		/* output stream */

	char	*colsep;	/* column separator */
	char	*linesep;	/* line separator */

	struct list_head	tb_columns;
	struct list_head	tb_lines;
	struct libscols_symbols	*symbols;
	struct libscols_cell	title;		/* optional table title (for humans) */

	int	indent;		/* indention counter */
	int	indent_last_sep;/* last printed has been line separator */
	int	format;		/* SCOLS_FMT_* */

	size_t	termlines_used;	/* printed line counter */
	size_t	header_next;	/* where repeat header */

	/* flags */
	unsigned int	ascii		:1,	/* don't use unicode */
			colors_wanted	:1,	/* enable colors */
			is_term		:1,	/* isatty() */
			padding_debug	:1,	/* output visible padding chars */
			maxout		:1,	/* maximize output */
			header_repeat   :1,     /* print header after libscols_table->termheight */
			header_printed  :1,	/* header already printed */
			priv_symbols	:1,	/* default private symbols */
			no_headings	:1,	/* don't print header */
			no_encode	:1,	/* don't care about control and non-printable chars */
			no_linesep	:1,	/* don't print line separator */
			no_wrap		:1;	/* never wrap lines */
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


static inline int scols_iter_is_last(const struct libscols_iter *itr)
{
	if (!itr || !itr->head || !itr->p)
		return 0;

	return itr->p == itr->head;
}



/*
 * buffer.c
 */
struct libscols_buffer;
extern struct libscols_buffer *new_buffer(size_t sz);
extern void free_buffer(struct libscols_buffer *buf);
extern int buffer_reset_data(struct libscols_buffer *buf);
extern int buffer_append_data(struct libscols_buffer *buf, const char *str);
extern int buffer_set_data(struct libscols_buffer *buf, const char *str);
extern void buffer_set_art_index(struct libscols_buffer *buf);
extern char *buffer_get_data(struct libscols_buffer *buf);
extern size_t buffer_get_size(struct libscols_buffer *buf);
extern char *buffer_get_safe_data(struct libscols_table *tb,
				  struct libscols_buffer *buf,
				  size_t *cells,
				  const char *safechars);
extern size_t buffer_get_safe_art_size(struct libscols_buffer *buf);

/*
 * calculate.c
 */
extern int __scols_calculate(struct libscols_table *tb, struct libscols_buffer *buf);

/*
 * print.c
 */
extern int __cell_to_buffer(struct libscols_table *tb,
                          struct libscols_line *ln,
                          struct libscols_column *cl,
                          struct libscols_buffer *buf);

void __scols_cleanup_printing(struct libscols_table *tb, struct libscols_buffer *buf);
int __scols_initialize_printing(struct libscols_table *tb, struct libscols_buffer **buf);
int __scols_print_tree(struct libscols_table *tb, struct libscols_buffer *buf);
int __scols_print_table(struct libscols_table *tb, struct libscols_buffer *buf);
int __scols_print_header(struct libscols_table *tb, struct libscols_buffer *buf);
int __scols_print_title(struct libscols_table *tb);
int __scols_print_range(struct libscols_table *tb,
                        struct libscols_buffer *buf,
                        struct libscols_iter *itr,
                        struct libscols_line *end);

/*
 * fput.c
 */
extern void fput_indent(struct libscols_table *tb);
extern void fput_table_open(struct libscols_table *tb);
extern void fput_table_close(struct libscols_table *tb);
extern void fput_children_open(struct libscols_table *tb);
extern void fput_children_close(struct libscols_table *tb);
extern void fput_line_open(struct libscols_table *tb);
extern void fput_line_close(struct libscols_table *tb, int last, int last_in_table);

static inline int is_last_child(struct libscols_line *ln)
{
	if (!ln || !ln->parent)
		return 1;

	return list_entry_is_last(&ln->ln_children, &ln->parent->ln_branch);
}


static inline int is_last_column(struct libscols_column *cl)
{
	struct libscols_column *next;

	if (list_entry_is_last(&cl->cl_columns, &cl->table->tb_columns))
		return 1;

	next = list_entry(cl->cl_columns.next, struct libscols_column, cl_columns);
	if (next && scols_column_is_hidden(next) && is_last_column(next))
		return 1;
	return 0;
}

#endif /* _LIBSMARTCOLS_PRIVATE_H */
