/*
 * table.c - functions handling the data at the table level
 *
 * Copyright (C) 2010-2014 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: table_print
 * @title: Table print
 * @short_description: table print API
 *
 * Table output API.
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <ctype.h>

#include "nls.h"
#include "mbsalign.h"
#include "widechar.h"
#include "ttyutils.h"
#include "carefulputc.h"
#include "smartcolsP.h"

/* This is private struct to work with output data */
struct libscols_buffer {
	char	*begin;		/* begin of the buffer */
	char	*cur;		/* current end of  the buffer */
	char	*encdata;	/* encoded buffer mbs_safe_encode() */

	size_t	bufsz;		/* size of the buffer */
	size_t	art_idx;	/* begin of the tree ascii art or zero */
};

static struct libscols_buffer *new_buffer(size_t sz)
{
	struct libscols_buffer *buf = malloc(sz + sizeof(struct libscols_buffer));

	if (!buf)
		return NULL;

	buf->cur = buf->begin = ((char *) buf) + sizeof(struct libscols_buffer);
	buf->encdata = NULL;
	buf->bufsz = sz;

	DBG(BUFF, ul_debugobj(buf, "alloc (size=%zu)", sz));
	return buf;
}

static void free_buffer(struct libscols_buffer *buf)
{
	if (!buf)
		return;
	DBG(BUFF, ul_debugobj(buf, "dealloc"));
	free(buf->encdata);
	free(buf);
}

static int buffer_reset_data(struct libscols_buffer *buf)
{
	if (!buf)
		return -EINVAL;

	/*DBG(BUFF, ul_debugobj(buf, "reset data"));*/
	buf->begin[0] = '\0';
	buf->cur = buf->begin;
	buf->art_idx = 0;
	return 0;
}

static int buffer_append_data(struct libscols_buffer *buf, const char *str)
{
	size_t maxsz, sz;

	if (!buf)
		return -EINVAL;
	if (!str || !*str)
		return 0;

	sz = strlen(str);
	maxsz = buf->bufsz - (buf->cur - buf->begin);

	if (maxsz <= sz)
		return -EINVAL;

	memcpy(buf->cur, str, sz + 1);
	buf->cur += sz;
	return 0;
}

static int buffer_set_data(struct libscols_buffer *buf, const char *str)
{
	int rc = buffer_reset_data(buf);
	return rc ? rc : buffer_append_data(buf, str);
}

/* save the current buffer possition to art_idx */
static void buffer_set_art_index(struct libscols_buffer *buf)
{
	if (buf) {
		buf->art_idx = buf->cur - buf->begin;
		/*DBG(BUFF, ul_debugobj(buf, "art index: %zu", buf->art_idx));*/
	}
}

static char *buffer_get_data(struct libscols_buffer *buf)
{
	return buf ? buf->begin : NULL;
}

/* encode data by mbs_safe_encode() to avoid control and non-printable chars */
static char *buffer_get_safe_data(struct libscols_buffer *buf, size_t *cells)
{
	char *data = buffer_get_data(buf);
	char *res = NULL;

	if (!data)
		goto nothing;

	if (!buf->encdata) {
		buf->encdata = malloc(mbs_safe_encode_size(buf->bufsz) + 1);
		if (!buf->encdata)
			goto nothing;
	}

	res = mbs_safe_encode_to_buffer(data, cells, buf->encdata);
	if (!res || !*cells || *cells == (size_t) -1)
		goto nothing;
	return res;
nothing:
	*cells = 0;
	return NULL;
}

/* returns size in bytes of the ascii art (according to art_idx) in safe encoding */
static size_t buffer_get_safe_art_size(struct libscols_buffer *buf)
{
	char *data = buffer_get_data(buf);
	size_t bytes = 0;

	if (!data || !buf->art_idx)
		return 0;

	mbs_safe_nwidth(data, buf->art_idx, &bytes);
	return bytes;
}

#define is_last_column(_tb, _cl) \
		list_entry_is_last(&(_cl)->cl_columns, &(_tb)->tb_columns)

#define colsep(tb) ((tb)->colsep ? (tb)->colsep : " ")
#define linesep(tb) ((tb)->linesep ? (tb)->linesep : "\n")

static int print_data(struct libscols_table *tb,
		      struct libscols_column *cl,
		      struct libscols_line *ln,	/* optional */
		      struct libscols_cell *ce,	/* optional */
		      struct libscols_buffer *buf)
{
	size_t len = 0, i, width, bytes;
	const char *color = NULL;
	char *data;

	assert(tb);
	assert(cl);

	DBG(TAB, ul_debugobj(tb,
			" -> data, column=%p, line=%p, cell=%p, buff=%p",
			cl, ln, ce, buf));

	data = buffer_get_data(buf);
	if (!data)
		data = "";

	/* raw mode */
	if (scols_table_is_raw(tb)) {
		fputs_nonblank(data, tb->out);
		if (!is_last_column(tb, cl))
			fputs(colsep(tb), tb->out);
		return 0;
	}

	/* NAME=value mode */
	if (scols_table_is_export(tb)) {
		fprintf(tb->out, "%s=", scols_cell_get_data(&cl->header));
		fputs_quoted(data, tb->out);
		if (!is_last_column(tb, cl))
			fputs(colsep(tb), tb->out);
		return 0;
	}

	if (tb->colors_wanted) {
		if (ce && !color)
			color = ce->color;
		if (ln && !color)
			color = ln->color;
		if (!color)
			color = cl->color;
	}

	/* encode, note that 'len' and 'width' are number of cells, not bytes */
	data = buffer_get_safe_data(buf, &len);
	if (!data)
		data = "";
	width = cl->width;
	bytes = strlen(data);

	if (is_last_column(tb, cl) && len < width && !scols_table_is_maxout(tb))
		width = len;

	/* truncate data */
	if (len > width && scols_column_is_trunc(cl)) {
		len = width;
		bytes = mbs_truncate(data, &len);	/* updates 'len' */

		if (!data || bytes == (size_t) -1) {
			bytes = len = 0;
			data = NULL;
		}
	}

	if (data) {
		if (scols_column_is_right(cl)) {
			size_t xw = cl->width;
			if (color)
				fputs(color, tb->out);
			fprintf(tb->out, "%*s", (int) xw, data);
			if (color)
				fputs(UL_COLOR_RESET, tb->out);
			if (len < xw)
				len = xw;
		} else if (color) {
			char *p = data;
			size_t art = buffer_get_safe_art_size(buf);

			/* we don't want to colorize tree ascii art */
			if (scols_column_is_tree(cl) && art && art < bytes) {
				fwrite(p, 1, art, tb->out);
				p += art;
			}

			fputs(color, tb->out);
			fputs(p, tb->out);
			fputs(UL_COLOR_RESET, tb->out);
		} else
			fputs(data, tb->out);
	}
	for (i = len; i < width; i++)
		fputs(" ", tb->out);		/* padding */

	if (!is_last_column(tb, cl)) {
		if (len > width && !scols_column_is_trunc(cl)) {
			fputs(linesep(tb), tb->out);
			for (i = 0; i <= (size_t) cl->seqnum; i++) {
				struct libscols_column *x = scols_table_get_column(tb, i);
				fprintf(tb->out, "%*s ", -((int)x->width), " ");
			}
		} else
			fputs(colsep(tb), tb->out);	/* columns separator */
	}

	return 0;
}

/* returns pointer to the end of used data */
static int line_ascii_art_to_buffer(struct libscols_table *tb,
				    struct libscols_line *ln,
				    struct libscols_buffer *buf)
{
	const char *art;
	int rc;

	assert(ln);
	assert(buf);

	if (!ln->parent)
		return 0;

	rc = line_ascii_art_to_buffer(tb, ln->parent, buf);
	if (rc)
		return rc;

	if (list_entry_is_last(&ln->ln_children, &ln->parent->ln_branch))
		art = "  ";
	else
		art = tb->symbols->vert;

	return buffer_append_data(buf, art);
}

static int cell_to_buffer(struct libscols_table *tb,
			  struct libscols_line *ln,
			  struct libscols_column *cl,
			  struct libscols_buffer *buf)
{
	const char *data;
	struct libscols_cell *ce;
	int rc = 0;

	assert(tb);
	assert(ln);
	assert(cl);
	assert(buf);
	assert(cl->seqnum <= tb->ncols);

	buffer_reset_data(buf);

	ce = scols_line_get_cell(ln, cl->seqnum);
	data = ce ? scols_cell_get_data(ce) : NULL;
	if (!data)
		return 0;

	if (!scols_column_is_tree(cl))
		return buffer_set_data(buf, data);

	/*
	 * Tree stuff
	 */
	if (ln->parent) {
		rc = line_ascii_art_to_buffer(tb, ln->parent, buf);

		if (!rc && list_entry_is_last(&ln->ln_children, &ln->parent->ln_branch))
			rc = buffer_append_data(buf, tb->symbols->right);
		else if (!rc)
			rc = buffer_append_data(buf, tb->symbols->branch);
		if (!rc)
			buffer_set_art_index(buf);
	}

	if (!rc)
		rc = buffer_append_data(buf, data);
	return rc;
}

/*
 * Prints data, data maybe be printed in more formats (raw, NAME=xxx pairs) and
 * control and non-printable chars maybe encoded in \x?? hex encoding.
 */
static int print_line(struct libscols_table *tb,
		      struct libscols_line *ln,
		      struct libscols_buffer *buf)
{
	int rc = 0;
	struct libscols_column *cl;
	struct libscols_iter itr;

	assert(ln);

	DBG(TAB, ul_debugobj(tb, "printing line, line=%p, buff=%p", ln, buf));

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (rc == 0 && scols_table_next_column(tb, &itr, &cl) == 0) {
		rc = cell_to_buffer(tb, ln, cl, buf);
		if (!rc)
			rc = print_data(tb, cl, ln,
					scols_line_get_cell(ln, cl->seqnum),
					buf);
	}

	if (rc == 0)
		fputs(linesep(tb), tb->out);
	return 0;
}

static int print_header(struct libscols_table *tb, struct libscols_buffer *buf)
{
	int rc = 0;
	struct libscols_column *cl;
	struct libscols_iter itr;

	assert(tb);

	if (scols_table_is_noheadings(tb) ||
	    scols_table_is_export(tb) ||
	    list_empty(&tb->tb_lines))
		return 0;

	DBG(TAB, ul_debugobj(tb, "printing header"));

	/* set width according to the size of data
	 */
	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (rc == 0 && scols_table_next_column(tb, &itr, &cl) == 0) {
		rc = buffer_set_data(buf, scols_cell_get_data(&cl->header));
		if (!rc)
			rc = print_data(tb, cl, NULL, &cl->header, buf);
	}

	if (rc == 0)
		fputs(linesep(tb), tb->out);
	return rc;
}

static int print_table(struct libscols_table *tb, struct libscols_buffer *buf)
{
	int rc;
	struct libscols_line *ln;
	struct libscols_iter itr;

	assert(tb);

	rc = print_header(tb, buf);

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (rc == 0 && scols_table_next_line(tb, &itr, &ln) == 0)
		rc = print_line(tb, ln, buf);

	return rc;
}

static int print_tree_line(struct libscols_table *tb,
			   struct libscols_line *ln,
			   struct libscols_buffer *buf)
{
	int rc;
	struct list_head *p;

	rc = print_line(tb, ln, buf);
	if (rc)
		return rc;
	if (list_empty(&ln->ln_branch))
		return 0;

	/* print all children */
	list_for_each(p, &ln->ln_branch) {
		struct libscols_line *chld =
				list_entry(p, struct libscols_line, ln_children);
		rc = print_tree_line(tb, chld, buf);
		if (rc)
			break;
	}

	return rc;
}

static int print_tree(struct libscols_table *tb, struct libscols_buffer *buf)
{
	int rc;
	struct libscols_line *ln;
	struct libscols_iter itr;

	assert(tb);

	DBG(TAB, ul_debugobj(tb, "printing tree"));

	rc = print_header(tb, buf);

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (rc == 0 && scols_table_next_line(tb, &itr, &ln) == 0) {
		if (ln->parent)
			continue;
		rc = print_tree_line(tb, ln, buf);
	}

	return rc;
}

static void dbg_column(struct libscols_table *tb, struct libscols_column *cl)
{
	DBG(COL, ul_debugobj(cl, "%15s seq=%zu, width=%zd, "
				 "hint=%d, avg=%zu, max=%zu, min=%zu, "
				 "extreme=%s",

		cl->header.data, cl->seqnum, cl->width,
		cl->width_hint > 1 ? (int) cl->width_hint :
				     (int) (cl->width_hint * tb->termwidth),
		cl->width_avg,
		cl->width_max,
		cl->width_min,
		cl->is_extreme ? "yes" : "not"));
}

static void dbg_columns(struct libscols_table *tb)
{
	struct libscols_iter itr;
	struct libscols_column *cl;

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_column(tb, &itr, &cl) == 0)
		dbg_column(tb, cl);
}

/*
 * This function counts column width.
 *
 * For the SCOLS_FL_NOEXTREMES columns is possible to call this function two
 * times.  The first pass counts width and average width. If the column
 * contains too large fields (width greater than 2 * average) then the column
 * is marked as "extreme". In the second pass all extreme fields are ignored
 * and column width is counted from non-extreme fields only.
 */
static int count_column_width(struct libscols_table *tb,
			      struct libscols_column *cl,
			      struct libscols_buffer *buf)
{
	struct libscols_line *ln;
	struct libscols_iter itr;
	int count = 0, rc = 0;
	size_t sum = 0;

	assert(tb);
	assert(cl);

	cl->width = 0;

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_line(tb, &itr, &ln) == 0) {
		size_t len;
		char *data;

		rc = cell_to_buffer(tb, ln, cl, buf);
		if (rc)
			return rc;

		data = buffer_get_data(buf);
		len = data ? mbs_safe_width(data) : 0;

		if (len == (size_t) -1)		/* ignore broken multibyte strings */
			len = 0;
		if (len > cl->width_max)
			cl->width_max = len;

		if (cl->is_extreme && len > cl->width_avg * 2)
			continue;
		else if (scols_column_is_noextremes(cl)) {
			sum += len;
			count++;
		}
		if (len > cl->width)
			cl->width = len;
	}

	if (count && cl->width_avg == 0) {
		cl->width_avg = sum / count;

		if (cl->width_max > cl->width_avg * 2)
			cl->is_extreme = 1;
	}

	/* check and set minimal column width */
	if (scols_cell_get_data(&cl->header))
		cl->width_min = mbs_safe_width(scols_cell_get_data(&cl->header));

	/* enlarge to minimal width */
	if (cl->width < cl->width_min && !scols_column_is_strict_width(cl))
		cl->width = cl->width_min;

	/* use relative size for large columns */
	else if (cl->width_hint >= 1 && cl->width < (size_t) cl->width_hint
		 && cl->width_min < (size_t) cl->width_hint)

		cl->width = (size_t) cl->width_hint;

	ON_DBG(COL, dbg_column(tb, cl));
	return rc;
}


/*
 * This is core of the scols_* voodo...
 */
static int recount_widths(struct libscols_table *tb, struct libscols_buffer *buf)
{
	struct libscols_column *cl;
	struct libscols_iter itr;
	size_t width = 0;		/* output width */
	int trunc_only, rc = 0;
	int extremes = 0;


	DBG(TAB, ul_debugobj(tb, "recounting widths (termwidth=%zu)", tb->termwidth));

	/* set basic columns width
	 */
	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_column(tb, &itr, &cl) == 0) {
		rc = count_column_width(tb, cl, buf);
		if (rc)
			return rc;

		width += cl->width + (is_last_column(tb, cl) ? 0 : 1);
		extremes += cl->is_extreme;
	}

	if (!tb->is_term)
		return 0;

	/* reduce columns with extreme fields
	 */
	if (width > tb->termwidth && extremes) {
		DBG(TAB, ul_debugobj(tb, "   reduce width (extreme columns)"));

		scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
		while (scols_table_next_column(tb, &itr, &cl) == 0) {
			size_t org_width;

			if (!cl->is_extreme)
				continue;

			org_width = cl->width;
			rc = count_column_width(tb, cl, buf);
			if (rc)
				return rc;

			if (org_width > cl->width)
				width -= org_width - cl->width;
			else
				extremes--;	/* hmm... nothing reduced */
		}
	}

	if (width < tb->termwidth) {
		if (extremes) {
			DBG(TAB, ul_debugobj(tb, "   enlarge width (extreme columns)"));

			/* enlarge the first extreme column */
			scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
			while (scols_table_next_column(tb, &itr, &cl) == 0) {
				size_t add;

				if (!cl->is_extreme)
					continue;

				/* this column is tooo large, ignore?
				if (cl->width_max - cl->width >
						(tb->termwidth - width))
					continue;
				*/

				add = tb->termwidth - width;
				if (add && cl->width + add > cl->width_max)
					add = cl->width_max - cl->width;

				cl->width += add;
				width += add;

				if (width == tb->termwidth)
					break;
			}
		}

		if (width < tb->termwidth && scols_table_is_maxout(tb)) {
			DBG(TAB, ul_debugobj(tb, "   enlarge width (max-out)"));

			/* try enlarge all columns */
			while (width < tb->termwidth) {
				scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
				while (scols_table_next_column(tb, &itr, &cl) == 0) {
					cl->width++;
					width++;
					if (width == tb->termwidth)
						break;
				}
			}
		} else if (width < tb->termwidth) {
			/* enlarge the last column */
			struct libscols_column *cl = list_entry(
				tb->tb_columns.prev, struct libscols_column, cl_columns);

			DBG(TAB, ul_debugobj(tb, "   enlarge width (last column)"));

			if (!scols_column_is_right(cl) && tb->termwidth - width > 0) {
				cl->width += tb->termwidth - width;
				width = tb->termwidth;
			}
		}
	}

	/* bad, we have to reduce output width, this is done in two steps:
	 * 1/ reduce columns with a relative width and with truncate flag
	 * 2) reduce columns with a relative width without truncate flag
	 */
	trunc_only = 1;
	while (width > tb->termwidth) {
		size_t org = width;

		DBG(TAB, ul_debugobj(tb, "   reduce width (current=%zu, "
					 "wanted=%zu, mode=%s)",
					width, tb->termwidth,
					trunc_only ? "trunc-only" : "all-relative"));

		scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
		while (scols_table_next_column(tb, &itr, &cl) == 0) {
			if (width <= tb->termwidth)
				break;
			if (cl->width_hint > 1 && !scols_column_is_trunc(cl))
				continue;	/* never truncate columns with absolute sizes */
			if (scols_column_is_tree(cl))
				continue;	/* never truncate the tree */
			if (trunc_only && !scols_column_is_trunc(cl))
				continue;
			if (cl->width == cl->width_min)
				continue;

			/* truncate column with relative sizes */
			if (cl->width_hint < 1 && cl->width > 0 && width > 0 &&
			    cl->width > cl->width_hint * tb->termwidth) {
				cl->width--;
				width--;
			}
			/* truncate column with absolute size */
			if (cl->width_hint > 1 && cl->width > 0 && width > 0 &&
			    !trunc_only) {
				cl->width--;
				width--;
			}

		}
		if (org == width) {
			if (trunc_only)
				trunc_only = 0;
			else
				break;
		}
	}

	DBG(TAB, ul_debugobj(tb, "  result: %zu", width));
	ON_DBG(TAB, dbg_columns(tb));

	return rc;
}

static size_t strlen_line(struct libscols_line *ln)
{
	size_t i, sz = 0;

	assert(ln);

	for (i = 0; i < ln->ncells; i++) {
		struct libscols_cell *ce = scols_line_get_cell(ln, i);
		const char *data = ce ? scols_cell_get_data(ce) : NULL;

		sz += data ? strlen(data) : 0;
	}

	return sz;
}



/**
 * scols_print_table:
 * @tb: table
 *
 * Prints the table to the output stream.
 *
 * Returns: 0, a negative value in case of an error.
 */
int scols_print_table(struct libscols_table *tb)
{
	int rc = 0;
	size_t bufsz;
	struct libscols_line *ln;
	struct libscols_iter itr;
	struct libscols_buffer *buf;

	assert(tb);
	if (!tb)
		return -1;

	DBG(TAB, ul_debugobj(tb, "printing"));
	if (!tb->symbols)
		scols_table_set_symbols(tb, NULL);	/* use default */

	tb->is_term = isatty(STDOUT_FILENO) ? 1 : 0;
	tb->termwidth = tb->is_term ? get_terminal_width() : 0;
	if (tb->termwidth <= 0)
		tb->termwidth = 80;
	tb->termwidth -= tb->termreduce;

	bufsz = tb->termwidth;

	scols_reset_iter(&itr, SCOLS_ITER_FORWARD);
	while (scols_table_next_line(tb, &itr, &ln) == 0) {
		size_t sz = strlen_line(ln);
		if (sz > bufsz)
			bufsz = sz;
	}

	buf = new_buffer(bufsz + 1);	/* data + space for \0 */
	if (!buf)
		return -ENOMEM;

	if (!(scols_table_is_raw(tb) || scols_table_is_export(tb))) {
		rc = recount_widths(tb, buf);
		if (rc != 0)
			goto done;
	}

	if (scols_table_is_tree(tb))
		rc = print_tree(tb, buf);
	else
		rc = print_table(tb, buf);

done:
	free_buffer(buf);
	return rc;
}

/**
 * scols_print_table_to_string:
 * @tb: table
 * @data: pointer to the beginning of a memory area to print to
 *
 * Prints the table to @data.
 *
 * Returns: 0, a negative value in case of an error.
 */
int scols_print_table_to_string(struct libscols_table *tb, char **data)
{
#ifdef HAVE_OPEN_MEMSTREAM
	FILE *stream;
	size_t sz;
	int rc;

	if (!tb)
		return -EINVAL;

	DBG(TAB, ul_debugobj(tb, "printing to string"));

	/* create a stream for output */
	stream = open_memstream(data, &sz);
	if (!stream)
		return -ENOMEM;

	scols_table_set_stream(tb, stream);
	rc = scols_print_table(tb);
	fclose(stream);

	return rc;
#else
	return -ENOSYS;
#endif
}

