/*
 * lsfd-counter.c - counter implementation used in --summary option
 *
 * Copyright (C) 2021 Red Hat, Inc.
 * Copyright (C) 2021 Masatake YAMATO <yamato@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include "lsfd-counter.h"

#include "xalloc.h"

struct lsfd_counter {
	char *name;
	size_t value;
	struct lsfd_filter *filter;
};

struct lsfd_counter *lsfd_counter_new(const char *const name, struct lsfd_filter *filter)
{
	struct lsfd_counter *counter = xmalloc(sizeof(struct lsfd_counter));

	counter->name = xstrdup(name);
	counter->value = 0;
	counter->filter = filter;

	return counter;
}

void lsfd_counter_free(struct lsfd_counter *counter)
{
	lsfd_filter_free(counter->filter);
	free(counter->name);
	free(counter);
}

bool lsfd_counter_accumulate(struct lsfd_counter *counter, struct libscols_line *ln)
{
	if (lsfd_filter_apply(counter->filter, ln)) {
		counter->value++;
		return true;
	}
	return false;
}

const char *lsfd_counter_name(struct lsfd_counter *counter)
{
	return counter->name;
}

size_t lsfd_counter_value(struct lsfd_counter *counter)
{
	return counter->value;
}
