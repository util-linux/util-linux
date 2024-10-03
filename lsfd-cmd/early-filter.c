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
	unsigned int n_pid_filters;

	pid_t *pids;
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
	early_filters->n_pid_filters = 0;
	early_filters->pids = NULL;
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
	free(early_filters->pids);

	free(early_filters);
}

static int pidcmp(const void *a, const void *b)
{
	pid_t pa = *(pid_t *)a;
	pid_t pb = *(pid_t *)b;

	if (pa < pb)
		return -1;
	else if (pa == pb)
		return 0;
	else
		return 1;
}

static void sort_pids(pid_t pids[], const int count)
{
	qsort(pids, count, sizeof(pid_t), pidcmp);
}

void early_filters_optimize(struct early_filters *early_filters)
{
	if (early_filters->n_pid_filters > 0) {
		int i = 0;
		struct list_head *ef;

		early_filters->pids = xmalloc(sizeof(early_filters->pids[0]) *
					      early_filters->n_pid_filters);


		list_for_each(ef, &early_filters->filters) {
			struct early_filter *early_filter = list_entry(ef,
								       struct early_filter,
								       filters);
			if (early_filter->type == ef_pid)
				early_filters->pids[i++] = early_filter->pid;
		}
		sort_pids(early_filters->pids, early_filters->n_pid_filters);
	}
}

static struct early_filter *new_early_filter_pid(pid_t pid)
{
	struct early_filter *ef = xmalloc(sizeof(*ef));
	ef->type = ef_pid;
	INIT_LIST_HEAD(&ef->filters);
	ef->pid = pid;
	return ef;
}

void early_filters_add_pid(struct early_filters *early_filters, pid_t pid)
{
	struct early_filter *ef = new_early_filter_pid(pid);
	list_add_tail(&ef->filters, &early_filters->filters);
	early_filters->n_pid_filters++;
}

bool early_filters_has_pid_filter(struct early_filters *early_filters)
{
	return early_filters->n_pid_filters > 0;
}

bool early_filters_apply_pid(struct early_filters *early_filters, pid_t pid)
{
	if (!early_filters_has_pid_filter(early_filters))
		return true;

	return bsearch(&pid, early_filters->pids, early_filters->n_pid_filters, sizeof(pid_t), pidcmp)
		? true
		: false;
}
