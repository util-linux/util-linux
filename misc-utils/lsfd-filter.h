/*
 * lsfd-filter.c - filtering engine for lsfd
 *
 * Copyright (C) 2021 Red Hat, Inc. All rights reserved.
 * Written by Masatake YAMATO <yamato@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef UTIL_LINUX_LSFD_FILTER_H
#define UTIL_LINUX_LSFD_FILTER_H

#include "libsmartcols.h"
#include <stdio.h>
#include <stdbool.h>

struct lsfd_filter;

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
