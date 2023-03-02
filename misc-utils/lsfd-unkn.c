/*
 * lsfd-unkn.c - handle associations opening unknown objects
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

#include "xalloc.h"
#include "nls.h"
#include "libsmartcols.h"

#include "lsfd.h"

struct unkn {
	struct file file;
	const struct anon_ops *anon_ops;
	void *anon_data;
};

struct anon_ops {
	const char *class;
	char * (*get_name)(struct unkn *);
	/* Return true is handled the column. */
	bool (*fill_column)(struct proc *,
			    struct unkn *,
			    struct libscols_line *,
			    int,
			    size_t,
			    char **str);
	void (*init)(struct unkn *);
	void (*free)(struct unkn *);
	int (*handle_fdinfo)(struct unkn *, const char *, const char *);
};

static const struct anon_ops anon_generic_ops;
static const struct anon_ops anon_pidfd_ops;

static char * anon_get_class(struct unkn *unkn)
{
	char *name;

	if (unkn->anon_ops->class)
		return xstrdup(unkn->anon_ops->class);

	/* See unkn_init_content() */
	name = ((struct file *)unkn)->name + 11;
	/* Does it have the form anon_inode:[class]? */
	if (*name == '[') {
		size_t len = strlen(name + 1);
		if (*(name + 1 + len - 1) == ']')
			return strndup(name + 1, len - 1);
	}

	return xstrdup(name);
}

static bool unkn_fill_column(struct proc *proc,
			     struct file *file,
			     struct libscols_line *ln,
			     int column_id,
			     size_t column_index)
{
	char *str = NULL;
	struct unkn *unkn = (struct unkn *)file;

	switch(column_id) {
	case COL_NAME:
		if (unkn->anon_ops && unkn->anon_ops->get_name) {
			str = unkn->anon_ops->get_name(unkn);
			if (str)
				break;
		}
		return false;
	case COL_TYPE:
		if (!unkn->anon_ops)
			return false;
		/* FALL THROUGH */
	case COL_AINODECLASS:
		if (unkn->anon_ops) {
			str = anon_get_class(unkn);
			break;
		}
		return false;
	case COL_SOURCE:
		if (unkn->anon_ops) {
			str = xstrdup("anon_inodefs");
			break;
		}
		return false;
	default:
		if (unkn->anon_ops && unkn->anon_ops->fill_column) {
			if (unkn->anon_ops->fill_column(proc, unkn, ln,
							column_id, column_index, &str))
				break;
		}
		return false;
	}

	if (!str)
		err(EXIT_FAILURE, _("failed to add output data"));
	if (scols_line_refer_data(ln, column_index, str))
		err(EXIT_FAILURE, _("failed to add output data"));
	return true;
}

static void unkn_init_content(struct file *file)
{
	struct unkn *unkn = (struct unkn *)file;

	assert(file);
	unkn->anon_ops = NULL;
	unkn->anon_data = NULL;

	if (major(file->stat.st_dev) == 0
	    && strncmp(file->name, "anon_inode:", 11) == 0) {
		const char *rest = file->name + 11;

		if (strncmp(rest, "[pidfd]", 7) == 0)
			unkn->anon_ops = &anon_pidfd_ops;
		else
			unkn->anon_ops = &anon_generic_ops;

		if (unkn->anon_ops->init)
			unkn->anon_ops->init(unkn);
	}
}

static void unkn_content_free(struct file *file)
{
	struct unkn *unkn = (struct unkn *)file;

	assert(file);
	if (unkn->anon_ops && unkn->anon_ops->free)
		unkn->anon_ops->free((struct unkn *)file);
}

static int unkn_handle_fdinfo(struct file *file, const char *key, const char *value)
{
	struct unkn *unkn = (struct unkn *)file;

	assert(file);
	if (unkn->anon_ops && unkn->anon_ops->handle_fdinfo)
		return unkn->anon_ops->handle_fdinfo(unkn, key, value);
	return 0;		/* Should be handled in parents */
}

/*
 * pidfd
 */
struct anon_pidfd_data {
	pid_t pid;
	char *nspid;
};

static char *anon_pidfd_get_name(struct unkn *unkn)
{
	char *str = NULL;
	struct anon_pidfd_data *data = (struct anon_pidfd_data *)unkn->anon_data;

	char *comm = NULL;
	struct proc *proc = get_proc(data->pid);
	if (proc)
		comm = proc->command;

	xasprintf(&str, "pid=%d comm=%s nspid=%s",
		  data->pid,
		  comm? comm: "",
		  data->nspid? data->nspid: "");
	return str;
}

static void anon_pidfd_init(struct unkn *unkn)
{
	unkn->anon_data = xcalloc(1, sizeof(struct anon_pidfd_data));
}

static void anon_pidfd_free(struct unkn *unkn)
{
	struct anon_pidfd_data *data = (struct anon_pidfd_data *)unkn->anon_data;

	if (data->nspid)
		free(data->nspid);
	free(data);
}

static int anon_pidfd_handle_fdinfo(struct unkn *unkn, const char *key, const char *value)
{
	if (strcmp(key, "Pid") == 0) {
		uint64_t pid;

		int rc = ul_strtou64(value, &pid, 10);
		if (rc < 0)
			return 0; /* ignore -- parse failed */
		((struct anon_pidfd_data *)unkn->anon_data)->pid = (pid_t)pid;
		return 1;
	}
	else if (strcmp(key, "NSpid") == 0) {
		((struct anon_pidfd_data *)unkn->anon_data)->nspid = xstrdup(value);
		return 1;

	}
	return 0;
}

static bool anon_pidfd_fill_column(struct proc *proc  __attribute__((__unused__)),
				   struct unkn *unkn,
				   struct libscols_line *ln __attribute__((__unused__)),
				   int column_id,
				   size_t column_index __attribute__((__unused__)),
				   char **str)
{
	struct anon_pidfd_data *data = (struct anon_pidfd_data *)unkn->anon_data;

	switch(column_id) {
	case COL_PIDFD_COMM: {
		struct proc *pidfd_proc = get_proc(data->pid);
		char *pidfd_comm = NULL;
		if (pidfd_proc)
			pidfd_comm = pidfd_proc->command;
		if (pidfd_comm) {
			*str = xstrdup(pidfd_comm);
			return true;
		}
		break;
	}
	case COL_PIDFD_NSPID:
		if (data->nspid) {
			*str = xstrdup(data->nspid);
			return true;
		}
		break;
	case COL_PIDFD_PID:
		xasprintf(str, "%d", (int)data->pid);
		return true;
	}

	return false;
}

static const struct anon_ops anon_pidfd_ops = {
	.class = "pidfd",
	.get_name = anon_pidfd_get_name,
	.fill_column = anon_pidfd_fill_column,
	.init = anon_pidfd_init,
	.free = anon_pidfd_free,
	.handle_fdinfo = anon_pidfd_handle_fdinfo,
};

/*
 * generic (fallback implementation)
 */
static const struct anon_ops anon_generic_ops = {
	.class = NULL,
	.get_name = NULL,
	.fill_column = NULL,
	.init = NULL,
	.free = NULL,
	.handle_fdinfo = NULL,
};

const struct file_class unkn_class = {
	.super = &file_class,
	.size = sizeof(struct unkn),
	.fill_column = unkn_fill_column,
	.initialize_content = unkn_init_content,
	.free_content = unkn_content_free,
	.handle_fdinfo = unkn_handle_fdinfo,
};
