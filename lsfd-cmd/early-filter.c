/*
 * early-filter.c - filter mechanism working when collecting fd informations
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

enum early_filter_type {
	ef_pid,
};

struct early_filters {
	struct list_head filters;
};

struct early_filter {
	enum early_filter_type type;
	union {
		pid_t pid;
	};
	struct list_head filters;
};

struct early_filters *new_early_filters(void)
{
	struct early_filters *early_filters = xmalloc(sizeof(*early_filters));
	INIT_LIST_HEAD(&early_filters->filters);
	return early_filters;
}

static void free_early_filter(struct early_filter *ef)
{
	free(ef);
}

void free_early_filters(struct early_filters *early_filters)
{
	list_free(&early_filters->filters, struct early_filter, filters,
		  free_early_filter);
	free(early_filters);
}

void early_filters_optimize(struct early_filters *early_filters)
{
	/* STUB */
}
