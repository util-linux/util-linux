/*
 * column.c - functions for table handling at the column level
 *
 * Copyright (C) 2014 Ondrej Oprala <ooprala@redhat.com>
 * Copyright (C) 2014 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: column
 * @title: Column
 * @short_description: defines output columns formats, headers, etc.
 *
 * An API to access and modify per-column data and information.
 */


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "mbsalign.h"

#include "smartcolsP.h"

/**
 * scols_new_column:
 *
 * Allocates space for a new column.
 *
 * Returns: a pointer to a new struct libscols_column instance, NULL in case of an ENOMEM error.
 */
struct libscols_column *scols_new_column(void)
{
	struct libscols_column *cl;

	cl = calloc(1, sizeof(*cl));
	if (!cl)
		return NULL;
	DBG(COL, ul_debugobj(cl, "alloc"));
	cl->refcount = 1;
	INIT_LIST_HEAD(&cl->cl_columns);
	return cl;
}

/**
 * scols_ref_column:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Increases the refcount of @cl.
 */
void scols_ref_column(struct libscols_column *cl)
{
	if (cl)
		cl->refcount++;
}

/**
 * scols_unref_column:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Decreases the refcount of @cl. When the count falls to zero, the instance
 * is automatically deallocated.
 */
void scols_unref_column(struct libscols_column *cl)
{
	if (cl && --cl->refcount <= 0) {
		DBG(COL, ul_debugobj(cl, "dealloc"));
		list_del(&cl->cl_columns);
		scols_reset_cell(&cl->header);
		free(cl->color);
		free(cl->safechars);
		free(cl->pending_data_buf);
		free(cl);
	}
}

/**
 * scols_copy_column:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Creates a new column and copies @cl's data over to it.
 *
 * Returns: a pointer to a new struct libscols_column instance.
 */
struct libscols_column *scols_copy_column(const struct libscols_column *cl)
{
	struct libscols_column *ret;

	if (!cl)
		return NULL;
	ret = scols_new_column();
	if (!ret)
		return NULL;

	DBG(COL, ul_debugobj(cl, "copy"));

	if (scols_column_set_color(ret, cl->color))
		goto err;
	if (scols_cell_copy_content(&ret->header, &cl->header))
		goto err;

	ret->width	= cl->width;
	ret->width_min	= cl->width_min;
	ret->width_max	= cl->width_max;
	ret->width_avg	= cl->width_avg;
	ret->width_hint	= cl->width_hint;
	ret->flags	= cl->flags;
	ret->is_extreme = cl->is_extreme;
	ret->is_groups  = cl->is_groups;

	return ret;
err:
	scols_unref_column(ret);
	return NULL;
}

/**
 * scols_column_set_whint:
 * @cl: a pointer to a struct libscols_column instance
 * @whint: a width hint
 *
 * Sets the width hint of column @cl to @whint. See scols_table_new_column().
 *
 * Returns: 0, a negative value in case of an error.
 */
int scols_column_set_whint(struct libscols_column *cl, double whint)
{
	if (!cl)
		return -EINVAL;

	cl->width_hint = whint;
	return 0;
}

/**
 * scols_column_get_whint:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Returns: The width hint of column @cl, a negative value in case of an error.
 */
double scols_column_get_whint(const struct libscols_column *cl)
{
	return cl->width_hint;
}

/**
 * scols_column_set_flags:
 * @cl: a pointer to a struct libscols_column instance
 * @flags: a flag mask
 *
 * Sets the flags of @cl to @flags.
 *
 * Returns: 0, a negative value in case of an error.
 */
int scols_column_set_flags(struct libscols_column *cl, int flags)
{
	if (!cl)
		return -EINVAL;

	if (cl->table) {
		if (!(cl->flags & SCOLS_FL_TREE) && (flags & SCOLS_FL_TREE))
			cl->table->ntreecols++;
		else if ((cl->flags & SCOLS_FL_TREE) && !(flags & SCOLS_FL_TREE))
			cl->table->ntreecols--;
	}

	DBG(COL, ul_debugobj(cl, "setting flags from 0%x to 0%x", cl->flags, flags));
	cl->flags = flags;
	return 0;
}

/**
 * scols_column_set_json_type:
 * @cl: a pointer to a struct libscols_column instance
 * @type: SCOLS_JSON_* type
 *
 * Sets the type used for JSON formatting, the default is SCOLS_JSON_STRING.
 *
 * Returns: 0, a negative value in case of an error.
 *
 * Since: 2.33
 */
int scols_column_set_json_type(struct libscols_column *cl, int type)
{
	if (!cl)
		return -EINVAL;

	cl->json_type = type;
	return 0;

}

/**
 * scols_column_get_json_type:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Note that SCOLS_JSON_BOOLEAN interprets NULL, empty strings, '0', 'N' and
 * 'n' as "false"; and everything else as "true".
 *
 * Returns: JSON type used for formatting or a negative value in case of an error.
 *
 * Since: 2.33
 */
int scols_column_get_json_type(const struct libscols_column *cl)
{
	return cl ? cl->json_type : -EINVAL;
}


/**
 * scols_column_get_table:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Returns: pointer to the table where columns is used
 */
struct libscols_table *scols_column_get_table(const struct libscols_column *cl)
{
	return cl->table;
}

/**
 * scols_column_get_flags:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Returns: The flag mask of @cl, a negative value in case of an error.
 */
int scols_column_get_flags(const struct libscols_column *cl)
{
	return cl->flags;
}

/**
 * scols_column_get_header:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Returns: A pointer to a struct libscols_cell instance, representing the
 * header info of column @cl or NULL in case of an error.
 */
struct libscols_cell *scols_column_get_header(struct libscols_column *cl)
{
	return &cl->header;
}

/**
 * scols_column_set_color:
 * @cl: a pointer to a struct libscols_column instance
 * @color: color name or ESC sequence
 *
 * The default color for data cells and column header.
 *
 * If you want to set header specific color then use scols_column_get_header()
 * and scols_cell_set_color().
 *
 * If you want to set data cell specific color the use scols_line_get_cell() +
 * scols_cell_set_color().
 *
 * Returns: 0, a negative value in case of an error.
 */
int scols_column_set_color(struct libscols_column *cl, const char *color)
{
	if (color && isalpha(*color)) {
		color = color_sequence_from_colorname(color);
		if (!color)
			return -EINVAL;
	}
	return strdup_to_struct_member(cl, color, color);
}

/**
 * scols_column_get_color:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Returns: The current color setting of the column @cl.
 */
const char *scols_column_get_color(const struct libscols_column *cl)
{
	return cl->color;
}

/**
 * scols_wrapnl_nextchunk:
 * @cl: a pointer to a struct libscols_column instance
 * @data: string
 * @userdata: callback private data
 *
 * This is built-in function for scols_column_set_wrapfunc(). This function
 * terminates the current chunk by \0 and returns pointer to the begin of
 * the next chunk. The chunks are based on \n.
 *
 * For example for data "AAA\nBBB\nCCC" the next chunk is "BBB".
 *
 * Returns: next chunk
 *
 * Since: 2.29
 */
char *scols_wrapnl_nextchunk(const struct libscols_column *cl __attribute__((unused)),
			char *data,
			void *userdata __attribute__((unused)))
{
	char *p = data ? strchr(data, '\n') : NULL;

	if (p) {
		*p = '\0';
		return p + 1;
	}
	return NULL;
}

/**
 * scols_wrapnl_chunksize:
 * @cl: a pointer to a struct libscols_column instance
 * @data: string
 * @userdata: callback private data
 *
 * Analyzes @data and returns size of the largest chunk. The chunks are based
 * on \n. For example for data "AAA\nBBB\nCCCC" the largest chunk size is 4.
 *
 * Note that the size has to be based on number of terminal cells rather than
 * bytes to support multu-byte output.
 *
 * Returns: size of the largest chunk.
 *
 * Since: 2.29
 */
size_t scols_wrapnl_chunksize(const struct libscols_column *cl __attribute__((unused)),
		const char *data,
		void *userdata __attribute__((unused)))
{
	size_t sum = 0;

	while (data && *data) {
		const char *p;
		size_t sz;

		p = strchr(data, '\n');
		if (p) {
			sz = cl->table && scols_table_is_noencoding(cl->table) ?
					mbs_nwidth(data, p - data) :
					mbs_safe_nwidth(data, p - data, NULL);
			p++;
		} else {
			sz = cl->table && scols_table_is_noencoding(cl->table) ?
					mbs_width(data) :
					mbs_safe_width(data);
		}
		sum = max(sum, sz);
		data = p;
	}

	return sum;
}

/**
 * scols_column_set_cmpfunc:
 * @cl: column
 * @cmp: pointer to compare function
 * @data: private data for cmp function
 *
 * Returns: 0, a negative value in case of an error.
 */
int scols_column_set_cmpfunc(struct libscols_column *cl,
			int (*cmp)(struct libscols_cell *,
				   struct libscols_cell *,
				   void *),
			void *data)
{
	if (!cl)
		return -EINVAL;

	cl->cmpfunc = cmp;
	cl->cmpfunc_data = data;
	return 0;
}

/**
 * scols_column_set_wrapfunc:
 * @cl: a pointer to a struct libscols_column instance
 * @wrap_chunksize: function to return size of the largest chink of data
 * @wrap_nextchunk: function to return next zero terminated data
 * @userdata: optional stuff for callbacks
 *
 * Extends SCOLS_FL_WRAP and can be used to set custom wrap function. The default
 * is to wrap by column size, but you can create functions to wrap for example
 * after \n or after words, etc.
 *
 * Returns: 0, a negative value in case of an error.
 *
 * Since: 2.29
 */
int scols_column_set_wrapfunc(struct libscols_column *cl,
			size_t (*wrap_chunksize)(const struct libscols_column *,
						 const char *,
						 void *),
			char * (*wrap_nextchunk)(const struct libscols_column *,
						 char *,
						 void *),
			void *userdata)
{
	if (!cl)
		return -EINVAL;

	cl->wrap_nextchunk = wrap_nextchunk;
	cl->wrap_chunksize = wrap_chunksize;
	cl->wrapfunc_data = userdata;
	return 0;
}

/**
 * scols_column_set_safechars:
 * @cl: a pointer to a struct libscols_column instance
 * @safe: safe characters (e.g. "\n\t")
 *
 * Use for bytes you don't want to encode on output. This is for example
 * necessary if you want to use custom wrap function based on \n, in this case
 * you have to set "\n" as a safe char.
 *
 * Returns: 0, a negative value in case of an error.
 *
 * Since: 2.29
 */
int scols_column_set_safechars(struct libscols_column *cl, const char *safe)
{
	return strdup_to_struct_member(cl, safechars, safe);
}

/**
 * scols_column_get_safechars:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Returns: safe chars
 *
 * Since: 2.29
 */
const char *scols_column_get_safechars(const struct libscols_column *cl)
{
	return cl->safechars;
}

/**
 * scols_column_get_width:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Important note: the column width is unknown until library starts printing
 * (width is calculated before printing). The function is usable for example in
 * nextchunk() callback specified by scols_column_set_wrapfunc().
 *
 * See also scols_column_get_whint(), it returns wanted size (!= final size).
 *
 * Returns: column width
 *
 * Since: 2.29
 */
size_t scols_column_get_width(const struct libscols_column *cl)
{
	return cl->width;
}

/**
 * scols_column_is_hidden:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Gets the value of @cl's flag hidden.
 *
 * Returns: 0 or 1
 *
 * Since: 2.27
 */
int scols_column_is_hidden(const struct libscols_column *cl)
{
	return cl->flags & SCOLS_FL_HIDDEN ? 1 : 0;
}

/**
 * scols_column_is_trunc:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Gets the value of @cl's flag trunc.
 *
 * Returns: 0 or 1
 */
int scols_column_is_trunc(const struct libscols_column *cl)
{
	return cl->flags & SCOLS_FL_TRUNC ? 1 : 0;
}
/**
 * scols_column_is_tree:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Gets the value of @cl's flag tree.
 *
 * Returns: 0 or 1
 */
int scols_column_is_tree(const struct libscols_column *cl)
{
	return cl->flags & SCOLS_FL_TREE ? 1 : 0;
}
/**
 * scols_column_is_right:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Gets the value of @cl's flag right.
 *
 * Returns: 0 or 1
 */
int scols_column_is_right(const struct libscols_column *cl)
{
	return cl->flags & SCOLS_FL_RIGHT ? 1 : 0;
}
/**
 * scols_column_is_strict_width:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Gets the value of @cl's flag strict_width.
 *
 * Returns: 0 or 1
 */
int scols_column_is_strict_width(const struct libscols_column *cl)
{
	return cl->flags & SCOLS_FL_STRICTWIDTH ? 1 : 0;
}
/**
 * scols_column_is_noextremes:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Gets the value of @cl's flag no_extremes.
 *
 * Returns: 0 or 1
 */
int scols_column_is_noextremes(const struct libscols_column *cl)
{
	return cl->flags & SCOLS_FL_NOEXTREMES ? 1 : 0;
}
/**
 * scols_column_is_wrap:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Gets the value of @cl's flag wrap.
 *
 * Returns: 0 or 1
 *
 * Since: 2.28
 */
int scols_column_is_wrap(const struct libscols_column *cl)
{
	return cl->flags & SCOLS_FL_WRAP ? 1 : 0;
}
/**
 * scols_column_is_customwrap:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Returns: 0 or 1
 *
 * Since: 2.29
 */
int scols_column_is_customwrap(const struct libscols_column *cl)
{
	return (cl->flags & SCOLS_FL_WRAP)
		&& cl->wrap_chunksize
		&& cl->wrap_nextchunk ? 1 : 0;
}
