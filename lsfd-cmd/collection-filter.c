/*
 * collection-filter.c - filter mechanism working when collecting fd informations
 *
 * Copyright (C) 2024 Red Hat, Inc. All rights reserved.
 * Written by Masatake YAMATO <yamato@redhat.com>
 *
 * Very generally based on lsof(8) by Victor A. Abell <abe@purdue.edu>
 * It supports multiple OSes. lsfd specializes to Linux.
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

#include "lsfd.h"

#include "list.h"

enum cl_filter_type {
	clf_pid,
};

struct cl_filters {
	struct list_head filters;
};

struct cl_filter {
	enum cl_filter_type type;
	union {
		pid_t pid;
	};
	struct list_head filters;
};

struct cl_filters *new_cl_filters(void)
{
	struct cl_filters *cl_filters = xmalloc(sizeof(*cl_filters));
	INIT_LIST_HEAD(&cl_filters->filters);
	return cl_filters;
}

static void free_cl_filter(struct cl_filter *clf)
{
	free(clf);
}

void free_cl_filters(struct cl_filters *cl_filters)
{
	list_free(&cl_filters->filters, struct cl_filter, filters,
		  free_cl_filter);
	free(cl_filters);
}

void cl_filters_optimize(struct cl_filters *cl_filters __attribute__((unused)))
{
	/* STUB */
}
