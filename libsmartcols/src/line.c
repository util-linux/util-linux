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

/*
 * Note that the line is allocated without cells, the cells will be allocated
 * later when you add the line to the table. If you want to use the line
 * without table then you have to explicitly allocate the cells by
 * scols_line_alloc_cells().
 */
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
		list_del(&ln->ln_lines);
		list_del(&ln->ln_children);

		scols_line_free_cells(ln);
		free(ln->color);
		free(ln);
		return;
	}
}

void scols_line_free_cells(struct libscols_line *ln)
{
	size_t i;

	if (!ln || !ln->cells)
		return;

	for (i = 0; i < ln->ncells; i++)
		scols_reset_cell(&ln->cells[i]);

	free(ln->cells);
	ln->ncells = 0;
	ln->cells = NULL;
}


int scols_line_alloc_cells(struct libscols_line *ln, size_t n)
{
	struct libscols_cell *ce;

	assert(ln);

	if (!ln)
		return -EINVAL;
	if (ln->ncells == n)
		return 0;

	if (!n) {
		scols_line_free_cells(ln);
		return 0;
	}

	ce = realloc(ln->cells, n * sizeof(struct libscols_cell));
	if (!ce)
		return -errno;

	if (n > ln->ncells)
		memset(ce + ln->ncells, 0,
		       (n - ln->ncells) * sizeof(struct libscols_cell));

	ln->cells = ce;
	ln->ncells = n;
	return 0;
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

int scols_line_remove_child(struct libscols_line *ln, struct libscols_line *child)
{
	assert(ln);
	assert(child);

	if (!ln || !child)
		return -EINVAL;
	list_del_init(&child->ln_children);
	scols_unref_line(child);

	child->parent = NULL;
	scols_unref_line(ln);
	return 0;
}

int scols_line_add_child(struct libscols_line *ln, struct libscols_line *child)
{
	assert(ln);
	assert(child);

	if (!ln || !child)
		return -EINVAL;

	/* unref old<->parent */
	if (child->parent)
		scols_line_remove_child(child->parent, child);

	/* new reference from parent to child */
	list_add_tail(&child->ln_children, &ln->ln_branch);
	scols_ref_line(child);

	/* new reference from child to parent */
	child->parent = ln;
	scols_ref_line(ln);

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
	return &ln->cells[n];
}

/* just shortcut */
int scols_line_set_data(struct libscols_line *ln, size_t n, const char *data)
{
	struct libscols_cell *ce = scols_line_get_cell(ln, n);

	if (!ce)
		return -EINVAL;
	return scols_cell_set_data(ce, data);
}

/* just shortcut */
int scols_line_refer_data(struct libscols_line *ln, size_t n, char *data)
{
	struct libscols_cell *ce = scols_line_get_cell(ln, n);

	if (!ce)
		return -EINVAL;
	return scols_cell_refer_data(ce, data);
}

struct libscols_line *scols_copy_line(struct libscols_line *ln)
{
	struct libscols_line *ret;
	size_t i;

	assert (ln);
	if (!ln)
		return NULL;

	ret = scols_new_line();
	if (!ret)
		return NULL;
	if (scols_line_set_color(ret, ln->color))
		goto err;
	if (scols_line_alloc_cells(ret, ln->ncells))
		goto err;

	ret->userdata = ln->userdata;
	ret->ncells   = ln->ncells;
	ret->seqnum   = ln->seqnum;

	for (i = 0; i < ret->ncells; ++i) {
		if (scols_cell_copy_content(&ret->cells[i], &ln->cells[i]))
			goto err;
	}

	return ret;
err:
	scols_unref_line(ret);
	return NULL;
}


