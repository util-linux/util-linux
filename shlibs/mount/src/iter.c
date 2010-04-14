/*
 * Copyright (C) 2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: iter
 * @title: Iterator
 * @short_description: unified iterator
 *
 * The iterator keeps direction and last position for access to the internal
 * library tables/lists.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mountP.h"

/**
 * mnt_new_iter:
 * @direction: MNT_INTER_{FOR,BACK}WARD direction
 *
 * Returns: newly allocated generic libmount iterator.
 */
mnt_iter *mnt_new_iter(int direction)
{
	mnt_iter *itr = calloc(1, sizeof(struct _mnt_iter));
	if (!itr)
		return NULL;
	itr->direction = direction;
	return itr;
}

/**
 * mnt_free_iter:
 * @itr: iterator pointer
 *
 * Deallocates iterator.
 */
void mnt_free_iter(mnt_iter *itr)
{
	free(itr);
}

/**
 * mnt_reset_iter:
 * @itr: iterator pointer
 * @direction: MNT_INTER_{FOR,BACK}WARD or -1 to keep the derection unchanged
 *
 * Resets iterator.
 */
void mnt_reset_iter(mnt_iter *itr, int direction)
{
	assert(itr);

	if (direction == -1)
		direction = itr->direction;

	if (itr) {
		memset(itr, 0, sizeof(struct _mnt_iter));
		itr->direction = direction;
	}
}

/**
 * mnt_iter_get_direction:
 * @itr: iterator pointer
 *
 * Returns: MNT_INTER_{FOR,BACK}WARD or -1 in case of error.
 */
int mnt_iter_get_direction(mnt_iter *itr)
{
	assert(itr);
	return itr ? itr->direction : -1;
}
