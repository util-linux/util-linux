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

	free(ce->data);
	free(ce->color);
	memset(ce, 0, sizeof(*ce));
	return 0;
}

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
	free(ce->data);
	ce->data = p;
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

