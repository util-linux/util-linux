/*
 * lsfd(1) - list file descriptors
 *
 * Copyright (C) 2021 Red Hat, Inc. All rights reserved.
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

#include "xalloc.h"
#include "nls.h"

#include "libsmartcols.h"

#include "lsfd.h"

static const char *strftype(mode_t ftype)
{
	switch (ftype) {
	case S_IFBLK:
		return "BLK";
	case S_IFCHR:
		return "CHR";
	case S_IFDIR:
		return "DIR";
	case S_IFIFO:
		return "FIFO";
	case S_IFLNK:
		return "LINK";
	case S_IFREG:
		return "REG";
	case S_IFSOCK:
		return "SOCK";
	default:
		return "UNKN";
	}
}

static bool file_fill_column(struct proc *proc,
			     struct file *file,
			     struct libscols_line *ln,
			     int column_id,
			     size_t column_index)
{
	char *str = NULL;
	mode_t ftype;

	switch(column_id) {
	case COL_COMMAND:
		if (proc->command && scols_line_set_data(ln, column_index, proc->command))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_NAME:
		if (file->name && scols_line_set_data(ln, column_index, file->name))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_TYPE:
		ftype = file->stat.st_mode & S_IFMT;
		if (scols_line_set_data(ln, column_index, strftype(ftype)))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_PID:
		xasprintf(&str, "%d", (int)proc->pid);
		if (!str)
			err(EXIT_FAILURE, _("failed to add output data"));
		if (scols_line_refer_data(ln, column_index, str))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	};

	return false;
}

static void file_free_content(struct file *file)
{
	free(file->name);
}

struct file *make_file(const struct file_class *class,
		       struct stat *sb, const char *name)
{
	struct file *file = xcalloc(1, class->size);

	file->class = class;
	file->name = xstrdup(name);
	file->stat = *sb;
	return file;
}

const struct file_class file_class = {
	.super = NULL,
	.size = 0,
	.fill_column = file_fill_column,
	.free_content = file_free_content,
};
