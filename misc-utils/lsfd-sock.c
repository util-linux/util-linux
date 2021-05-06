/*
 * lsfd-sock.c - handle associations opening socket objects
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

static bool sock_fill_column(struct proc *proc __attribute__((__unused__)),
			     struct file *file __attribute__((__unused__)),
			     struct libscols_line *ln,
			     int column_id,
			     size_t column_index)
{
	switch(column_id) {
	case COL_TYPE:
		if (scols_line_set_data(ln, column_index, "SOCK"))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	default:
		return false;
	}
}

const struct file_class sock_class = {
	.super = &file_class,
	.size = sizeof(struct file),
	.fill_column = sock_fill_column,
	.free_content = NULL,
};

struct file *make_sock(const struct file_class *class,
		       struct stat *sb, const char *name, int fd)
{
	return make_file(class? class: &sock_class,
			 sb, name, fd);
}
