/*
 * findmnt(8)
 *
 * Copyright (C) 2010,2011 Red Hat, Inc. All rights reserved.
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

#include "pathnames.h"
#include "nls.h"
#include "closestream.h"
#include "c.h"
#include "tt.h"
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
	FL_ALL		= (1 << 11)
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
	COL_PASSNO,

	FINDMNT_NCOLUMNS
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
	int		flags;		/* tt flags */
	const char      *help;		/* column description */
	const char	*match;		/* pattern for match_func() */
	void		*match_data;	/* match specific data */
};

/* columns descriptions (don't use const, this is writable) */
static struct colinfo infos[FINDMNT_NCOLUMNS] = {
	[COL_SOURCE]       = { "SOURCE",       0.25, TT_FL_NOEXTREMES, N_("source device") },
	[COL_TARGET]       = { "TARGET",       0.30, TT_FL_TREE | TT_FL_NOEXTREMES, N_("mountpoint") },
	[COL_FSTYPE]       = { "FSTYPE",       0.10, TT_FL_TRUNC, N_("filesystem type") },
	[COL_OPTIONS]      = { "OPTIONS",      0.10, TT_FL_TRUNC, N_("all mount options") },
	[COL_VFS_OPTIONS]  = { "VFS-OPTIONS",  0.20, TT_FL_TRUNC, N_("VFS specific mount options") },
	[COL_FS_OPTIONS]   = { "FS-OPTIONS",   0.10, TT_FL_TRUNC, N_("FS specific mount options") },
	[COL_LABEL]        = { "LABEL",        0.10, 0, N_("filesystem label") },
	[COL_UUID]         = { "UUID",           36, 0, N_("filesystem UUID") },
	[COL_PARTLABEL]    = { "PARTLABEL",    0.10, 0, N_("partition label") },
	[COL_PARTUUID]     = { "PARTUUID",       36, 0, N_("partition UUID") },
	[COL_MAJMIN]       = { "MAJ:MIN",         6, 0, N_("major:minor device number") },
	[COL_ACTION]       = { "ACTION",         10, TT_FL_STRICTWIDTH, N_("action detected by --poll") },
	[COL_OLD_OPTIONS]  = { "OLD-OPTIONS",  0.10, TT_FL_TRUNC, N_("old mount options saved by --poll") },
	[COL_OLD_TARGET]   = { "OLD-TARGET",   0.30, 0, N_("old mountpoint saved by --poll") },
	[COL_SIZE]         = { "SIZE",            5, TT_FL_RIGHT, N_("filesystem size") },
	[COL_AVAIL]        = { "AVAIL",           5, TT_FL_RIGHT, N_("filesystem size available") },
	[COL_USED]         = { "USED",            5, TT_FL_RIGHT, N_("filesystem size used") },
	[COL_USEPERC]      = { "USE%",            3, TT_FL_RIGHT, N_("filesystem use percentage") },
	[COL_FSROOT]       = { "FSROOT",       0.25, TT_FL_NOEXTREMES, N_("filesystem root") },
	[COL_TID]          = { "TID",             4, TT_FL_RIGHT, N_("task ID") },
	[COL_ID]           = { "ID",              2, TT_FL_RIGHT, N_("mount ID") },
	[COL_OPT_FIELDS]   = { "OPT-FIELDS",   0.10, TT_FL_TRUNC, N_("optional mount fields") },
	[COL_PROPAGATION]  = { "PROPAGATION",  0.10, 0, N_("VFS propagation flags") },
	[COL_FREQ]         = { "FREQ",            1, TT_FL_RIGHT, N_("dump(8) period in days [fstab only]") },
	[COL_PASSNO]       = { "PASSNO",          1, TT_FL_RIGHT, N_("pass number on parallel fsck(8) [fstab only]") }
};

/* global flags */
static int flags;
static int tt_flags;

/* array with IDs of enabled columns */
static int columns[FINDMNT_NCOLUMNS];
static int ncolumns;

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
	assert(columns[num] < FINDMNT_NCOLUMNS);
	return columns[num];
}

static struct colinfo *get_column_info(int num)
{
	return &infos[ get_column_id(num) ];
}

static const char *column_id_to_name(int id)
{
	assert(id < FINDMNT_NCOLUMNS);
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
	assert(id < FINDMNT_NCOLUMNS);
	return infos[id].match;
}

static void *get_match_data(int id)
{
	assert(id < FINDMNT_NCOLUMNS);
	return infos[id].match_data;
}

static void set_match(int id, const char *match)
{
	assert(id < FINDMNT_NCOLUMNS);
	infos[id].match = match;
}

static void set_match_data(int id, void *data)
{
	assert(id < FINDMNT_NCOLUMNS);
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
	char *cn = NULL, *mnt = NULL;

	/*
	 * Check if match pattern is mountpoint, if not use the
	 * real mountpoint.
	 */
	cn = mnt_resolve_path(get_match(COL_TARGET), cache);
	if (!cn)
		return;

	mnt = mnt_get_mountpoint(cn);
	if (!mnt || strcmp(mnt, cn) == 0)
		return;

	/* replace the current setting with the real mountpoint */
	set_match(COL_TARGET, mnt);
}


static int is_tabdiff_column(int id)
{
	assert(id < FINDMNT_NCOLUMNS);

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
	int i;

	for (i = 0; i < FINDMNT_NCOLUMNS; i++)
		infos[i].flags &= ~TT_FL_TRUNC;
}

/*
 * converts @name to column ID
 */
static int column_name_to_id(const char *name, size_t namesz)
{
	int i;

	for (i = 0; i < FINDMNT_NCOLUMNS; i++) {
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

		if (dev)
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

	return vfs_attr == 0 ? xstrdup("0") :
		size_to_human_string(SIZE_SUFFIX_1LETTER, vfs_attr);
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

		if ((tt_flags & TT_FL_RAW) || (tt_flags & TT_FL_EXPORT))
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
static struct tt_line *add_line(struct tt *tt, struct libmnt_fs *fs,
					struct tt_line *parent)
{
	int i;
	struct tt_line *line = tt_add_line(tt, parent);

	if (!line) {
		warn(_("failed to add line to output"));
		return NULL;
	}
	for (i = 0; i < ncolumns; i++)
		tt_line_set_data(line, i, get_data(fs, i));

	tt_line_set_userdata(line, fs);
	return line;
}

static struct tt_line *add_tabdiff_line(struct tt *tt, struct libmnt_fs *new_fs,
			struct libmnt_fs *old_fs, int change)
{
	int i;
	struct tt_line *line = tt_add_line(tt, NULL);

	if (!line) {
		warn(_("failed to add line to output"));
		return NULL;
	}
	for (i = 0; i < ncolumns; i++)
		tt_line_set_data(line, i,
				get_tabdiff_data(old_fs, new_fs, change, i));

	return line;
}

static int has_line(struct tt *tt, struct libmnt_fs *fs)
{
	struct list_head *p;

	list_for_each(p, &tt->tb_lines) {
		struct tt_line *ln = list_entry(p, struct tt_line, ln_lines);
		if ((struct libmnt_fs *) ln->userdata == fs)
			return 1;
	}
	return 0;
}

/* reads filesystems from @tb (libmount) and fillin @tt (output table) */
static int create_treenode(struct tt *tt, struct libmnt_table *tb,
			   struct libmnt_fs *fs, struct tt_line *parent_line)
{
	struct libmnt_fs *chld = NULL;
	struct libmnt_iter *itr = NULL;
	struct tt_line *line;
	int rc = -1;

	if (!fs) {
		/* first call, get root FS */
		if (mnt_table_get_root_fs(tb, &fs))
			goto leave;
		parent_line = NULL;

	} else if ((flags & FL_SUBMOUNTS) && has_line(tt, fs))
		return 0;

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr)
		goto leave;

	if ((flags & FL_SUBMOUNTS) || match_func(fs, NULL)) {
		line = add_line(tt, fs, parent_line);
		if (!line)
			goto leave;
	} else
		line = parent_line;

	/*
	 * add all children to the output table
	 */
	while(mnt_table_next_child_fs(tb, itr, fs, &chld) == 0) {
		if (create_treenode(tt, tb, chld, line))
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

/* checks if @tb contains parent->child relations */
static int tab_is_tree(struct libmnt_table *tb)
{
	struct libmnt_fs *fs = NULL;
	struct libmnt_iter *itr = NULL;
	int rc = 0;

	itr = mnt_new_iter(MNT_ITER_BACKWARD);
	if (!itr)
		return 0;

	if (mnt_table_next_fs(tb, itr, &fs) == 0)
		rc = mnt_fs_get_id(fs) > 0 && mnt_fs_get_parent_id(fs) > 0;

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
			      struct tt *tt, int direction)
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
		if ((tt_flags & TT_FL_TREE) || (flags & FL_SUBMOUNTS))
			rc = create_treenode(tt, tb, fs, NULL);
		else
			rc = !add_line(tt, fs, NULL);
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
		  int timeout, struct tt *tt, int direction)
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
			rc = !add_tabdiff_line(tt, new, old, change);
			if (rc)
				goto done;
			if (flags & FL_FIRSTONLY)
				break;
		}

		if (count) {
			rc = tt_print_table(tt);
			if (rc)
				goto done;
		}

		/* swap tables */
		tmp = tb;
		tb = tb_new;
		tb_new = tmp;

		tt_remove_lines(tt);
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

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	int i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(
	" %1$s [options]\n"
	" %1$s [options] <device> | <mountpoint>\n"
	" %1$s [options] <device> <mountpoint>\n"
	" %1$s [options] [--source <device>] [--target <mountpoint>]\n"),
		program_invocation_short_name);

	fprintf(out, _(
	"\nOptions:\n"
	" -s, --fstab            search in static table of filesystems\n"
	" -m, --mtab             search in table of mounted filesystems\n"
	" -k, --kernel           search in kernel table of mounted\n"
        "                          filesystems (default)\n\n"));

	fprintf(out, _(
	" -p, --poll[=<list>]    monitor changes in table of mounted filesystems\n"
	" -w, --timeout <num>    upper limit in milliseconds that --poll will block\n\n"));

	fprintf(out, _(
	" -A, --all              disable all built-in filters, print all filesystems\n"
	" -a, --ascii            use ASCII chars for tree formatting\n"
	" -c, --canonicalize     canonicalize printed paths\n"
	" -D, --df               imitate the output of df(1)\n"
	" -d, --direction <word> direction of search, 'forward' or 'backward'\n"
	" -e, --evaluate         convert tags (LABEL,UUID,PARTUUID,PARTLABEL) \n"
	"                          to device names\n"
	" -F, --tab-file <path>  alternative file for --fstab, --mtab or --kernel options\n"
	" -f, --first-only       print the first found filesystem only\n"));

	fprintf(out, _(
	" -i, --invert           invert the sense of matching\n"
	" -l, --list             use list format output\n"
	" -N, --task <tid>       use alternative namespace (/proc/<tid>/mountinfo file)\n"
	" -n, --noheadings       don't print column headings\n"
	" -u, --notruncate       don't truncate text in columns\n"));
fputs (_(" -O, --options <list>   limit the set of filesystems by mount options\n"), out);
fputs (_(" -o, --output <list>    the output columns to be shown\n"), out);
fputs (_(" -P, --pairs            use key=\"value\" output format\n"), out);
fputs (_(" -r, --raw              use raw output format\n"), out);
fputs (_(" -t, --types <list>     limit the set of filesystems by FS types\n"), out);
	fprintf(out, _(
	" -v, --nofsroot         don't print [/dir] for bind or btrfs mounts\n"
	" -R, --submounts        print all submounts for the matching filesystems\n"
	" -S, --source <string>  the device to mount (by name, maj:min, \n"
	"                          LABEL=, UUID=, PARTUUID=, PARTLABEL=)\n"
	" -T, --target <string>  the mountpoint to use\n"));

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, _("\nAvailable columns:\n"));

	for (i = 0; i < FINDMNT_NCOLUMNS; i++)
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

	struct tt *tt = NULL;

	static const struct option longopts[] = {
	    { "all",          0, 0, 'A' },
	    { "ascii",        0, 0, 'a' },
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
	    { "nofsroot",     0, 0, 'v' },
	    { "submounts",    0, 0, 'R' },
	    { "source",       1, 0, 'S' },
	    { "tab-file",     1, 0, 'F' },
	    { "task",         1, 0, 'N' },
	    { "target",       1, 0, 'T' },
	    { "timeout",      1, 0, 'w' },
	    { "version",      0, 0, 'V' },

	    { NULL,           0, 0, 0 }
	};

	static const ul_excl_t excl[] = {	/* rows and cols in in ASCII order */
		{ 'N','k','m','s' },		/* task,kernel,mtab,fstab */
		{ 'P','l','r' },		/* pairs,list,raw */
		{ 'm','p','s' },		/* mtab,poll,fstab */
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	assert(ARRAY_SIZE(columns) == FINDMNT_NCOLUMNS);

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	/* default output format */
	tt_flags |= TT_FL_TREE;

	while ((c = getopt_long(argc, argv,
				"AacDd:ehifF:o:O:p::PklmnN:rst:uvRS:T:w:V",
				longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch(c) {
		case 'A':
			flags |= FL_ALL;
			break;
		case 'a':
			tt_flags |= TT_FL_ASCII;
			break;
		case 'c':
			flags |= FL_CANONICALIZE;
			break;
		case 'D':
			tt_flags &= ~TT_FL_TREE;
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
			tt_flags &= ~TT_FL_TREE;
			break;
		case 'P':
			tt_flags |= TT_FL_EXPORT;
			tt_flags &= ~TT_FL_TREE;
			break;
		case 'm':		/* mtab */
			tabtype = TABTYPE_MTAB;
			tt_flags &= ~TT_FL_TREE;
			break;
		case 's':		/* fstab */
			tabtype = TABTYPE_FSTAB;
			tt_flags &= ~TT_FL_TREE;
			break;
		case 'k':		/* kernel (mountinfo) */
			tabtype = TABTYPE_KERNEL;
			break;
		case 't':
			set_match(COL_FSTYPE, optarg);
			break;
		case 'r':
			tt_flags &= ~TT_FL_TREE;	/* disable the default */
			tt_flags |= TT_FL_RAW;		/* enable raw */
			break;
		case 'l':
			tt_flags &= ~TT_FL_TREE; /* disable the default */
			break;
		case 'n':
			tt_flags |= TT_FL_NOHEADINGS;
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
		columns[ncolumns++] = COL_SOURCE;
		columns[ncolumns++] = COL_FSTYPE;
		columns[ncolumns++] = COL_SIZE;
		columns[ncolumns++] = COL_USED;
		columns[ncolumns++] = COL_AVAIL;
		columns[ncolumns++] = COL_USEPERC;
		columns[ncolumns++] = COL_TARGET;
	}

	/* default columns */
	if (!ncolumns) {
		if (flags & FL_POLL)
			columns[ncolumns++] = COL_ACTION;

		columns[ncolumns++] = COL_TARGET;
		columns[ncolumns++] = COL_SOURCE;
		columns[ncolumns++] = COL_FSTYPE;
		columns[ncolumns++] = COL_OPTIONS;
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
		tt_flags &= ~TT_FL_TREE;

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

	tb = parse_tabfiles(tabfiles, ntabfiles, tabtype);
	if (!tb)
		goto leave;

	if ((tt_flags & TT_FL_TREE) && (ntabfiles > 1 || !tab_is_tree(tb)))
		tt_flags &= ~TT_FL_TREE;

	cache = mnt_new_cache();
	if (!cache) {
		warn(_("failed to initialize libmount cache"));
		goto leave;
	}
	mnt_table_set_cache(tb, cache);


	/*
	 * initialize output formatting (tt.h)
	 */
	tt = tt_new_table(tt_flags | TT_FL_FREEDATA);
	if (!tt) {
		warn(_("failed to initialize output table"));
		goto leave;
	}

	for (i = 0; i < ncolumns; i++) {
		int fl = get_column_flags(i);
		int id = get_column_id(i);

		if (!(tt_flags & TT_FL_TREE))
			fl &= ~TT_FL_TREE;

		if (!(flags & FL_POLL) && is_tabdiff_column(id)) {
			warnx(_("%s column is requested, but --poll "
			       "is not enabled"), get_column_name(i));
			goto leave;
		}
		if (!tt_define_column(tt, get_column_name(i),
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
		rc = poll_table(tb, tabfiles ? *tabfiles : _PATH_PROC_MOUNTINFO, timeout, tt, direction);

	} else if ((tt_flags & TT_FL_TREE) && !(flags & FL_SUBMOUNTS)) {
		/* whole tree */
		rc = create_treenode(tt, tb, NULL, NULL);
	} else {
		/* whole lits of sub-tree */
		rc = add_matching_lines(tb, tt, direction);

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
			rc = add_matching_lines(tb, tt, direction);
		}
	}

	/*
	 * Print the output table for non-poll modes
	 */
	if (!rc && !(flags & FL_POLL))
		tt_print_table(tt);
leave:
	tt_free_table(tt);

	mnt_unref_table(tb);
	mnt_unref_cache(cache);

	free(tabfiles);
#ifdef HAVE_LIBUDEV
	udev_unref(udev);
#endif
	return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
