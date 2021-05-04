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

static const char *assocstr[N_ASSOCS] = {
	[ASSOC_CWD]       = "cwd",
	[ASSOC_EXE]       = "exe",
	/* "root" appears as user names, too.
	 * So we use "rtd" here instead of "root". */
	[ASSOC_ROOT]      = "rtd",
	[ASSOC_NS_CGROUP] = "cgroup",
	[ASSOC_NS_IPC]    = "ipc",
	[ASSOC_NS_MNT]    = "mnt",
	[ASSOC_NS_NET]    = "net",
	[ASSOC_NS_PID]    = "pid",
	[ASSOC_NS_PID4C]  = "pid4c",
	[ASSOC_NS_TIME]   = "time",
	[ASSOC_NS_TIME4C] = "time4c",
	[ASSOC_NS_USER]   = "user",
	[ASSOC_NS_UTS]    = "uts",
	[ASSOC_MEM]       = "mem",
};

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
		if (proc->command
		    && scols_line_set_data(ln, column_index, proc->command))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_NAME:
		if (file->name
		    && scols_line_set_data(ln, column_index, file->name))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_TYPE:
		ftype = file->stat.st_mode & S_IFMT;
		if (scols_line_set_data(ln, column_index, strftype(ftype)))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_USER:
		add_uid(username_cache, (int)proc->uid);
		if (scols_line_set_data(ln, column_index,
					get_id(username_cache,
					       (int)proc->uid)->name))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_FD:
		if (file->association < 0)
			return false;
		/* FALL THROUGH */
	case COL_ASSOC:
		if (file->association >= 0)
			xasprintf(&str, "%d", file->association);
		else {
			int assoc = file->association * -1;
			if (assoc >= N_ASSOCS)
				return false; /* INTERNAL ERROR */
			xasprintf(&str, "%s", assocstr[assoc]);
		}
		break;
	case COL_INODE:
		xasprintf(&str, "%llu", (unsigned long long)file->stat.st_ino);
		break;
	case COL_DEV:
	case COL_DEVICE:
		xasprintf(&str, "%u:%u",
			  major(file->stat.st_dev),
			  minor(file->stat.st_dev));
		break;
	case COL_RDEV:
		xasprintf(&str, "%u:%u",
			  major(file->stat.st_rdev),
			  minor(file->stat.st_rdev));
		break;
	case COL_PID:
		xasprintf(&str, "%d", (int)proc->pid);
		break;
	case COL_UID:
		xasprintf(&str, "%d", (int)proc->uid);
		break;
	case COL_SIZE:
		xasprintf(&str, "%ju", file->stat.st_size);
		break;
	case COL_NLINK:
		xasprintf(&str, "%ju", (unsigned long int)file->stat.st_nlink);
		break;
	case COL_DELETED:
		xasprintf(&str, "%d", file->stat.st_nlink == 0);
		break;
	default:
		return false;
	};

	if (!str)
		err(EXIT_FAILURE, _("failed to add output data"));
	if (scols_line_refer_data(ln, column_index, str))
		err(EXIT_FAILURE, _("failed to add output data"));
	return true;
}

static void file_free_content(struct file *file)
{
	free(file->name);
}

struct file *make_file(const struct file_class *class,
		       struct stat *sb, const char *name, int association)
{
	struct file *file;

	class = class? class: &file_class;
	file = xcalloc(1, class->size);

	file->class = class;
	file->association = association;
	file->name = xstrdup(name);
	file->stat = *sb;
	return file;
}

const struct file_class file_class = {
	.super = NULL,
	.size = sizeof(struct file),
	.fill_column = file_fill_column,
	.free_content = file_free_content,
};
