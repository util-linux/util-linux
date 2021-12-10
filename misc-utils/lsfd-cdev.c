/*
 * lsfd-cdev.c - handle associations opening character devices
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

static struct list_head miscdevs;

struct miscdev {
	struct list_head miscdevs;
	unsigned long minor;
	char *name;
};

static bool cdev_fill_column(struct proc *proc __attribute__((__unused__)),
			     struct file *file __attribute__((__unused__)),
			     struct libscols_line *ln,
			     int column_id,
			     size_t column_index)
{
	char *str = NULL;
	const char *devdrv;
	const char *miscdev;

	switch(column_id) {
	case COL_TYPE:
		if (scols_line_set_data(ln, column_index, "CHR"))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_MISCDEV:
		devdrv = get_chrdrv(major(file->stat.st_rdev));
		if (devdrv && strcmp(devdrv, "misc") == 0) {
			miscdev = get_miscdev(minor(file->stat.st_rdev));
			if (miscdev)
				str = strdup(miscdev);
			else
				xasprintf(&str, "%u",
					  minor(file->stat.st_rdev));
			break;
		}
		return true;
	case COL_DEVTYPE:
		if (scols_line_set_data(ln, column_index,
					"char"))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_CHRDRV:
		devdrv = get_chrdrv(major(file->stat.st_rdev));
		if (devdrv)
			str = strdup(devdrv);
		else
			xasprintf(&str, "%u",
				  major(file->stat.st_rdev));
		break;
	case COL_SOURCE:
		devdrv = get_chrdrv(major(file->stat.st_rdev));
		miscdev = NULL;
		if (devdrv && strcmp(devdrv, "misc") == 0)
			miscdev = get_miscdev(minor(file->stat.st_rdev));
		if (devdrv) {
			if (miscdev) {
				xasprintf(&str, "misc:%s", miscdev);
			} else {
				xasprintf(&str, "%s:%u", devdrv,
					  minor(file->stat.st_rdev));
			}
			break;
		}
		/* FALL THROUGH */
	case COL_MAJMIN:
		xasprintf(&str, "%u:%u",
			  major(file->stat.st_rdev),
			  minor(file->stat.st_rdev));
		break;
	default:
		return false;
	}

	if (!str)
		err(EXIT_FAILURE, _("failed to add output data"));
	if (scols_line_refer_data(ln, column_index, str))
		err(EXIT_FAILURE, _("failed to add output data"));
	return true;
}

static struct miscdev *new_miscdev(unsigned long minor, const char *name)
{
	struct miscdev *miscdev = xcalloc(1, sizeof(*miscdev));

	INIT_LIST_HEAD(&miscdev->miscdevs);

	miscdev->minor = minor;
	miscdev->name = xstrdup(name);

	return miscdev;
}

static void free_miscdev(struct miscdev *miscdev)
{
	free(miscdev->name);
	free(miscdev);
}

static void read_misc(struct list_head *miscdevs_list, FILE *misc_fp)
{
	unsigned long minor;
	char line[256];
	char name[sizeof(line)];

	while (fgets(line, sizeof(line), misc_fp)) {
		struct miscdev *miscdev;

		if (sscanf(line, "%lu %s", &minor, name) != 2)
			continue;

		miscdev = new_miscdev(minor, name);
		list_add_tail(&miscdev->miscdevs, miscdevs_list);
	}
}

static void cdev_class_initialize(void)
{
	FILE *misc_fp;

	INIT_LIST_HEAD(&miscdevs);

	misc_fp = fopen("/proc/misc", "r");
	if (misc_fp) {
		read_misc(&miscdevs, misc_fp);
		fclose(misc_fp);
	}
}

static void cdev_class_finalize(void)
{
	list_free(&miscdevs, struct miscdev, miscdevs, free_miscdev);
}

const char *get_miscdev(unsigned long minor)
{
	struct list_head *c;
	list_for_each(c, &miscdevs) {
		struct miscdev *miscdev = list_entry(c, struct miscdev, miscdevs);
		if (miscdev->minor == minor)
			return miscdev->name;
	}
	return NULL;
}

const struct file_class cdev_class = {
	.super = &file_class,
	.size = sizeof(struct file),
	.initialize_class = cdev_class_initialize,
	.finalize_class = cdev_class_finalize,
	.fill_column = cdev_fill_column,
	.free_content = NULL,
};
