/*
 * smartcols-counter.c - counter implementation stacked on the filter engine extension
 *
 * Copyright (C) 2021 Red Hat, Inc.
 * Copyright (C) 2021 Masatake YAMATO <yamato@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include "smartcols-counter.h"

#include "xalloc.h"

struct scols_counter {
	char *name;
	uint64_t value;
	struct scols_filter *filter;
};

struct scols_counter *scols_counter_new(const char *const name, struct scols_filter *filter)
{
	struct scols_counter *counter = xmalloc(sizeof(struct scols_counter));

	counter->name = xstrdup(name);
	counter->value = 0;
	counter->filter = filter;

	return counter;
}

void scols_counter_free(struct scols_counter *counter)
{
	scols_filter_free(counter->filter);
	free(counter->name);
	free(counter);
}

bool scols_counter_accumulate(struct scols_counter *counter, struct libscols_line *ln)
{
	if (scols_filter_apply(counter->filter, ln)) {
		counter->value++;
		return true;
	}
	return false;
}

const char *scols_counter_name(struct scols_counter *counter)
{
	return counter->name;
}

uint64_t scols_counter_value(struct scols_counter *counter)
{
	return counter->value;
}
