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

static struct list_head chrdrvs;
static struct list_head miscdevs;

struct chrdrv {
	struct list_head chrdrvs;
	unsigned long major;
	char *name;
};

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
	const char *chrdrv;

	switch(column_id) {
	case COL_TYPE:
		if (scols_line_set_data(ln, column_index, "CHR"))
			err(EXIT_FAILURE, _("failed to add output data"));
		return true;
	case COL_CHRDRV:
		chrdrv = get_chrdrv(major(file->stat.st_rdev));
		if (chrdrv)
			str = strdup(chrdrv);
		else
			xasprintf(&str, "%u",
				  major(file->stat.st_rdev));
		break;
	case COL_DEVICE:
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

struct file *make_cdev(const struct file_class *class,
		       struct stat *sb, const char *name, int fd)
{
	return make_file(class? class: &cdev_class,
			 sb, name, fd);
}

static struct chrdrv *make_chrdrv(unsigned long major, const char *name)
{
	struct chrdrv *chrdrv = xcalloc(1, sizeof(*chrdrv));

	INIT_LIST_HEAD(&chrdrv->chrdrvs);

	chrdrv->major = major;
	chrdrv->name = xstrdup(name);

	return chrdrv;
}

static void free_chrdrv(struct chrdrv *chrdrv)
{
	free(chrdrv->name);
	free(chrdrv);
}

static void read_devices(struct list_head *chrdrvs_list, FILE *devices_fp)
{
	unsigned long major;
	char line[256];
	char name[sizeof(line)];

	while (fgets(line, sizeof(line), devices_fp)) {
		struct chrdrv *chrdrv;

		if (line[0] == 'C')
			continue; /* "Character devices:" */
		else if (line[0] == '\n')
			break;

		if (sscanf(line, "%lu %s", &major, name) != 2)
			continue;
		chrdrv = make_chrdrv(major, name);
		list_add_tail(&chrdrv->chrdrvs, chrdrvs_list);
	}
}

static struct miscdev *make_miscdev(unsigned long minor, const char *name)
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

		miscdev = make_miscdev(minor, name);
		list_add_tail(&miscdev->miscdevs, miscdevs_list);
	}
}

static void cdev_class_initialize(void)
{
	FILE *devices_fp;
	FILE *misc_fp;

	INIT_LIST_HEAD(&chrdrvs);
	INIT_LIST_HEAD(&miscdevs);

	devices_fp = fopen("/proc/devices", "r");
	if (devices_fp) {
		read_devices(&chrdrvs, devices_fp);
		fclose(devices_fp);
	}

	misc_fp = fopen("/proc/misc", "r");
	if (misc_fp) {
		read_misc(&miscdevs, misc_fp);
		fclose(misc_fp);
	}
}

static void cdev_class_finalize(void)
{
	list_free(&chrdrvs, struct chrdrv, chrdrvs, free_chrdrv);
	list_free(&miscdevs, struct miscdev, miscdevs, free_miscdev);
}

const char *get_chrdrv(unsigned long major)
{
	struct list_head *c;
	list_for_each(c, &chrdrvs) {
		struct chrdrv *chrdrv = list_entry(c, struct chrdrv, chrdrvs);
		if (chrdrv->major == major)
			return chrdrv->name;
	}
	return NULL;
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
