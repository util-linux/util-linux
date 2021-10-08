/*
 * lsfd-filter.c - filtering engine for lsfd
 *
 * Copyright (C) 2021 Red Hat, Inc.
 * Copyright (C) 2021 Masatake YAMATO <yamato@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#ifndef UTIL_LINUX_LSFD_FILTER_H
#define UTIL_LINUX_LSFD_FILTER_H

#include "libsmartcols.h"
#include <stdio.h>
#include <stdbool.h>

#define LSFD_FILTER_UNKNOWN_COL_ID -1

struct lsfd_filter;

/*
 * @column_name_to_id: a function converting a column name to its id.
 *
 * @column_name_to_id should return LSFD_FILTER_UNKNOWN_COL_ID if
 * an unknown column name is given.
 */
struct lsfd_filter *lsfd_filter_new(const char *const expr, struct libscols_table *tb,
				      int ncols,
				      int (*column_name_to_id)(const char *, void *),
				      struct libscols_column *(*add_column_by_id)(struct libscols_table *, int, void*),
				      void *data);

/* Call lsfd_filter_get_errmsg() after lsfd_filter_new() to detect
 * whether lsfd_filter_new() is failed or not. Returning NULL means,
 * lsfd_filter_new() is successful. */
const char *lsfd_filter_get_errmsg(struct lsfd_filter *filter);
void lsfd_filter_free(struct lsfd_filter *filter);
bool lsfd_filter_apply(struct lsfd_filter *filter, struct libscols_line *ln);

/* Dumping AST. */
void lsfd_filter_dump(struct lsfd_filter *filter, FILE *stream);

#endif	/* UTIL_LINUX_LSFD_FILTER_H */
