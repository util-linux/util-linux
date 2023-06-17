/*
 * smartcols-counter.h - counter implementation stacked on the filter engine extension
 *
 * Copyright (C) 2021 Red Hat, Inc.
 * Copyright (C) 2021 Masatake YAMATO <yamato@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#ifndef UTIL_LINUX_SMARTCOLS_COUNTER_H
#define UTIL_LINUX_SMARTCOLS_COUNTER_H

#include "libsmartcols.h"
#include "smartcols-filter.h"
#include <stdbool.h>
#include <stdint.h>

struct scols_counter;

/* The created counter takes the ownership of the filter; the filter is
 * freed in scols_counter_free().
 */
struct scols_counter *scols_counter_new(const char *const name, struct scols_filter *filter);
void scols_counter_free(struct scols_counter *counter);

bool scols_counter_accumulate(struct scols_counter *counter, struct libscols_line *ln);

const char *scols_counter_name(struct scols_counter *counter);
uint64_t scols_counter_value(struct scols_counter *counter);

#endif	/* UTIL_LINUX_SMARTCOLS_COUNTER_H */
