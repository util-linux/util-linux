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
	clf_name,
	clf_devino,
};

struct cl_filters {
	struct list_head filters;
	unsigned int n_pid_filters;
	unsigned int n_name_filters;
	unsigned int n_devino_filters;

	pid_t *pids;
};

struct devino {
	dev_t dev;
	ino_t ino;
};

struct cl_filter {
	enum cl_filter_type type;
	union {
		pid_t pid;
		struct {
			const char *name;
			size_t name_len;
		};
		struct devino file;
	};
	struct list_head filters;
};

struct cl_filters *new_cl_filters(void)
{
	struct cl_filters *cl_filters = xmalloc(sizeof(*cl_filters));
	INIT_LIST_HEAD(&cl_filters->filters);
	cl_filters->n_pid_filters = 0;
	cl_filters->n_name_filters = 0;
	cl_filters->n_devino_filters = 0;
	cl_filters->pids = NULL;
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
	free(cl_filters->pids);

	free(cl_filters);
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

void cl_filters_optimize(struct cl_filters *cl_filters)
{
	if (cl_filters->n_pid_filters > 0) {
		int i = 0;
		struct list_head *clf;

		cl_filters->pids = xmalloc(sizeof(cl_filters->pids[0]) *
					   cl_filters->n_pid_filters);


		list_for_each(clf, &cl_filters->filters) {
			struct cl_filter *cl_filter = list_entry(clf,
								 struct cl_filter,
								 filters);
			if (cl_filter->type == clf_pid)
				cl_filters->pids[i++] = cl_filter->pid;
		}
		sort_pids(cl_filters->pids, cl_filters->n_pid_filters);
	}
}

static bool cl_filters_apply (struct cl_filters *cl_filters,
			      bool (*predicate)(struct cl_filter *, const void *), const void *data)
{
	struct list_head *clf;

	list_for_each(clf, &cl_filters->filters) {
		struct cl_filter *cl_filter = list_entry(clf,
							 struct cl_filter,
							 filters);
		if (predicate(cl_filter, data))
			return true;
	}

	return false;
}

static struct cl_filter *new_cl_filter_pid(pid_t pid)
{
	struct cl_filter *clf = xmalloc(sizeof(*clf));
	clf->type = clf_pid;
	INIT_LIST_HEAD(&clf->filters);
	clf->pid = pid;
	return clf;
}

void cl_filters_add_pid(struct cl_filters *cl_filters, pid_t pid)
{
	struct cl_filter *clf = new_cl_filter_pid(pid);
	list_add_tail(&clf->filters, &cl_filters->filters);
	cl_filters->n_pid_filters++;
}

bool cl_filters_has_pid_filter(struct cl_filters *cl_filters)
{
	return cl_filters->n_pid_filters > 0;
}

bool cl_filters_apply_pid(struct cl_filters *cl_filters, pid_t pid)
{
	if (!cl_filters_has_pid_filter(cl_filters))
		return true;

	return bsearch(&pid, cl_filters->pids, cl_filters->n_pid_filters, sizeof(pid_t), pidcmp)
		? true
		: false;
}

static struct cl_filter *new_cl_filter_name(const char *name)
{
	struct cl_filter *clf = xmalloc(sizeof(*clf));
	clf->type = clf_name;
	INIT_LIST_HEAD(&clf->filters);
	clf->name = name;
	clf->name_len = strlen(name);
	return clf;
}

void cl_filters_add_name(struct cl_filters *cl_filters, const char *name)
{
	struct cl_filter *clf = new_cl_filter_name(name);
	list_add_tail(&clf->filters, &cl_filters->filters);
	cl_filters->n_name_filters++;
}

bool cl_filters_has_name(struct cl_filters *cl_filters)
{
	return cl_filters->n_name_filters > 0;
}

static bool name_equal(struct cl_filter *cl_filter, const void *data)
{
	const char *name = data;

	if (cl_filter->type != clf_name)
		return false;

	if (strncmp(cl_filter->name, name, cl_filter->name_len) == 0) {
		const char *rest;
		if (name[cl_filter->name_len] == '\0')
			return true;

		rest = name + cl_filter->name_len;
		if (strcmp(rest, " (deleted)") == 0)
			return true;
	}

	return false;
}

bool cl_filters_apply_name(struct cl_filters *cl_filters, const char *name)
{
	if (!cl_filters_has_name(cl_filters))
		return true;

	return cl_filters_apply(cl_filters, name_equal, name);
}

static struct cl_filter *new_cl_filter_devino(dev_t dev, ino_t ino)
{
	struct cl_filter *clf = xmalloc(sizeof(*clf));
	clf->type = clf_devino;
	INIT_LIST_HEAD(&clf->filters);
	clf->file.dev = dev;
	clf->file.ino = ino;
	return clf;
}

void cl_filters_add_devino(struct cl_filters *cl_filters, dev_t dev, ino_t ino)
{
	struct cl_filter *clf = new_cl_filter_devino(dev, ino);
	list_add_tail(&clf->filters, &cl_filters->filters);
	cl_filters->n_devino_filters++;
}

bool cl_filters_has_devino(struct cl_filters *cl_filters)
{
	return cl_filters->n_devino_filters > 0;
}

static bool devino_equal(struct cl_filter *cl_filter, const void *data)
{
	const struct devino *devino = data;

	if (cl_filter->type != clf_devino)
		return false;

	return (devino->dev == cl_filter->file.dev
		&& devino->ino == cl_filter->file.ino);
}

bool cl_filters_apply_devino(struct cl_filters *cl_filters, dev_t dev, ino_t ino)
{
	struct devino devino;

	if (!cl_filters_has_devino(cl_filters))
		return true;

	devino.dev = dev;
	devino.ino = ino;

	return cl_filters_apply(cl_filters, devino_equal, &devino);
}
