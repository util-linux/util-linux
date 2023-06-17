/*
 * smartcols-filter.c - filtering engine extension
 *
 * Copyright (C) 2021 Red Hat, Inc.
 * Copyright (C) 2021 Masatake YAMATO <yamato@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#ifndef UTIL_LINUX_SMARTCOLS_FILTER_H
#define UTIL_LINUX_SMARTCOLS_FILTER_H
#include "libsmartcols.h"
#include <stdio.h>
#include <stdbool.h>

#define SMARTCOLS_FILTER_UNKNOWN_COL_ID -1

struct scols_filter;

/*
 * scols_filter_new:
 * @column_name_to_id: a function converting a column name to its id.
 *
 * Make a filter object.

 * @column_name_to_id should return SMARTCOLS_FILTER_UNKNOWN_COL_ID if
 * an unknown column name is given.
 */
struct scols_filter *scols_filter_new(const char *const expr, struct libscols_table *tb,
				      int ncols,
				      int (*column_name_to_id)(const char *, void *),
				      struct libscols_column *(*add_column_by_id)(struct libscols_table *, int, void*),
				      void *data);

/* Call scols_filter_get_errmsg() after scols_filter_new() to detect
 * whether scols_filter_new() is failed or not. Returning NULL means,
 * scols_filter_new() is successful. */
const char *scols_filter_get_errmsg(struct scols_filter *filter);
void scols_filter_free(struct scols_filter *filter);
bool scols_filter_apply(struct scols_filter *filter, struct libscols_line *ln);

/* Dumping AST. */
void scols_filter_dump(struct scols_filter *filter, FILE *stream);

#endif	/* UTIL_LINUX_SMARTCOLS_FILTER_H */
