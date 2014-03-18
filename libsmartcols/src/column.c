/*
 * column.c - functions for table handling at the column level
 *
 * Copyright (C) 2014 Ondrej Oprala <ooprala@redhat.com>
 * Copyright (C) 2014 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "smartcolsP.h"

struct libscols_column *scols_new_column(void)
{
	struct libscols_column *cl;

	cl = calloc(1, sizeof(*cl));
	if (!cl)
		return NULL;

	cl->refcount = 1;
	INIT_LIST_HEAD(&cl->cl_columns);
	return cl;
}

void scols_ref_column(struct libscols_column *cl)
{
	if (cl)
		cl->refcount++;
}

void scols_unref_column(struct libscols_column *cl)
{
	if (cl && --cl->refcount <= 0) {
		list_del(&cl->cl_columns);
		scols_reset_cell(&cl->header);
		free(cl->color);
		free(cl);
	}
}

struct libscols_column *scols_copy_column(const struct libscols_column *cl)
{
	struct libscols_column *ret;

	assert (cl);
	if (!cl)
		return NULL;
	ret = scols_new_column();
	if (!ret)
		return NULL;
	if (cl->color) {
		ret->color = strdup(cl->color);
		if (!ret->color)
			goto err;
	}

	if (scols_cell_set_data(&ret->header, scols_cell_get_data(&cl->header)))
		goto err;
	if (scols_cell_set_color(&ret->header, scols_cell_get_color(&cl->header)))
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

int scols_column_set_whint(struct libscols_column *cl, double whint)
{
	assert(cl);

	if (!cl)
		return -EINVAL;

	cl->width_hint = whint;
	return 0;
}

double scols_column_get_whint(struct libscols_column *cl)
{
	assert(cl);
	return cl ? cl->width_hint : -EINVAL;
}

int scols_column_set_flags(struct libscols_column *cl, int flags)
{
	assert(cl);

	if (!cl)
		return -EINVAL;

	cl->flags = flags;
	return 0;
}

int scols_column_get_flags(struct libscols_column *cl)
{
	assert(cl);
	return cl ? cl->flags : -EINVAL;
}

const struct libscols_cell *scols_column_get_header(struct libscols_column *cl)
{
	assert(cl);
	return cl ? &cl->header : NULL;
}

/*
 * The default color for data cells and column header.
 *
 * If you want to set header specific color then use scols_column_get_header()
 * and scols_cell_set_color().
 *
 * If you want to set data cell specific color the use scols_line_get_cell() +
 * scols_cell_set_color().
 */
int scols_column_set_color(struct libscols_column *cl, const char *color)
{
	char *p = NULL;

	assert(cl);
	if (!cl)
		return -EINVAL;
	if (color) {
		p = strdup(color);
		if (!p)
			return -ENOMEM;
	}

	free(cl->color);
	cl->color = p;
	return 0;
}

const char *scols_column_get_color(struct libscols_column *cl)
{
	assert(cl);
	return cl ? cl->color : NULL;
}



