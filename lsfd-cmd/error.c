/*
 * lsfd(1) - list file descriptors
 *
 * Copyright (C) 2021-2026 Red Hat, Inc. All rights reserved.
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

/*
 * Error classes
 */

/* get_errno_name() --- the private replacement of strerrorname_np(3).
 * Some platforms don't have strerrorname_np.
 *
 * Mainly copied from misc-utils/enosys.c.
 */
struct errno_s {
	const char *const name;
	long number;
};

static const struct errno_s errnos[] = {
#define UL_ERRNO(name, nr) { name, nr },
#include "errnos.h"
#undef UL_ERRNO
};

static const char *get_errno_name(int ern)
{
	for (size_t i = 0; i < ARRAY_SIZE(errnos); i ++) {
		if (errnos[i].number == ern)
			return errnos[i].name;
	}
	return NULL;
}

static bool error_fill_column(struct proc *proc __attribute__((__unused__)),
			      struct file *file,
			      struct libscols_line *ln,
			      int column_id,
			      size_t column_index)
{
	char *str = NULL;
	const char *ename;

	switch(column_id) {
	case COL_TYPE:
		if (scols_line_set_data(ln, column_index, "ERROR"))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_SOURCE:
		ename = get_errno_name(file->error.number);
		if (ename)
			xasprintf(&str, "%s:%s",
				  file->error.syscall, ename);
		else
			xasprintf(&str, "%s:unknown(%d)",
				  file->error.syscall, file->error.number);
		if (scols_line_refer_data(ln, column_index, str))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	default:
		return false;
	}
}

static void error_file_free_content(struct file *file)
{
	free(file->name);	/* NULL is acceptable.  */
}

static const struct file_class error_class = {
	.super = &abst_class,
	.size = sizeof(struct file),
	.free_content = error_file_free_content,
	.fill_column = error_fill_column,
};

static bool readlink_error_fill_column(struct proc *proc __attribute__((__unused__)),
				       struct file *file __attribute__((__unused__)),
				       struct libscols_line *ln __attribute__((__unused__)),
				       int column_id,
				       size_t column_index __attribute__((__unused__)))
{
	switch(column_id) {
	case COL_NAME:
	case COL_KNAME:
		return true;
	default:
		return false;
	}
}

const struct file_class readlink_error_class = {
	.super = &error_class,
	.size = sizeof(struct file),
	.fill_column = readlink_error_fill_column,
};

const struct file_class stat_error_class = {
	.super = &error_class,
	.size = sizeof(struct file),
};

bool is_error_object(struct file *f)
{
	const struct file_class *c;

	assert(f);
	c = f->class;

	while (c) {
		if (c == &error_class)
			return true;
		c = c->super;
	}

	return false;
}
