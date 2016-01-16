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

#include "smartcolsP.h"

/**
 * scols_new_column:
 *
 * Allocates space for a new column.
 *
 * Returns: a pointer to a new struct libscols_cell instance, NULL in case of an ENOMEM error.
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

	DBG(COL, ul_debugobj((void *) cl, "copy to %p", ret));

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
 * Sets the width hint of column @cl to @whint.
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
double scols_column_get_whint(struct libscols_column *cl)
{
	assert(cl);
	return cl ? cl->width_hint : -EINVAL;
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

	cl->flags = flags;
	return 0;
}

/**
 * scols_column_get_flags:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Returns: The flag mask of @cl, a negative value in case of an error.
 */
int scols_column_get_flags(struct libscols_column *cl)
{
	assert(cl);
	return cl ? cl->flags : -EINVAL;
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
	return cl ? &cl->header : NULL;
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
	char *p = NULL;

	if (!cl)
		return -EINVAL;
	if (color) {
		if (isalpha(*color)) {
			color = color_sequence_from_colorname(color);

			if (!color)
				return -EINVAL;
		}
		p = strdup(color);
		if (!p)
			return -ENOMEM;
	}

	free(cl->color);
	cl->color = p;
	return 0;
}

/**
 * scols_column_get_color:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Returns: The current color setting of the column @cl.
 */
const char *scols_column_get_color(struct libscols_column *cl)
{
	assert(cl);
	return cl ? cl->color : NULL;
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
 * scols_column_is_hidden:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Gets the value of @cl's flag hidden.
 *
 * Returns: hidden flag value, negative value in case of an error.
 */
int scols_column_is_hidden(struct libscols_column *cl)
{
	if (!cl)
		return -EINVAL;
	return cl->flags & SCOLS_FL_HIDDEN;
}

/**
 * scols_column_is_trunc:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Gets the value of @cl's flag trunc.
 *
 * Returns: trunc flag value, negative value in case of an error.
 */
int scols_column_is_trunc(struct libscols_column *cl)
{
	if (!cl)
		return -EINVAL;
	return cl->flags & SCOLS_FL_TRUNC;
}
/**
 * scols_column_is_tree:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Gets the value of @cl's flag tree.
 *
 * Returns: tree flag value, negative value in case of an error.
 */
int scols_column_is_tree(struct libscols_column *cl)
{
	if (!cl)
		return -EINVAL;
	return cl->flags & SCOLS_FL_TREE;
}
/**
 * scols_column_is_right:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Gets the value of @cl's flag right.
 *
 * Returns: right flag value, negative value in case of an error.
 */
int scols_column_is_right(struct libscols_column *cl)
{
	if (!cl)
		return -EINVAL;
	return cl->flags & SCOLS_FL_RIGHT;
}
/**
 * scols_column_is_strict_width:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Gets the value of @cl's flag strict_width.
 *
 * Returns: strict_width flag value, negative value in case of an error.
 */
int scols_column_is_strict_width(struct libscols_column *cl)
{
	if (!cl)
		return -EINVAL;
	return cl->flags & SCOLS_FL_STRICTWIDTH;
}
/**
 * scols_column_is_noextremes:
 * @cl: a pointer to a struct libscols_column instance
 *
 * Gets the value of @cl's flag no_extremes.
 *
 * Returns: no_extremes flag value, negative value in case of an error.
 */
int scols_column_is_noextremes(struct libscols_column *cl)
{
	if (!cl)
		return -EINVAL;
	return cl->flags & SCOLS_FL_NOEXTREMES;
}
