/*
 * line.c - functions for table handling at the line level
 *
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

struct libscols_line *scols_new_line(void)
{
	struct libscols_line *ln;

	ln = calloc(1, sizeof(*ln));
	if (!ln)
		return NULL;

	ln->refcount = 1;
	INIT_LIST_HEAD(&ln->ln_lines);
	INIT_LIST_HEAD(&ln->ln_children);
	INIT_LIST_HEAD(&ln->ln_branch);
	return ln;
}

void scols_ref_line(struct libscols_line *ln)
{
	if (ln)
		ln->refcount++;
}

void scols_unref_line(struct libscols_line *ln)
{
	if (ln && --ln->refcount <= 0) {
		size_t i;

		list_del(&ln->ln_lines);
		list_del(&ln->ln_children);

		for (i = 0; i <= ln->ncells; i++)
			scols_reset_cell(ln->cells[i]);
		free(ln->data);
		free(ln->color);
		free(ln);
	}
}

int scols_line_set_userdata(struct libscols_line *ln, void *data)
{
	assert(ln);
	if (!ln)
		return -EINVAL;
	ln->userdata = data;
	return 0;
}

void *scols_line_get_userdata(struct libscols_line *ln)
{
	assert(ln);
	return ln ? ln->userdata : NULL;
}

int scols_line_set_parent(struct libscols_line *ln, struct libscols_line *parent)
{
	assert(ln);
	if (!ln)
		return -EINVAL;
	ln->parent = parent;
	return 0;
}

struct libscols_line *scols_line_get_parent(struct libscols_line *ln)
{
	assert(ln);
	return ln ? ln->parent : NULL;
}

/*
 * The default line color, used when cell color unspecified.
 */
int scols_line_set_color(struct libscols_line *ln, const char *color)
{
	char *p = NULL;

	assert(ln);
	if (!ln)
		return -EINVAL;
	if (color) {
		p = strdup(color);
		if (!p)
			return -ENOMEM;
	}

	free(ln->color);
	ln->color = p;
	return 0;
}

const char *scols_line_get_color(struct libscols_line *ln)
{
	assert(ln);
	return ln ? ln->color : NULL;
}

size_t scols_line_get_datasize(struct libscols_line *ln)
{
	assert(ln);
	return ln ? ln->data_sz : 0;
}

size_t scols_line_get_ncells(struct libscols_line *ln)
{
	assert(ln);
	return ln ? ln->ncells : 0;
}

struct libscols_cell *scols_line_get_cell(struct libscols_line *ln,
					  size_t n)
{
	assert(ln);

	if (!ln || n >= ln->ncells)
		return NULL;
	return ln->cells[n];
}

struct libscols_line *scols_copy_line(struct libscols_line *ln)
{
	struct libscols_line *ret;

	assert (ln);
	if (!ln)
		return NULL;

	ret = scols_new_line();
	if (!ret)
		return NULL;
	if (scols_line_set_color(ret, ln->color))
		goto err;
	ret->userdata = ln->userdata;
	ret->parent   = ln->parent;
	ret->data_sz  = ln->data_sz;
	ret->ncells   = ln->ncells;

	if (ln->ncells) {
		size_t i;

		ret->cells = calloc(ln->ncells, sizeof(struct libscols_cell));
		if (!ret->cells)
			goto err;

		for (i = 0; i < ret->ncells; ++i) {
			if (scols_cell_copy_content(ret->cells[i], ln->cells[i]))
				goto err;
		}
	}

	return ret;
err:
	scols_unref_line(ret);
	return NULL;
}


