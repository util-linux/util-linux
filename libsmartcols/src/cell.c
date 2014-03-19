/*
 * cell.c - functions for table handling at the cell level
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

/*
 * The cell has no ref-counting, free() and new() functions. All is
 * handled by libscols_line.
 */
int scols_reset_cell(struct libscols_cell *ce)
{
	assert(ce);

	if (!ce)
		return -EINVAL;

	if (!ce->is_ref)
		free(ce->data);
	free(ce->color);
	memset(ce, 0, sizeof(*ce));
	return 0;
}

/* stores copy of the @str to cell */
int scols_cell_set_data(struct libscols_cell *ce, const char *str)
{
	char *p = NULL;

	assert(ce);

	if (!ce)
		return -EINVAL;
	if (str) {
		p = strdup(str);
		if (!p)
			return -ENOMEM;
	}
	if (!ce->is_ref)
		free(ce->data);
	ce->data = p;
	ce->is_ref = 0;
	return 0;
}

/* add reference to @str to cell */
int scols_cell_refer_data(struct libscols_cell *ce, char *str)
{
	char *p = NULL;

	assert(ce);

	if (!ce)
		return -EINVAL;
	if (!ce->is_ref)
		free(ce->data);
	ce->data = p;
	ce->is_ref = 1;
	return 0;
}

const char *scols_cell_get_data(const struct libscols_cell *ce)
{
	assert(ce);
	return ce ? ce->data : NULL;
}

int scols_cell_set_color(struct libscols_cell *ce, const char *color)
{
	char *p = NULL;

	assert(ce);

	if (!ce)
		return -EINVAL;
	if (color) {
		p = strdup(color);
		if (!p)
			return -ENOMEM;
	}
	free(ce->color);
	ce->color = p;
	return 0;
}

const char *scols_cell_get_color(const struct libscols_cell *ce)
{
	assert(ce);
	return ce ? ce->color : NULL;
}

int scols_cell_copy_content(struct libscols_cell *dest,
			    const struct libscols_cell *src)
{
	int rc;

	assert(dest);
	assert(src);

	rc = scols_cell_set_data(dest, scols_cell_get_data(src));
	if (!rc)
		rc = scols_cell_set_color(dest, scols_cell_get_color(src));
	return rc;
}
