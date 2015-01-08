/*
 * findmnt(8)
 *
 * Copyright (C) 2010-2014 Red Hat, Inc. All rights reserved.
 * Written by Karel Zak <kzak@redhat.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <termios.h>
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#include <assert.h>
#include <poll.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#ifdef HAVE_LIBUDEV
# include <libudev.h>
#endif
#include <libmount.h>
#include <libsmartcols.h>

#include "pathnames.h"
#include "nls.h"
#include "closestream.h"
#include "c.h"
#include "strutils.h"
#include "xalloc.h"
#include "optutils.h"
#include "mangle.h"

/* flags */
enum {
	FL_EVALUATE	= (1 << 1),
	FL_CANONICALIZE = (1 << 2),
	FL_FIRSTONLY	= (1 << 3),
	FL_INVERT	= (1 << 4),
	FL_NOSWAPMATCH	= (1 << 6),
	FL_NOFSROOT	= (1 << 7),
	FL_SUBMOUNTS	= (1 << 8),
	FL_POLL		= (1 << 9),
	FL_DF		= (1 << 10),
	FL_ALL		= (1 << 11),
	FL_UNIQ		= (1 << 12),
	FL_BYTES	= (1 << 13),
	FL_NOCACHE	= (1 << 14),

	/* basic table settings */
	FL_ASCII	= (1 << 20),
	FL_RAW		= (1 << 21),
	FL_NOHEADINGS	= (1 << 22),
	FL_EXPORT	= (1 << 23),
	FL_TREE		= (1 << 24)
};

/* column IDs */
enum {
	COL_SOURCE,
	COL_TARGET,
	COL_FSTYPE,
	COL_OPTIONS,
	COL_VFS_OPTIONS,
	COL_FS_OPTIONS,
	COL_LABEL,
	COL_UUID,
	COL_PARTLABEL,
	COL_PARTUUID,
	COL_MAJMIN,
	COL_ACTION,
	COL_OLD_TARGET,
	COL_OLD_OPTIONS,
	COL_SIZE,
	COL_AVAIL,
	COL_USED,
	COL_USEPERC,
	COL_FSROOT,
	COL_TID,
	COL_ID,
	COL_OPT_FIELDS,
	COL_PROPAGATION,
	COL_FREQ,
	COL_PASSNO
};

enum {
	TABTYPE_FSTAB = 1,
	TABTYPE_MTAB,
	TABTYPE_KERNEL
};

/* column names */
struct colinfo {
	const char	*name;		/* header */
	double		whint;		/* width hint (N < 1 is in percent of termwidth) */
	int		flags;		/* libsmartcols flags */
	const char      *help;		/* column description */
	const char	*match;		/* pattern for match_func() */
	void		*match_data;	/* match specific data */
};

/* columns descriptions (don't use const, this is writable) */
static struct colinfo infos[] = {
	[COL_SOURCE]       = { "SOURCE",       0.25, SCOLS_FL_NOEXTREMES, N_("source device") },
	[COL_TARGET]       = { "TARGET",       0.30, SCOLS_FL_TREE| SCOLS_FL_NOEXTREMES, N_("mountpoint") },
	[COL_FSTYPE]       = { "FSTYPE",       0.10, SCOLS_FL_TRUNC, N_("filesystem type") },
	[COL_OPTIONS]      = { "OPTIONS",      0.10, SCOLS_FL_TRUNC, N_("all mount options") },
	[COL_VFS_OPTIONS]  = { "VFS-OPTIONS",  0.20, SCOLS_FL_TRUNC, N_("VFS specific mount options") },
	[COL_FS_OPTIONS]   = { "FS-OPTIONS",   0.10, SCOLS_FL_TRUNC, N_("FS specific mount options") },
	[COL_LABEL]        = { "LABEL",        0.10, 0, N_("filesystem label") },
	[COL_UUID]         = { "UUID",           36, 0, N_("filesystem UUID") },
	[COL_PARTLABEL]    = { "PARTLABEL",    0.10, 0, N_("partition label") },
	[COL_PARTUUID]     = { "PARTUUID",       36, 0, N_("partition UUID") },
	[COL_MAJMIN]       = { "MAJ:MIN",         6, 0, N_("major:minor device number") },
	[COL_ACTION]       = { "ACTION",         10, SCOLS_FL_STRICTWIDTH, N_("action detected by --poll") },
	[COL_OLD_OPTIONS]  = { "OLD-OPTIONS",  0.10, SCOLS_FL_TRUNC, N_("old mount options saved by --poll") },
	[COL_OLD_TARGET]   = { "OLD-TARGET",   0.30, 0, N_("old mountpoint saved by --poll") },
	[COL_SIZE]         = { "SIZE",            5, SCOLS_FL_RIGHT, N_("filesystem size") },
	[COL_AVAIL]        = { "AVAIL",           5, SCOLS_FL_RIGHT, N_("filesystem size available") },
	[COL_USED]         = { "USED",            5, SCOLS_FL_RIGHT, N_("filesystem size used") },
	[COL_USEPERC]      = { "USE%",            3, SCOLS_FL_RIGHT, N_("filesystem use percentage") },
	[COL_FSROOT]       = { "FSROOT",       0.25, SCOLS_FL_NOEXTREMES, N_("filesystem root") },
	[COL_TID]          = { "TID",             4, SCOLS_FL_RIGHT, N_("task ID") },
	[COL_ID]           = { "ID",              2, SCOLS_FL_RIGHT, N_("mount ID") },
	[COL_OPT_FIELDS]   = { "OPT-FIELDS",   0.10, SCOLS_FL_TRUNC, N_("optional mount fields") },
	[COL_PROPAGATION]  = { "PROPAGATION",  0.10, 0, N_("VFS propagation flags") },
	[COL_FREQ]         = { "FREQ",            1, SCOLS_FL_RIGHT, N_("dump(8) period in days [fstab only]") },
	[COL_PASSNO]       = { "PASSNO",          1, SCOLS_FL_RIGHT, N_("pass number on parallel fsck(8) [fstab only]") }
};

/* columns[] array specifies all currently wanted output column. The columns
 * are defined by infos[] array and you can specify (on command line) each
 * column twice. That's enough, dynamically allocated array of the columns is
 * unnecessary overkill and over-engineering in this case */
static int columns[ARRAY_SIZE(infos) * 2];
static int ncolumns;

static inline size_t err_columns_index(size_t arysz, size_t idx)
{
	if (idx >= arysz)
		errx(EXIT_FAILURE, _("too many columns specified, "
				     "the limit is %zu columns"),
				arysz - 1);
	return idx;
}

#define add_column(ary, n, id)	\
		((ary)[ err_columns_index(ARRAY_SIZE(ary), (n)) ] = (id))

/* global flags */
static int flags;


/* poll actions (parsed --poll=<list> */
#define FINDMNT_NACTIONS	4		/* mount, umount, move, remount */
static int actions[FINDMNT_NACTIONS];
static int nactions;

/* libmount cache */
static struct libmnt_cache *cache;

#ifdef HAVE_LIBUDEV
struct udev *udev;
#endif

static int match_func(struct libmnt_fs *fs, void *data __attribute__ ((__unused__)));


static int get_column_id(int num)
{
	assert(num < ncolumns);
	assert((size_t) columns[num] < ARRAY_SIZE(infos));
	return columns[num];
}

static struct colinfo *get_column_info(int num)
{
	return &infos[ get_column_id(num) ];
}

static const char *column_id_to_name(int id)
{
	assert((size_t) id < ARRAY_SIZE(infos));
	return infos[id].name;
}

static const char *get_column_name(int num)
{
	return get_column_info(num)->name;
}

static float get_column_whint(int num)
{
	return get_column_info(num)->whint;
}

static int get_column_flags(int num)
{
	return get_column_info(num)->flags;
}

static const char *get_match(int id)
{
	assert((size_t) id < ARRAY_SIZE(infos));
	return infos[id].match;
}

static void *get_match_data(int id)
{
	assert((size_t) id < ARRAY_SIZE(infos));
	return infos[id].match_data;
}

static void set_match(int id, const char *match)
{
	assert((size_t) id < ARRAY_SIZE(infos));
	infos[id].match = match;
}

static void set_match_data(int id, void *data)
{
	assert((size_t) id < ARRAY_SIZE(infos));
	infos[id].match_data = data;
}

/*
 * source match means COL_SOURCE *or* COL_MAJMIN, depends on
 * data format.
 */
static void set_source_match(const char *data)
{
	int maj, min;

	if (sscanf(data, "%d:%d", &maj, &min) == 2) {
		dev_t *devno = xmalloc(sizeof(dev_t));

		*devno = makedev(maj, min);
		set_match(COL_MAJMIN, data);
		set_match_data(COL_MAJMIN, (void *) devno);
		flags |= FL_NOSWAPMATCH;
	} else
		set_match(COL_SOURCE, data);
}

static void enable_extra_target_match(void)
{
	const char *cn = NULL, *mnt = NULL;

	/*
	 * Check if match pattern is mountpoint, if not use the
	 * real mountpoint.
	 */
	if (flags & FL_NOCACHE)
		cn = get_match(COL_TARGET);
	else {
		cn = mnt_resolve_path(get_match(COL_TARGET), cache);
		if (!cn)
			return;
	}

	mnt = mnt_get_mountpoint(cn);
	if (!mnt || strcmp(mnt, cn) == 0)
		return;

	/* replace the current setting with the real mountpoint */
	set_match(COL_TARGET, mnt);
}


static int is_tabdiff_column(int id)
{
	assert((size_t) id < ARRAY_SIZE(infos));

	switch(id) {
	case COL_ACTION:
	case COL_OLD_TARGET:
	case COL_OLD_OPTIONS:
		return 1;
	default:
		break;
	}
	return 0;
}

/*
 * "findmnt" without any filter
 */
static int is_listall_mode(void)
{
	if ((flags & FL_DF) && !(flags & FL_ALL))
		return 0;

	return (!get_match(COL_SOURCE) &&
		!get_match(COL_TARGET) &&
		!get_match(COL_FSTYPE) &&
		!get_match(COL_OPTIONS) &&
		!get_match(COL_MAJMIN));
}

/*
 * Returns 1 if the @act is in the --poll=<list>
 */
static int has_poll_action(int act)
{
	int i;

	if (!nactions)
		return 1;	/* all actions enabled */
	for (i = 0; i < nactions; i++)
		if (actions[i] == act)
			return 1;
	return 0;
}

static int poll_action_name_to_id(const char *name, size_t namesz)
{
	int id = -1;

	if (strncasecmp(name, "move", namesz) == 0 && namesz == 4)
		id = MNT_TABDIFF_MOVE;
	else if (strncasecmp(name, "mount", namesz) == 0 && namesz == 5)
		id = MNT_TABDIFF_MOUNT;
	else if (strncasecmp(name, "umount", namesz) == 0 && namesz == 6)
		id = MNT_TABDIFF_UMOUNT;
	else if (strncasecmp(name, "remount", namesz) == 0 && namesz == 7)
		id = MNT_TABDIFF_REMOUNT;
	else
		warnx(_("unknown action: %s"), name);

	return id;
}

/*
 * findmnt --first-only <devname|TAG=|mountpoint>
 *
 * ... it works like "mount <devname|TAG=|mountpoint>"
 */
static int is_mount_compatible_mode(void)
{
	if (!get_match(COL_SOURCE))
	       return 0;		/* <devname|TAG=|mountpoint> is required */
	if (get_match(COL_FSTYPE) || get_match(COL_OPTIONS))
		return 0;		/* cannot be restricted by -t or -O */
	if (!(flags & FL_FIRSTONLY))
		return 0;		/* we have to return the first entry only */

	return 1;			/* ok */
}

static void disable_columns_truncate(void)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(infos); i++)
		infos[i].flags &= ~SCOLS_FL_TRUNC;
}

/*
 * converts @name to column ID
 */
static int column_name_to_id(const char *name, size_t namesz)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(infos); i++) {
		const char *cn = column_id_to_name(i);

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}


#ifdef HAVE_LIBUDEV
static char *get_tag_from_udev(const char *devname, int col)
{
	struct udev_device *dev;
	const char *data = NULL;
	char *res = NULL, *path;

	if (!udev)
		udev = udev_new();
	if (!udev)
		return NULL;

	/* libudev don't like /dev/mapper/ symlinks */
	path = realpath(devname, NULL);
	if (path)
		devname = path;

	if (strncmp(devname, "/dev/", 5) == 0)
		devname += 5;

	dev = udev_device_new_from_subsystem_sysname(udev, "block", devname);
	free(path);

	if (!dev)
		return NULL;

	switch(col) {
	case COL_LABEL:
		data = udev_device_get_property_value(dev, "ID_FS_LABEL_ENC");
		break;
	case COL_UUID:
		data = udev_device_get_property_value(dev, "ID_FS_UUID_ENC");
		break;
	case COL_PARTUUID:
		data = udev_device_get_property_value(dev, "ID_PART_ENTRY_UUID");
		break;
	case COL_PARTLABEL:
		data = udev_device_get_property_value(dev, "ID_PART_ENTRY_NAME");
		break;
	default:
		break;
	}

	if (data) {
		res = xstrdup(data);
		unhexmangle_string(res);
	}

	udev_device_unref(dev);
	return res;
}
#endif /* HAVE_LIBUDEV */

/* Returns LABEL or UUID */
static char *get_tag(struct libmnt_fs *fs, const char *tagname, int col
#ifndef HAVE_LIBUDEV
		__attribute__((__unused__))
#endif
		)
{
	const char *t, *v;
	char *res = NULL;

	if (!mnt_fs_get_tag(fs, &t, &v) && !strcmp(t, tagname))
		res = xstrdup(v);
	else {
		const char *dev = mnt_fs_get_source(fs);

		if (dev && !(flags & FL_NOCACHE))
			dev = mnt_resolve_spec(dev, cache);
#ifdef HAVE_LIBUDEV
		if (dev)
			res = get_tag_from_udev(dev, col);
#endif
		if (!res) {
			res = mnt_cache_find_tag_value(cache, dev, tagname);
			if (res && cache)
				/* don't return pointer to cache */
				res = xstrdup(res);
		}
	}

	return res;
}

static char *get_vfs_attr(struct libmnt_fs *fs, int sizetype)
{
	struct statvfs buf;
	uint64_t vfs_attr = 0;
	char *sizestr;

	if (statvfs(mnt_fs_get_target(fs), &buf) != 0)
		return NULL;

	switch(sizetype) {
	case COL_SIZE:
		vfs_attr = buf.f_frsize * buf.f_blocks;
		break;
	case COL_AVAIL:
		vfs_attr = buf.f_frsize * buf.f_bavail;
		break;
	case COL_USED:
		vfs_attr = buf.f_frsize * (buf.f_blocks - buf.f_bfree);
		break;
	case COL_USEPERC:
		if (buf.f_blocks == 0)
			return xstrdup("-");

		xasprintf(&sizestr, "%.0f%%",
				(double)(buf.f_blocks - buf.f_bfree) /
				buf.f_blocks * 100);
		return sizestr;
	}

	if (!vfs_attr)
		sizestr = xstrdup("0");
	else if (flags & FL_BYTES)
		xasprintf(&sizestr, "%ju", vfs_attr);
	else
		sizestr = size_to_human_string(SIZE_SUFFIX_1LETTER, vfs_attr);

	return sizestr;
}

/* reads FS data from libmount
 */
static char *get_data(struct libmnt_fs *fs, int num)
{
	char *str = NULL;
	int col_id = get_column_id(num);

	switch (col_id) {
	case COL_SOURCE:
	{
		const char *root = mnt_fs_get_root(fs);
		const char *spec = mnt_fs_get_srcpath(fs);

		if (spec && (flags & FL_CANONICALIZE))
			spec = mnt_resolve_path(spec, cache);
		if (!spec) {
			spec = mnt_fs_get_source(fs);

			if (spec && (flags & FL_EVALUATE))
				spec = mnt_resolve_spec(spec, cache);
		}
		if (root && spec && !(flags & FL_NOFSROOT) && strcmp(root, "/"))
			xasprintf(&str, "%s[%s]", spec, root);
		else if (spec)
			str = xstrdup(spec);
		break;
	}
	case COL_TARGET:
		str = xstrdup(mnt_fs_get_target(fs));
		break;
	case COL_FSTYPE:
		str = xstrdup(mnt_fs_get_fstype(fs));
		break;
	case COL_OPTIONS:
		str = xstrdup(mnt_fs_get_options(fs));
		break;
	case COL_VFS_OPTIONS:
		str = xstrdup(mnt_fs_get_vfs_options(fs));
		break;
	case COL_FS_OPTIONS:
		str = xstrdup(mnt_fs_get_fs_options(fs));
		break;
	case COL_OPT_FIELDS:
		str = xstrdup(mnt_fs_get_optional_fields(fs));
		break;
	case COL_UUID:
		str = get_tag(fs, "UUID", col_id);
		break;
	case COL_PARTUUID:
		str = get_tag(fs, "PARTUUID", col_id);
		break;
	case COL_LABEL:
		str = get_tag(fs, "LABEL", col_id);
		break;
	case COL_PARTLABEL:
		str = get_tag(fs, "PARTLABEL", col_id);
		break;

	case COL_MAJMIN:
	{
		dev_t devno = mnt_fs_get_devno(fs);
		if (!devno)
			break;

		if ((flags & FL_RAW) || (flags & FL_EXPORT))
			xasprintf(&str, "%u:%u", major(devno), minor(devno));
		else
			xasprintf(&str, "%3u:%-3u", major(devno), minor(devno));
		break;
	}
	case COL_SIZE:
	case COL_AVAIL:
	case COL_USED:
	case COL_USEPERC:
		str = get_vfs_attr(fs, col_id);
		break;
	case COL_FSROOT:
		str = xstrdup(mnt_fs_get_root(fs));
		break;
	case COL_TID:
		if (mnt_fs_get_tid(fs))
			xasprintf(&str, "%d", mnt_fs_get_tid(fs));
		break;
	case COL_ID:
		if (mnt_fs_get_id(fs))
			xasprintf(&str, "%d", mnt_fs_get_id(fs));
		break;
	case COL_PROPAGATION:
		if (mnt_fs_is_kernel(fs)) {
			unsigned long fl = 0;
			char *n = NULL;

			if (mnt_fs_get_propagation(fs, &fl) != 0)
				break;

			n = xstrdup((fl & MS_SHARED) ? "shared" : "private");

			if (fl & MS_SLAVE) {
				xasprintf(&str, "%s,slave", n);
				free(n);
				n = str;
			}
			if (fl & MS_UNBINDABLE) {
				xasprintf(&str, "%s,unbindable", n);
				free(n);
				n = str;
			}
			str = n;
		}
		break;
	case COL_FREQ:
		if (!mnt_fs_is_kernel(fs))
			xasprintf(&str, "%d", mnt_fs_get_freq(fs));
		break;
	case COL_PASSNO:
		if (!mnt_fs_is_kernel(fs))
			xasprintf(&str, "%d", mnt_fs_get_passno(fs));
		break;
	default:
		break;
	}
	return str;
}

static char *get_tabdiff_data(struct libmnt_fs *old_fs,
				    struct libmnt_fs *new_fs,
				    int change,
				    int num)
{
	char *str = NULL;

	switch (get_column_id(num)) {
	case COL_ACTION:
		switch (change) {
		case MNT_TABDIFF_MOUNT:
			str = _("mount");
			break;
		case MNT_TABDIFF_UMOUNT:
			str = _("umount");
			break;
		case MNT_TABDIFF_REMOUNT:
			str = _("remount");
			break;
		case MNT_TABDIFF_MOVE:
			str = _("move");
			break;
		default:
			str = _("unknown");
			break;
		}
		str = xstrdup(str);
		break;
	case COL_OLD_OPTIONS:
		if (old_fs && (change == MNT_TABDIFF_REMOUNT ||
			       change == MNT_TABDIFF_UMOUNT))
			str = xstrdup(mnt_fs_get_options(old_fs));
		break;
	case COL_OLD_TARGET:
		if (old_fs && (change == MNT_TABDIFF_MOVE ||
			       change == MNT_TABDIFF_UMOUNT))
			str = xstrdup(mnt_fs_get_target(old_fs));
		break;
	default:
		if (new_fs)
			str = get_data(new_fs, num);
		else
			str = get_data(old_fs, num);
		break;
	}
	return str;
}

/* adds one line to the output @tab */
static struct libscols_line *add_line(struct libscols_table *table, struct libmnt_fs *fs,
					struct libscols_line *parent)
{
	int i;
	struct libscols_line *line = scols_table_new_line(table, parent);

	if (!line) {
		warn(_("failed to add line to output"));
		return NULL;
	}
	for (i = 0; i < ncolumns; i++)
		scols_line_refer_data(line, i, get_data(fs, i));

	scols_line_set_userdata(line, fs);
	return line;
}

static struct libscols_line *add_tabdiff_line(struct libscols_table *table, struct libmnt_fs *new_fs,
			struct libmnt_fs *old_fs, int change)
{
	int i;
	struct libscols_line *line = scols_table_new_line(table, NULL);

	if (!line) {
		warn(_("failed to add line to output"));
		return NULL;
	}
	for (i = 0; i < ncolumns; i++)
		scols_line_refer_data(line, i,
				get_tabdiff_data(old_fs, new_fs, change, i));

	return line;
}

static int has_line(struct libscols_table *table, struct libmnt_fs *fs)
{
	struct libscols_line *ln;
	struct libscols_iter *itr;
	int rc = 0;

	itr = scols_new_iter(SCOLS_ITER_FORWARD);
	if (!itr)
		return 0;

	while (scols_table_next_line(table, itr, &ln) == 0) {
		if ((struct libmnt_fs *) scols_line_get_userdata(ln) == fs) {
			rc = 1;
			break;
		}
	}

	scols_free_iter(itr);
	return rc;
}

/* reads filesystems from @tb (libmount) and fillin @table (output table) */
static int create_treenode(struct libscols_table *table, struct libmnt_table *tb,
			   struct libmnt_fs *fs, struct libscols_line *parent_line)
{
	struct libmnt_fs *chld = NULL;
	struct libmnt_iter *itr = NULL;
	struct libscols_line *line;
	int rc = -1;

	if (!fs) {
		/* first call, get root FS */
		if (mnt_table_get_root_fs(tb, &fs))
			goto leave;
		parent_line = NULL;

	} else if ((flags & FL_SUBMOUNTS) && has_line(table, fs))
		return 0;

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr)
		goto leave;

	if ((flags & FL_SUBMOUNTS) || match_func(fs, NULL)) {
		line = add_line(table, fs, parent_line);
		if (!line)
			goto leave;
	} else
		line = parent_line;

	/*
	 * add all children to the output table
	 */
	while(mnt_table_next_child_fs(tb, itr, fs, &chld) == 0) {
		if (create_treenode(table, tb, chld, line))
			goto leave;
	}
	rc = 0;
leave:
	mnt_free_iter(itr);
	return rc;
}

/* error callback */
static int parser_errcb(struct libmnt_table *tb __attribute__ ((__unused__)),
			const char *filename, int line)
{
	warnx(_("%s: parse error at line %d"), filename, line);
	return 0;
}

static char **append_tabfile(char **files, int *nfiles, char *filename)
{
	files = xrealloc(files, sizeof(char *) * (*nfiles + 1));
	files[(*nfiles)++] = filename;
	return files;
}

static char **append_pid_tabfile(char **files, int *nfiles, pid_t pid)
{
	char *path = NULL;

	xasprintf(&path, "/proc/%d/mountinfo", (int) pid);
	return append_tabfile(files, nfiles, path);
}

/* calls libmount fstab/mtab/mountinfo parser */
static struct libmnt_table *parse_tabfiles(char **files,
					   int nfiles,
					   int tabtype)
{
	struct libmnt_table *tb;
	int rc = 0;

	tb = mnt_new_table();
	if (!tb) {
		warn(_("failed to initialize libmount table"));
		return NULL;
	}
	mnt_table_set_parser_errcb(tb, parser_errcb);

	do {
		/* NULL means that libmount will use default paths */
		const char *path = nfiles ? *files++ : NULL;

		switch (tabtype) {
		case TABTYPE_FSTAB:
			rc = mnt_table_parse_fstab(tb, path);
			break;
		case TABTYPE_MTAB:
			rc = mnt_table_parse_mtab(tb, path);
			break;
		case TABTYPE_KERNEL:
			if (!path)
				path = access(_PATH_PROC_MOUNTINFO, R_OK) == 0 ?
					      _PATH_PROC_MOUNTINFO :
					      _PATH_PROC_MOUNTS;

			rc = mnt_table_parse_file(tb, path);
			break;
		}
		if (rc) {
			mnt_unref_table(tb);
			warn(_("can't read %s"), path);
			return NULL;
		}
	} while (--nfiles > 0);

	return tb;
}

/*
 * Parses mountinfo and calls mnt_cache_set_targets(cache, mtab). Only
 * necessary if @tb in main() was read from a non-kernel source.
 */
static void cache_set_targets(struct libmnt_cache *tmp)
{
	struct libmnt_table *tb;
	const char *path;

	tb = mnt_new_table();
	if (!tb)
		return;

	path = access(_PATH_PROC_MOUNTINFO, R_OK) == 0 ?
		_PATH_PROC_MOUNTINFO :
		_PATH_PROC_MOUNTS;

	if (mnt_table_parse_file(tb, path) == 0)
		mnt_cache_set_targets(tmp, tb);

	mnt_unref_table(tb);
}

/* checks if @tb contains parent->child relations */
static int tab_is_tree(struct libmnt_table *tb)
{
	struct libmnt_fs *fs = NULL;
	struct libmnt_iter *itr = NULL;
	int rc = 0;

	itr = mnt_new_iter(MNT_ITER_BACKWARD);
	if (!itr)
		return 0;

	rc = (mnt_table_next_fs(tb, itr, &fs) == 0 &&
	      mnt_fs_is_kernel(fs) &&
	      mnt_fs_get_root(fs));

	mnt_free_iter(itr);
	return rc;
}


/* filter function for libmount (mnt_table_find_next_fs()) */
static int match_func(struct libmnt_fs *fs,
		      void *data __attribute__ ((__unused__)))
{
	int rc = flags & FL_INVERT ? 1 : 0;
	const char *m;
	void *md;

	m = get_match(COL_FSTYPE);
	if (m && !mnt_fs_match_fstype(fs, m))
		return rc;

	m = get_match(COL_OPTIONS);
	if (m && !mnt_fs_match_options(fs, m))
		return rc;

	md = get_match_data(COL_MAJMIN);
	if (md && mnt_fs_get_devno(fs) != *((dev_t *) md))
		return rc;

	m = get_match(COL_TARGET);
	if (m && !mnt_fs_match_target(fs, m, cache))
		return rc;

	m = get_match(COL_SOURCE);
	if (m && !mnt_fs_match_source(fs, m, cache))
		return rc;

	if ((flags & FL_DF) && !(flags & FL_ALL)) {
		const char *type = mnt_fs_get_fstype(fs);

		if (type && strstr(type, "tmpfs"))	/* tmpfs is wanted */
			return !rc;

		if (mnt_fs_is_pseudofs(fs))
			return rc;
	}

	return !rc;
}

/* iterate over filesystems in @tb */
static struct libmnt_fs *get_next_fs(struct libmnt_table *tb,
				     struct libmnt_iter *itr)
{
	struct libmnt_fs *fs = NULL;

	if (is_listall_mode()) {
		/*
		 * Print whole file
		 */
		if (mnt_table_next_fs(tb, itr, &fs) != 0)
			return NULL;

	} else if (is_mount_compatible_mode()) {
		/*
		 * Look up for FS in the same way how mount(8) searchs in fstab
		 *
		 *   findmnt -f <spec>
		 */
		fs = mnt_table_find_source(tb, get_match(COL_SOURCE),
					mnt_iter_get_direction(itr));

		if (!fs && !(flags & FL_NOSWAPMATCH))
			fs = mnt_table_find_target(tb, get_match(COL_SOURCE),
					mnt_iter_get_direction(itr));
	} else {
		/*
		 * Look up for all matching entries
		 *
		 *    findmnt [-l] <source> <target> [-O <options>] [-t <types>]
		 *    findmnt [-l] <spec> [-O <options>] [-t <types>]
		 */
again:
		mnt_table_find_next_fs(tb, itr, match_func,  NULL, &fs);

		if (!fs &&
		    !(flags & FL_NOSWAPMATCH) &&
		    !get_match(COL_TARGET) && get_match(COL_SOURCE)) {

			/* swap 'spec' and target. */
			set_match(COL_TARGET, get_match(COL_SOURCE));
			set_match(COL_SOURCE, NULL);
			mnt_reset_iter(itr, -1);

			goto again;
		}
	}

	return fs;
}

/*
 * Filter out unwanted lines for --list output or top level lines for
 * --submounts tree output.
 */
static int add_matching_lines(struct libmnt_table *tb,
			      struct libscols_table *table, int direction)
{
	struct libmnt_iter *itr = NULL;
	struct libmnt_fs *fs;
	int nlines = 0, rc = -1;

	itr = mnt_new_iter(direction);
	if (!itr) {
		warn(_("failed to initialize libmount iterator"));
		goto done;
	}

	while((fs = get_next_fs(tb, itr))) {
		if ((flags & FL_TREE) || (flags & FL_SUBMOUNTS))
			rc = create_treenode(table, tb, fs, NULL);
		else
			rc = !add_line(table, fs, NULL);
		if (rc)
			goto done;
		nlines++;
		if (flags & FL_FIRSTONLY)
			break;
		flags |= FL_NOSWAPMATCH;
	}

	if (nlines)
		rc = 0;
done:
	mnt_free_iter(itr);
	return rc;
}

static int poll_match(struct libmnt_fs *fs)
{
	int rc = match_func(fs, NULL);

	if (rc == 0 && !(flags & FL_NOSWAPMATCH) &&
	    get_match(COL_SOURCE) && !get_match(COL_TARGET)) {
		/*
		 * findmnt --poll /foo
		 * The '/foo' maybe source as well as target.
		 */
		const char *str = get_match(COL_SOURCE);

		set_match(COL_TARGET, str);	/* swap */
		set_match(COL_SOURCE, NULL);

		rc = match_func(fs, NULL);

		set_match(COL_TARGET, NULL);	/* restore */
		set_match(COL_SOURCE, str);

	}
	return rc;
}

static int poll_table(struct libmnt_table *tb, const char *tabfile,
		  int timeout, struct libscols_table *table, int direction)
{
	FILE *f = NULL;
	int rc = -1;
	struct libmnt_iter *itr = NULL;
	struct libmnt_table *tb_new = NULL;
	struct libmnt_tabdiff *diff = NULL;
	struct pollfd fds[1];

	tb_new = mnt_new_table();
	if (!tb_new) {
		warn(_("failed to initialize libmount table"));
		goto done;
	}

	itr = mnt_new_iter(direction);
	if (!itr) {
		warn(_("failed to initialize libmount iterator"));
		goto done;
	}

	diff = mnt_new_tabdiff();
	if (!diff) {
		warn(_("failed to initialize libmount tabdiff"));
		goto done;
	}

	/* cache is unnecessary to detect changes */
	mnt_table_set_cache(tb, NULL);
	mnt_table_set_cache(tb_new, NULL);

	f = fopen(tabfile, "r");
	if (!f) {
		warn(_("cannot open %s"), tabfile);
		goto done;
	}

	mnt_table_set_parser_errcb(tb_new, parser_errcb);

	fds[0].fd = fileno(f);
	fds[0].events = POLLPRI;

	while (1) {
		struct libmnt_table *tmp;
		struct libmnt_fs *old, *new;
		int change, count;

		count = poll(fds, 1, timeout);
		if (count == 0)
			break;	/* timeout */
		if (count < 0) {
			warn(_("poll() failed"));
			goto done;
		}

		rewind(f);
		rc = mnt_table_parse_stream(tb_new, f, tabfile);
		if (!rc)
			rc = mnt_diff_tables(diff, tb, tb_new);
		if (rc < 0)
			goto done;

		count = 0;
		mnt_reset_iter(itr, direction);
		while(mnt_tabdiff_next_change(
				diff, itr, &old, &new, &change) == 0) {

			if (!has_poll_action(change))
				continue;
			if (!poll_match(new ? new : old))
				continue;
			count++;
			rc = !add_tabdiff_line(table, new, old, change);
			if (rc)
				goto done;
			if (flags & FL_FIRSTONLY)
				break;
		}

		if (count) {
			rc = scols_print_table(table);
			if (rc)
				goto done;
		}

		/* swap tables */
		tmp = tb;
		tb = tb_new;
		tb_new = tmp;

		scols_table_remove_lines(table);
		mnt_reset_table(tb_new);

		if (count && (flags & FL_FIRSTONLY))
			break;
	}

	rc = 0;
done:
	mnt_unref_table(tb_new);
	mnt_free_tabdiff(diff);
	mnt_free_iter(itr);
	if (f)
		fclose(f);
	return rc;
}

static int uniq_fs_target_cmp(
		struct libmnt_table *tb __attribute__((__unused__)),
		struct libmnt_fs *a,
		struct libmnt_fs *b)
{
	return !mnt_fs_match_target(a, mnt_fs_get_target(b), cache);
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	size_t i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(
	" %1$s [options]\n"
	" %1$s [options] <device> | <mountpoint>\n"
	" %1$s [options] <device> <mountpoint>\n"
	" %1$s [options] [--source <device>] [--target <mountpoint>]\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Find a (mounted) filesystem.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -s, --fstab            search in static table of filesystems\n"), out);
	fputs(_(" -m, --mtab             search in table of mounted filesystems\n"), out);
	fputs(_(" -k, --kernel           search in kernel table of mounted\n"
		"                          filesystems (default)\n"), out);
	fputc('\n', out);
	fputs(_(" -p, --poll[=<list>]    monitor changes in table of mounted filesystems\n"), out);
	fputs(_(" -w, --timeout <num>    upper limit in milliseconds that --poll will block\n"), out);
	fputc('\n', out);

	fputs(_(" -A, --all              disable all built-in filters, print all filesystems\n"), out);
	fputs(_(" -a, --ascii            use ASCII chars for tree formatting\n"), out);
	fputs(_(" -b, --bytes            print sizes in bytes rather than in human readable format\n"), out);
	fputs(_(" -C, --nocanonicalize   don't canonicalize when comparing paths\n"), out);
	fputs(_(" -c, --canonicalize     canonicalize printed paths\n"), out);
	fputs(_(" -D, --df               imitate the output of df(1)\n"), out);
	fputs(_(" -d, --direction <word> direction of search, 'forward' or 'backward'\n"), out);
	fputs(_(" -e, --evaluate         convert tags (LABEL,UUID,PARTUUID,PARTLABEL) \n"
	        "                          to device names\n"), out);
	fputs(_(" -F, --tab-file <path>  alternative file for -s, -m or -k options\n"), out);
	fputs(_(" -f, --first-only       print the first found filesystem only\n"), out);
	fputs(_(" -i, --invert           invert the sense of matching\n"), out);
	fputs(_(" -l, --list             use list format output\n"), out);
	fputs(_(" -N, --task <tid>       use alternative namespace (/proc/<tid>/mountinfo file)\n"), out);
	fputs(_(" -n, --noheadings       don't print column headings\n"), out);
	fputs(_(" -O, --options <list>   limit the set of filesystems by mount options\n"), out);
	fputs(_(" -o, --output <list>    the output columns to be shown\n"), out);
	fputs(_(" -P, --pairs            use key=\"value\" output format\n"), out);
	fputs(_(" -R, --submounts        print all submounts for the matching filesystems\n"), out);
	fputs(_(" -r, --raw              use raw output format\n"), out);
	fputs(_(" -S, --source <string>  the device to mount (by name, maj:min, \n"
	        "                          LABEL=, UUID=, PARTUUID=, PARTLABEL=)\n"), out);
	fputs(_(" -T, --target <string>  the mountpoint to use\n"), out);
	fputs(_(" -t, --types <list>     limit the set of filesystems by FS types\n"), out);
	fputs(_(" -U, --uniq             ignore filesystems with duplicate target\n"), out);
	fputs(_(" -u, --notruncate       don't truncate text in columns\n"), out);
	fputs(_(" -v, --nofsroot         don't print [/dir] for bind or btrfs mounts\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, _("\nAvailable columns:\n"));

	for (i = 0; i < ARRAY_SIZE(infos); i++)
		fprintf(out, " %11s  %s\n", infos[i].name, _(infos[i].help));

	fprintf(out, USAGE_MAN_TAIL("findmnt(8)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	struct libmnt_table *tb = NULL;
	char **tabfiles = NULL;
	int direction = MNT_ITER_FORWARD;
	int i, c, rc = -1, timeout = -1;
	int ntabfiles = 0, tabtype = 0;
	char *outarg = NULL;

	struct libscols_table *table = NULL;

	static const struct option longopts[] = {
	    { "all",          0, 0, 'A' },
	    { "ascii",        0, 0, 'a' },
	    { "bytes",        0, 0, 'b' },
	    { "canonicalize", 0, 0, 'c' },
	    { "direction",    1, 0, 'd' },
	    { "df",           0, 0, 'D' },
	    { "evaluate",     0, 0, 'e' },
	    { "first-only",   0, 0, 'f' },
	    { "fstab",        0, 0, 's' },
	    { "help",         0, 0, 'h' },
	    { "invert",       0, 0, 'i' },
	    { "kernel",       0, 0, 'k' },
	    { "list",         0, 0, 'l' },
	    { "mtab",         0, 0, 'm' },
	    { "noheadings",   0, 0, 'n' },
	    { "notruncate",   0, 0, 'u' },
	    { "options",      1, 0, 'O' },
	    { "output",       1, 0, 'o' },
	    { "poll",         2, 0, 'p' },
	    { "pairs",        0, 0, 'P' },
	    { "raw",          0, 0, 'r' },
	    { "types",        1, 0, 't' },
	    { "nocanonicalize", 0, 0, 'C' },
	    { "nofsroot",     0, 0, 'v' },
	    { "submounts",    0, 0, 'R' },
	    { "source",       1, 0, 'S' },
	    { "tab-file",     1, 0, 'F' },
	    { "task",         1, 0, 'N' },
	    { "target",       1, 0, 'T' },
	    { "timeout",      1, 0, 'w' },
	    { "uniq",         0, 0, 'U' },
	    { "version",      0, 0, 'V' },

	    { NULL,           0, 0, 0 }
	};

	static const ul_excl_t excl[] = {	/* rows and cols in in ASCII order */
		{ 'C', 'c'},                    /* [no]canonicalize */
		{ 'C', 'e' },			/* nocanonicalize, evaluate */
		{ 'N','k','m','s' },		/* task,kernel,mtab,fstab */
		{ 'P','l','r' },		/* pairs,list,raw */
		{ 'm','p','s' },		/* mtab,poll,fstab */
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	/* default output format */
	flags |= FL_TREE;

	while ((c = getopt_long(argc, argv,
				"AabCcDd:ehifF:o:O:p::PklmnN:rst:uvRS:T:Uw:V",
				longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch(c) {
		case 'A':
			flags |= FL_ALL;
			break;
		case 'a':
			flags |= FL_ASCII;
			break;
		case 'b':
			flags |= FL_BYTES;
			break;
		case 'C':
			flags |= FL_NOCACHE;
			break;
		case 'c':
			flags |= FL_CANONICALIZE;
			break;
		case 'D':
			flags &= ~FL_TREE;
			flags |= FL_DF;
			break;
		case 'd':
			if (!strcmp(optarg, "forward"))
				direction = MNT_ITER_FORWARD;
			else if (!strcmp(optarg, "backward"))
				direction = MNT_ITER_BACKWARD;
			else
				errx(EXIT_FAILURE,
					_("unknown direction '%s'"), optarg);
			break;
		case 'e':
			flags |= FL_EVALUATE;
			break;
		case 'h':
			usage(stdout);
			break;
		case 'i':
			flags |= FL_INVERT;
			break;
		case 'f':
			flags |= FL_FIRSTONLY;
			break;
		case 'F':
			tabfiles = append_tabfile(tabfiles, &ntabfiles, optarg);
			break;
		case 'u':
			disable_columns_truncate();
			break;
		case 'o':
			outarg = optarg;
			break;
		case 'O':
			set_match(COL_OPTIONS, optarg);
			break;
		case 'p':
			if (optarg) {
				nactions = string_to_idarray(optarg,
						actions, ARRAY_SIZE(actions),
						poll_action_name_to_id);
				if (nactions < 0)
					exit(EXIT_FAILURE);
			}
			flags |= FL_POLL;
			flags &= ~FL_TREE;
			break;
		case 'P':
			flags |= FL_EXPORT;
			flags &= ~FL_TREE;
			break;
		case 'm':		/* mtab */
			tabtype = TABTYPE_MTAB;
			flags &= ~FL_TREE;
			break;
		case 's':		/* fstab */
			tabtype = TABTYPE_FSTAB;
			flags &= ~FL_TREE;
			break;
		case 'k':		/* kernel (mountinfo) */
			tabtype = TABTYPE_KERNEL;
			break;
		case 't':
			set_match(COL_FSTYPE, optarg);
			break;
		case 'r':
			flags &= ~FL_TREE;	/* disable the default */
			flags |= FL_RAW;	/* enable raw */
			break;
		case 'l':
			flags &= ~FL_TREE;	/* disable the default */
			break;
		case 'n':
			flags |= FL_NOHEADINGS;
			break;
		case 'N':
			tabtype = TABTYPE_KERNEL;
			tabfiles = append_pid_tabfile(tabfiles, &ntabfiles,
					strtou32_or_err(optarg,
						_("invalid TID argument")));
			break;
		case 'v':
			flags |= FL_NOFSROOT;
			break;
		case 'R':
			flags |= FL_SUBMOUNTS;
			break;
		case 'S':
			set_source_match(optarg);
			flags |= FL_NOSWAPMATCH;
			break;
		case 'T':
			set_match(COL_TARGET, optarg);
			flags |= FL_NOSWAPMATCH;
			break;
		case 'U':
			flags |= FL_UNIQ;
			break;
		case 'w':
			timeout = strtos32_or_err(optarg, _("invalid timeout argument"));
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		default:
			usage(stderr);
			break;
		}
	}

	if (!ncolumns && (flags & FL_DF)) {
		add_column(columns, ncolumns++, COL_SOURCE);
		add_column(columns, ncolumns++, COL_FSTYPE);
		add_column(columns, ncolumns++, COL_SIZE);
		add_column(columns, ncolumns++, COL_USED);
		add_column(columns, ncolumns++, COL_AVAIL);
		add_column(columns, ncolumns++, COL_USEPERC);
		add_column(columns, ncolumns++, COL_TARGET);
	}

	/* default columns */
	if (!ncolumns) {
		if (flags & FL_POLL)
			add_column(columns, ncolumns++, COL_ACTION);

		add_column(columns, ncolumns++, COL_TARGET);
		add_column(columns, ncolumns++, COL_SOURCE);
		add_column(columns, ncolumns++, COL_FSTYPE);
		add_column(columns, ncolumns++, COL_OPTIONS);
	}

	if (outarg && string_add_to_idarray(outarg, columns, ARRAY_SIZE(columns),
					 &ncolumns, column_name_to_id) < 0)
		return EXIT_FAILURE;

	if (!tabtype)
		tabtype = TABTYPE_KERNEL;

	if ((flags & FL_POLL) && ntabfiles > 1)
		errx(EXIT_FAILURE, _("--poll accepts only one file, but more specified by --tab-file"));

	if (optind < argc && (get_match(COL_SOURCE) || get_match(COL_TARGET)))
		errx(EXIT_FAILURE, _(
			"options --target and --source can't be used together "
			"with command line element that is not an option"));

	if (optind < argc)
		set_source_match(argv[optind++]);	/* dev/tag/mountpoint/maj:min */
	if (optind < argc)
		set_match(COL_TARGET, argv[optind++]);	/* mountpoint */

	if ((flags & FL_SUBMOUNTS) && is_listall_mode())
		/* don't care about submounts if list all mounts */
		flags &= ~FL_SUBMOUNTS;

	if (!(flags & FL_SUBMOUNTS) && ((flags & FL_FIRSTONLY)
	    || get_match(COL_TARGET)
	    || get_match(COL_SOURCE)
	    || get_match(COL_MAJMIN)))
		flags &= ~FL_TREE;

	if (!(flags & FL_NOSWAPMATCH) &&
	    !get_match(COL_TARGET) && get_match(COL_SOURCE)) {
		/*
		 * Check if we can swap source and target, it's
		 * not possible if the source is LABEL=/UUID=
		 */
		const char *x = get_match(COL_SOURCE);

		if (!strncmp(x, "LABEL=", 6) || !strncmp(x, "UUID=", 5) ||
		    !strncmp(x, "PARTLABEL=", 10) || !strncmp(x, "PARTUUID=", 9))
			flags |= FL_NOSWAPMATCH;
	}

	/*
	 * initialize libmount
	 */
	mnt_init_debug(0);
	scols_init_debug(0);

	tb = parse_tabfiles(tabfiles, ntabfiles, tabtype);
	if (!tb)
		goto leave;

	if ((flags & FL_TREE) && (ntabfiles > 1 || !tab_is_tree(tb)))
		flags &= ~FL_TREE;

	if (!(flags & FL_NOCACHE)) {
		cache = mnt_new_cache();
		if (!cache) {
			warn(_("failed to initialize libmount cache"));
			goto leave;
		}
		mnt_table_set_cache(tb, cache);

		if (tabtype != TABTYPE_KERNEL)
			cache_set_targets(cache);
	}

	if (flags & FL_UNIQ)
		mnt_table_uniq_fs(tb, MNT_UNIQ_KEEPTREE, uniq_fs_target_cmp);

	/*
	 * initialize output formatting (libsmartcols.h)
	 */
	table = scols_new_table();
	if (!table) {
		warn(_("failed to initialize output table"));
		goto leave;
	}
	scols_table_enable_raw(table,        !!(flags & FL_RAW));
	scols_table_enable_export(table,     !!(flags & FL_EXPORT));
	scols_table_enable_ascii(table,      !!(flags & FL_ASCII));
	scols_table_enable_noheadings(table, !!(flags & FL_NOHEADINGS));

	for (i = 0; i < ncolumns; i++) {
		int fl = get_column_flags(i);
		int id = get_column_id(i);

		if (!(flags & FL_TREE))
			fl &= ~SCOLS_FL_TREE;

		if (!(flags & FL_POLL) && is_tabdiff_column(id)) {
			warnx(_("%s column is requested, but --poll "
			       "is not enabled"), get_column_name(i));
			goto leave;
		}
		if (!scols_table_new_column(table, get_column_name(i),
					get_column_whint(i), fl)) {
			warn(_("failed to initialize output column"));
			goto leave;
		}
	}

	/*
	 * Fill in data to the output table
	 */
	if (flags & FL_POLL) {
		/* poll mode (accept the first tabfile only) */
		rc = poll_table(tb, tabfiles ? *tabfiles : _PATH_PROC_MOUNTINFO, timeout, table, direction);

	} else if ((flags & FL_TREE) && !(flags & FL_SUBMOUNTS)) {
		/* whole tree */
		rc = create_treenode(table, tb, NULL, NULL);
	} else {
		/* whole lits of sub-tree */
		rc = add_matching_lines(tb, table, direction);

		if (rc != 0
		    && tabtype == TABTYPE_KERNEL
		    && (flags & FL_NOSWAPMATCH)
		    && get_match(COL_TARGET)) {
			/*
			 * Found nothing, maybe the --target is regular file,
			 * try it again with extra functionality for target
			 * match
			 */
			enable_extra_target_match();
			rc = add_matching_lines(tb, table, direction);
		}
	}

	/*
	 * Print the output table for non-poll modes
	 */
	if (!rc && !(flags & FL_POLL))
		scols_print_table(table);
leave:
	scols_unref_table(table);

	mnt_unref_table(tb);
	mnt_unref_cache(cache);

	free(tabfiles);
#ifdef HAVE_LIBUDEV
	udev_unref(udev);
#endif
	return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
