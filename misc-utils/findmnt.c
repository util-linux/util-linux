/*
 * findmnt(8)
 *
 * Copyright (C) 2010-2015 Red Hat, Inc. All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
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
#include <blkid.h>
#include <libmount.h>
#include <libsmartcols.h>

#include "pathnames.h"
#include "nls.h"
#include "closestream.h"
#include "c.h"
#include "cctype.h"
#include "strutils.h"
#include "xalloc.h"
#include "optutils.h"
#include "mangle.h"
#include "buffer.h"
#include "column-list-table.h"
#include "ttyutils.h"

#include "findmnt.h"

/* column IDs */
enum {
	COL_TARGET,	/* Keep it first to make --output-all readable */

	COL_ACTION,
	COL_AVAIL,
	COL_FREQ,
	COL_FSROOT,
	COL_FSTYPE,
	COL_FS_OPTIONS,
	COL_ID,
	COL_INO_AVAIL,
	COL_INO_TOTAL,
	COL_INO_USED,
	COL_INO_USEPERC,
	COL_LABEL,
	COL_MAJMIN,
	COL_OLD_OPTIONS,
	COL_OLD_TARGET,
	COL_OPTIONS,
	COL_OPT_FIELDS,
	COL_PARENT,
	COL_PARTLABEL,
	COL_PARTUUID,
	COL_PASSNO,
	COL_PROPAGATION,
	COL_SIZE,
	COL_SOURCE,
	COL_SOURCES,
	COL_TID,
	COL_UNIQ_ID,
	COL_USED,
	COL_USEPERC,
	COL_UUID,
	COL_VFS_OPTIONS
};

enum {
	TABTYPE_FSTAB = 1,
	TABTYPE_MTAB,
	TABTYPE_KERNEL_MOUNTINFO,
	TABTYPE_KERNEL_LISTMOUNT
};

/* column names */
struct colinfo {
	const char	* const name;	/* header */
	double		whint;		/* width hint (N < 1 is in percent of termwidth) */
	int		flags;		/* libsmartcols flags */
	const char      *help;		/* column description */
	const char	*match;		/* pattern for match_func() */
	void		*match_data;	/* match specific data */
};

/* columns descriptions (don't use const, this is writable) */
static struct colinfo infos[] = {
	[COL_ACTION]       = { "ACTION",         10, SCOLS_FL_STRICTWIDTH, N_("action detected by --poll") },
	[COL_AVAIL]        = { "AVAIL",           5, SCOLS_FL_RIGHT, N_("filesystem size available, use <number> if --bytes is given") },
	[COL_FREQ]         = { "FREQ",            1, SCOLS_FL_RIGHT, N_("dump(8) period in days [fstab only]") },
	[COL_FSROOT]       = { "FSROOT",       0.25, SCOLS_FL_NOEXTREMES, N_("filesystem root") },
	[COL_FSTYPE]       = { "FSTYPE",       0.10, SCOLS_FL_TRUNC, N_("filesystem type") },
	[COL_FS_OPTIONS]   = { "FS-OPTIONS",   0.10, SCOLS_FL_TRUNC, N_("FS specific mount options") },
	[COL_ID]           = { "ID",              2, SCOLS_FL_RIGHT, N_("mount ID") },
	[COL_INO_AVAIL]    = { "INO.AVAIL",       5, SCOLS_FL_RIGHT, N_("number of available inodes") },
	[COL_INO_TOTAL]    = { "INO.TOTAL",       5, SCOLS_FL_RIGHT, N_("total number of inodes") },
	[COL_INO_USED]     = { "INO.USED",        5, SCOLS_FL_RIGHT, N_("number of used inodes") },
	[COL_INO_USEPERC]  = { "INO.USE%",        3, SCOLS_FL_RIGHT, N_("percentage of INO.USED divided by INO.TOTAL") },
	[COL_LABEL]        = { "LABEL",        0.10, 0, N_("filesystem label") },
	[COL_MAJMIN]       = { "MAJ:MIN",         6, 0, N_("major:minor device number") },
	[COL_OLD_OPTIONS]  = { "OLD-OPTIONS",  0.10, SCOLS_FL_TRUNC, N_("old mount options saved by --poll") },
	[COL_OLD_TARGET]   = { "OLD-TARGET",   0.30, 0, N_("old mountpoint saved by --poll") },
	[COL_OPTIONS]      = { "OPTIONS",      0.10, SCOLS_FL_TRUNC, N_("all mount options") },
	[COL_OPT_FIELDS]   = { "OPT-FIELDS",   0.10, SCOLS_FL_TRUNC, N_("optional mount fields") },
	[COL_PARENT]       = { "PARENT",          2, SCOLS_FL_RIGHT, N_("mount parent ID") },
	[COL_PARTLABEL]    = { "PARTLABEL",    0.10, 0, N_("partition label") },
	[COL_PARTUUID]     = { "PARTUUID",       36, 0, N_("partition UUID") },
	[COL_PASSNO]       = { "PASSNO",          1, SCOLS_FL_RIGHT, N_("pass number on parallel fsck(8) [fstab only]") },
	[COL_PROPAGATION]  = { "PROPAGATION",  0.10, 0, N_("VFS propagation flags") },
	[COL_SIZE]         = { "SIZE",            5, SCOLS_FL_RIGHT, N_("filesystem size, use <number> if --bytes is given") },
	[COL_SOURCES]      = { "SOURCES",      0.25, SCOLS_FL_WRAP, N_("all possible source devices") },
	[COL_SOURCE]       = { "SOURCE",       0.25, SCOLS_FL_NOEXTREMES, N_("source device") },
	[COL_TARGET]       = { "TARGET",       0.30, SCOLS_FL_TREE| SCOLS_FL_NOEXTREMES, N_("mountpoint") },
	[COL_TID]          = { "TID",             4, SCOLS_FL_RIGHT, N_("task ID") },
	[COL_UNIQ_ID]      = { "UNIQ-ID",        10, SCOLS_FL_RIGHT, N_("mount 64-bit ID (requires --kernel=listmount)") },
	[COL_USED]         = { "USED",            5, SCOLS_FL_RIGHT, N_("filesystem size used, use <number> if --bytes is given") },
	[COL_USEPERC]      = { "USE%",            3, SCOLS_FL_RIGHT, N_("filesystem use percentage") },
	[COL_UUID]         = { "UUID",           36, 0, N_("filesystem UUID") },
	[COL_VFS_OPTIONS]  = { "VFS-OPTIONS",  0.20, SCOLS_FL_TRUNC, N_("VFS specific mount options") }
};

/* columns[] array specifies all currently wanted output column. The columns
 * are defined by infos[] array and you can specify (on command line) each
 * column twice. That's enough, dynamically allocated array of the columns is
 * unnecessary overkill and over-engineering in this case */
static int columns[ARRAY_SIZE(infos) * 2];
static size_t ncolumns;

static inline void add_column(int id)
{
	if (ncolumns >= ARRAY_SIZE(columns))
		errx(EXIT_FAILURE, _("too many columns specified, "
				     "the limit is %zu columns"),
				ARRAY_SIZE(columns) - 1);
	columns[ ncolumns++ ] =  id;
}

/* poll actions (parsed --poll=<list> */
#define FINDMNT_NACTIONS	4		/* mount, umount, move, remount */
static int actions[FINDMNT_NACTIONS];
static int nactions;

static blkid_cache blk_cache;

#ifdef HAVE_LIBUDEV
static struct udev *udev;
#endif

/* "userdata" used by callback for libsmartcols filter */
struct filler_data {
	struct libmnt_fs *fs;
	struct findmnt *findmnt;
};

static int match_func(struct libmnt_fs *fs, void *data __attribute__ ((__unused__)));


static int get_column_id(int num)
{
	assert(num >= 0);
	assert((size_t) num < ncolumns);
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

#define is_defined_match(x)	(get_match(x) || get_match_data(x))

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
static void set_source_match(const char *data, struct findmnt *findmnt)
{
	int maj, min;

	if (sscanf(data, "%d:%d", &maj, &min) == 2) {
		dev_t *devno = xmalloc(sizeof(dev_t));

		*devno = makedev(maj, min);
		set_match(COL_MAJMIN, data);
		set_match_data(COL_MAJMIN, (void *) devno);
		findmnt->flags |= FL_NOSWAPMATCH;
	} else
		set_match(COL_SOURCE, data);
}

/*
 * Extra functionality for --target <path>. The function mnt_table_find_mountpoint()
 * also checks parents (path elements in reverse order) to get mountpoint.
 *
 * @tb has to be from kernel (so no fstab or so)!
 */
static void enable_extra_target_match(struct libmnt_table *tb, struct findmnt *findmnt)
{
	char *cn = NULL;
	const char *tgt = NULL, *mnt = NULL;
	struct libmnt_fs *fs;

	/*
	 * Check if match pattern is mountpoint, if not use the
	 * real mountpoint.
	 */
	if (findmnt->flags & FL_NOCACHE)
		tgt = get_match(COL_TARGET);
	else {
		tgt = cn = mnt_resolve_path(get_match(COL_TARGET), findmnt->cache);
		if (!cn)
			return;
	}

	fs = mnt_table_find_mountpoint(tb, tgt, MNT_ITER_BACKWARD);
	if (fs)
		mnt = mnt_fs_get_target(fs);
	if (mnt && strcmp(mnt, tgt) != 0)
		set_match(COL_TARGET, xstrdup(mnt));	/* replace the current setting */

	if (!findmnt->cache)
		free(cn);
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
int is_listall_mode(unsigned int flags)
{
	if ((flags & FL_DF || flags & FL_REAL || flags & FL_PSEUDO) && !(flags & FL_ALL))
		return 0;

	return (!is_defined_match(COL_SOURCE) &&
		!is_defined_match(COL_TARGET) &&
		!is_defined_match(COL_FSTYPE) &&
		!is_defined_match(COL_OPTIONS) &&
		!is_defined_match(COL_ID) &&
		!is_defined_match(COL_UNIQ_ID) &&
		!is_defined_match(COL_MAJMIN));
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

	if (c_strncasecmp(name, "move", namesz) == 0 && namesz == 4)
		id = MNT_TABDIFF_MOVE;
	else if (c_strncasecmp(name, "mount", namesz) == 0 && namesz == 5)
		id = MNT_TABDIFF_MOUNT;
	else if (c_strncasecmp(name, "umount", namesz) == 0 && namesz == 6)
		id = MNT_TABDIFF_UMOUNT;
	else if (c_strncasecmp(name, "remount", namesz) == 0 && namesz == 7)
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
static int is_mount_compatible_mode(unsigned int flags)
{
	if (!is_defined_match(COL_SOURCE))
	       return 0;		/* <devname|TAG=|mountpoint> is required */
	if (is_defined_match(COL_FSTYPE)
	    || is_defined_match(COL_OPTIONS)
	    || is_defined_match(COL_ID)
	    || is_defined_match(COL_UNIQ_ID))
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

		if (!c_strncasecmp(name, cn, namesz) && !*(cn + namesz))
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
		, struct findmnt *findmnt)
{
	const char *t, *v;
	char *res = NULL;

	if (!mnt_fs_get_tag(fs, &t, &v) && !strcmp(t, tagname))
		res = xstrdup(v);
	else {
		const char *dev = mnt_fs_get_source(fs);

		if (dev && !(findmnt->flags & FL_NOCACHE))
			dev = mnt_resolve_spec(dev, findmnt->cache);
#ifdef HAVE_LIBUDEV
		if (dev)
			res = get_tag_from_udev(dev, col);
#endif
		if (!res) {
			res = mnt_cache_find_tag_value(findmnt->cache, dev, tagname);
			if (res && findmnt->cache)
				/* don't return pointer to cache */
				res = xstrdup(res);
		}
	}

	return res;
}

static char *get_vfs_attr(struct libmnt_fs *fs, int sizetype, unsigned int flags)
{
	struct statvfs buf;
	uint64_t vfs_attr = 0;
	char *sizestr;
	bool no_bytes = false;

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
	case COL_INO_AVAIL:
		/* Quoted from startvfs(3):
		 *
		 *   Under Linux, f_favail is always the same as
		 *   f_ffree, and there's no way for a filesystem to
		 *   report otherwise.  This is not an issue, since no
		 *   filesystems with an inode root reservation exist.
		 */
		no_bytes = true;
		vfs_attr = buf.f_ffree;
		break;
	case COL_INO_TOTAL:
		no_bytes = true;
		vfs_attr = buf.f_files;
		break;
	case COL_INO_USED:
		no_bytes = true;
		vfs_attr = buf.f_files - buf.f_ffree;
		break;
	case COL_INO_USEPERC:
		if (buf.f_files == 0)
			return xstrdup("-");

		xasprintf(&sizestr, "%.0f%%",
				(double)(buf.f_files - buf.f_ffree) /
				buf.f_files * 100);
		return sizestr;
	}

	if (!vfs_attr)
		sizestr = xstrdup("0");
	else if ((flags & FL_BYTES) || no_bytes)
		xasprintf(&sizestr, "%ju", vfs_attr);
	else
		sizestr = size_to_human_string(SIZE_SUFFIX_1LETTER, vfs_attr);

	return sizestr;
}

/* reads sources from libmount/libblkid
 */
static char *get_data_col_sources(struct libmnt_fs *fs, int evaluate, size_t *datasiz)
{
	const char *tag = NULL, *p = NULL;
	const char *device = NULL;
	char *val = NULL;
	blkid_dev_iterate iter;
	blkid_dev dev;
	struct ul_buffer buf = UL_INIT_BUFFER;

	/* get TAG from libmount if available (e.g. fstab) */
	if (mnt_fs_get_tag(fs, &tag, &p) == 0) {

		/* if device is in the form 'UUID=..' or 'LABEL=..' and evaluate==0
		 * then it is ok to do not search for multiple devices
		 */
		if (!evaluate)
			goto nothing;
		val = xstrdup(p);
	}

	/* or get device name */
	else if (!(device = mnt_fs_get_srcpath(fs))
		 || strncmp(device, "/dev/", 5) != 0)
		goto nothing;

	if (!blk_cache) {
		if (blkid_get_cache(&blk_cache, NULL) != 0)
			goto nothing;
		blkid_probe_all(blk_cache);
	}

	/* ask libblkid for the UUID */
	if (!val) {
		assert(device);

		tag = "UUID";
		val = blkid_get_tag_value(blk_cache, "UUID", device);	/* returns allocated string */
		if (!val)
			goto nothing;
	}

	assert(val);
	assert(tag);

	/* scan all devices for the TAG */
	iter = blkid_dev_iterate_begin(blk_cache);
	blkid_dev_set_search(iter, tag, val);

	while (blkid_dev_next(iter, &dev) == 0) {
		dev = blkid_verify(blk_cache, dev);
		if (!dev)
			continue;
		ul_buffer_append_string(&buf, blkid_dev_devname(dev));
		ul_buffer_append_data(&buf, "\0", 1);
	}
	blkid_dev_iterate_end(iter);
	free(val);

	return ul_buffer_get_data(&buf, datasiz, NULL);

nothing:
	free(val);
	return NULL;
}

/* reads FS data from libmount
 */
static char *get_data(struct libmnt_fs *fs, int num, size_t *datasiz, struct findmnt *findmnt)
{
	char *str = NULL;
	int col_id = get_column_id(num);

	switch (col_id) {
	case COL_SOURCES:
		/* print all devices with the same tag (LABEL, UUID) */
		str = get_data_col_sources(fs, findmnt->flags & FL_EVALUATE, datasiz);
		if (str)
			break;

		FALLTHROUGH;
	case COL_SOURCE:
	{
		const char *root = mnt_fs_get_root(fs);
		const char *spec = mnt_fs_get_srcpath(fs);
		char *cn = NULL;

		if (spec && (findmnt->flags & FL_CANONICALIZE))
			spec = cn = mnt_resolve_path(spec, findmnt->cache);
		if (!spec) {
			spec = mnt_fs_get_source(fs);

			if (spec && (findmnt->flags & FL_EVALUATE))
				spec = cn = mnt_resolve_spec(spec, findmnt->cache);
		}
		if (root && spec && !(findmnt->flags & FL_NOFSROOT) && strcmp(root, "/") != 0)
			xasprintf(&str, "%s[%s]", spec, root);
		else if (spec)
			str = xstrdup(spec);
		if (!findmnt->cache)
			free(cn);
		break;
	}

	case COL_TARGET:
		if (mnt_fs_get_target(fs))
			str = xstrdup(mnt_fs_get_target(fs));
		break;
	case COL_FSTYPE:
		if (mnt_fs_get_fstype(fs))
			str = xstrdup(mnt_fs_get_fstype(fs));
		break;
	case COL_OPTIONS:
		if (mnt_fs_get_options(fs))
			str = xstrdup(mnt_fs_get_options(fs));
		break;
	case COL_VFS_OPTIONS:
		if (findmnt->flags & FL_VFS_ALL)
			str = mnt_fs_get_vfs_options_all(fs);
		else if (mnt_fs_get_vfs_options(fs))
			str = xstrdup(mnt_fs_get_vfs_options(fs));
		break;
	case COL_FS_OPTIONS:
		if (mnt_fs_get_fs_options(fs))
			str = xstrdup(mnt_fs_get_fs_options(fs));
		break;
	case COL_OPT_FIELDS:
		if (mnt_fs_get_optional_fields(fs))
			str = xstrdup(mnt_fs_get_optional_fields(fs));
		break;
	case COL_UUID:
		str = get_tag(fs, "UUID", col_id, findmnt);
		break;
	case COL_PARTUUID:
		str = get_tag(fs, "PARTUUID", col_id, findmnt);
		break;
	case COL_LABEL:
		str = get_tag(fs, "LABEL", col_id, findmnt);
		break;
	case COL_PARTLABEL:
		str = get_tag(fs, "PARTLABEL", col_id, findmnt);
		break;

	case COL_MAJMIN:
	{
		dev_t devno = mnt_fs_get_devno(fs);
		if (!devno)
			break;

		if ((findmnt->flags & FL_RAW) || (findmnt->flags & FL_EXPORT) || (findmnt->flags & FL_JSON))
			xasprintf(&str, "%u:%u", major(devno), minor(devno));
		else
			xasprintf(&str, "%3u:%-3u", major(devno), minor(devno));
		break;
	}
	case COL_SIZE:
	case COL_AVAIL:
	case COL_USED:
	case COL_USEPERC:
	case COL_INO_TOTAL:
	case COL_INO_AVAIL:
	case COL_INO_USED:
	case COL_INO_USEPERC:
		str = get_vfs_attr(fs, col_id, findmnt->flags);
		break;
	case COL_FSROOT:
		if (mnt_fs_get_root(fs))
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
	case COL_UNIQ_ID:
		if (mnt_fs_get_uniq_id(fs))
			xasprintf(&str, "%" PRIu64, mnt_fs_get_uniq_id(fs));
		break;
	case COL_PARENT:
		if (mnt_fs_get_parent_id(fs))
			xasprintf(&str, "%d", mnt_fs_get_parent_id(fs));
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
				    int num,
				    size_t *datasiz,
				    struct findmnt *findmnt)
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
			       change == MNT_TABDIFF_UMOUNT)
		           && mnt_fs_get_options(old_fs))
			str = xstrdup(mnt_fs_get_options(old_fs));
		break;
	case COL_OLD_TARGET:
		if (old_fs && (change == MNT_TABDIFF_MOVE ||
			       change == MNT_TABDIFF_UMOUNT)
			   && mnt_fs_get_target(old_fs))
			str = xstrdup(mnt_fs_get_target(old_fs));
		break;
	default:
		if (new_fs)
			str = get_data(new_fs, num, datasiz, findmnt);
		else
			str = get_data(old_fs, num, datasiz, findmnt);
		break;
	}
	return str;
}

static void set_line_data(struct libscols_line *ln, size_t i, char *data, size_t datasiz)
{
	int rc;
	struct libscols_cell *ce;

	ce = scols_line_get_cell(ln, i);
	if (!ce)
		return;
	if (datasiz)
		rc = scols_cell_refer_memory(ce, data, datasiz);
	else
		rc = scols_cell_refer_data(ce, data);
	if (rc)
		err(EXIT_FAILURE, _("failed to add output data"));
}

static int filter_filler_cb(
		 struct libscols_filter *filter __attribute__((__unused__)),
		 struct libscols_line *line,
		 size_t column_index,
		 void *userdata)
{
	struct filler_data *fid = (struct filler_data *) userdata;
	size_t datasiz = 0;

	char *data = get_data(fid->fs, column_index, &datasiz, fid->findmnt);
	if (data)
		set_line_data(line, column_index, data, datasiz);
	return 0;
}

/* adds one line to the output @tab */
static struct libscols_line *add_line(struct libscols_table *table, struct libmnt_fs *fs,
				      struct libscols_line *parent, struct findmnt *findmnt, bool *filtered)
{
	size_t i;
	struct libscols_line *line = scols_table_new_line(table, parent);

	if (!line)
		err(EXIT_FAILURE, _("failed to allocate output line"));

	if (findmnt->filter) {
		int status = 0;
		struct filler_data fid = {
			.fs = fs,
			.findmnt = findmnt,
		};

		scols_filter_set_filler_cb(findmnt->filter,
				filter_filler_cb, (void *) &fid);

		if (scols_line_apply_filter(line, findmnt->filter, &status))
			err(EXIT_FAILURE, _("failed to apply filter"));

		if (status == 0) {
			if (parent)
				scols_line_remove_child(parent, line);
			scols_table_remove_line(table, line);
			*filtered = true;
			return NULL;
		}
	}

	for (i = 0; i < ncolumns; i++) {
		size_t datasiz = 0;
		char *data;

		if (scols_line_is_filled(line, i))
			continue;

		data = get_data(fs, i, &datasiz, findmnt);

		if (data)
			set_line_data(line, i, data, datasiz);
	}

	scols_line_set_userdata(line, fs);
	return line;
}

static struct libscols_line *add_tabdiff_line(struct libscols_table *table, struct libmnt_fs *new_fs,
			struct libmnt_fs *old_fs, int change, struct findmnt *findmnt)
{
	size_t i;
	struct libscols_line *line = scols_table_new_line(table, NULL);

	if (!line)
		err(EXIT_FAILURE, _("failed to allocate output line"));

	for (i = 0; i < ncolumns; i++) {
		size_t datasiz = 0;
		char *data = get_tabdiff_data(old_fs, new_fs, change, i, &datasiz, findmnt);

		if (data)
			set_line_data(line, i, data, datasiz);
	}

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
			   struct libmnt_fs *fs, struct libscols_line *parent_line, struct findmnt *findmnt)
{
	struct libmnt_fs *chld = NULL;
	struct libmnt_iter *itr = NULL;
	struct libscols_line *line;
	int rc = -1, first = 0;

	if (!fs) {
		/* first call, get root FS */
		if (mnt_table_get_root_fs(tb, &fs))
			goto leave;
		parent_line = NULL;
		first = 1;

	} else if ((findmnt->flags & FL_SUBMOUNTS) && has_line(table, fs))
		return 0;

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr)
		goto leave;

	if ((findmnt->flags & FL_SUBMOUNTS) || match_func(fs, findmnt)) {
		bool filtered = false;
		if (has_line(table, fs))
			goto leave;

		line = add_line(table, fs, parent_line, findmnt, &filtered);
		if (filtered)
			line = parent_line;
		else if (!line)
			goto leave;
	} else
		line = parent_line;

	/*
	 * add all children to the output table
	 */
	while (mnt_table_next_child_fs(tb, itr, fs, &chld) == 0) {
		if (create_treenode(table, tb, chld, line, findmnt))
			goto leave;
	}
	rc = 0;

	/* make sure all entries are in the tree */
	if (first && (size_t) mnt_table_get_nents(tb) >
		     (size_t) scols_table_get_nlines(table)) {
		mnt_reset_iter(itr, MNT_ITER_FORWARD);
		fs = NULL;
		while (mnt_table_next_fs(tb, itr, &fs) == 0) {
			if (!has_line(table, fs) && match_func(fs, findmnt))
				create_treenode(table, tb, fs, NULL, findmnt);
		}
	}
leave:
	mnt_free_iter(itr);
	return rc;
}

/* error callback */
static int parser_errcb(struct libmnt_table *tb,
			const char *filename, int line)
{
	struct findmnt *findmnt = mnt_table_get_userdata(tb);
	warnx(_("%s: parse error at line %d -- ignored"), filename, line);
	findmnt->parse_nerrors++;
	return 1;
}

static char **append_tabfile(char **files, int *nfiles, char *filename)
{
	files = xreallocarray(files, *nfiles + 1, sizeof(char *));
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
		case TABTYPE_KERNEL_MOUNTINFO:
			if (!path)
				path = access(_PATH_PROC_MOUNTINFO, R_OK) == 0 ?
					      _PATH_PROC_MOUNTINFO :
					      _PATH_PROC_MOUNTS;

			rc = mnt_table_parse_file(tb, path);
			break;
		default:
			rc = -EINVAL;
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

static struct libmnt_table *fetch_listmount(void)
{
	struct libmnt_table *tb = NULL;
	struct libmnt_statmnt *sm = NULL;

	sm = mnt_new_statmnt();
	if (!sm) {
		warn(_("failed to allocate statmnt handler"));
		goto failed;
	}

	tb = mnt_new_table();
	if (!tb) {
		warn(_("failed to initialize libmount table"));
		goto failed;
	}

	mnt_table_refer_statmnt(tb, sm);

	if (mnt_table_fetch_listmount(tb) != 0) {
		warn(_("failed to fetch mount nodes"));
		goto failed;
	}

	return tb;
failed:
	mnt_unref_table(tb);
	mnt_unref_statmnt(sm);
	return NULL;
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
	struct libmnt_iter *itr;
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

/* checks if all fs in @tb are from kernel */
static int tab_is_kernel(struct libmnt_table *tb)
{
	struct libmnt_fs *fs = NULL;
	struct libmnt_iter *itr;

	itr = mnt_new_iter(MNT_ITER_BACKWARD);
	if (!itr)
		return 0;

	while (mnt_table_next_fs(tb, itr, &fs) == 0) {
		if (!mnt_fs_is_kernel(fs)) {
			mnt_free_iter(itr);
			return 0;
		}
	}

	mnt_free_iter(itr);
	return 1;
}

/* filter function for libmount (mnt_table_find_next_fs()) */
static int match_func(struct libmnt_fs *fs,
		      void *data)
{
	struct findmnt *findmnt = data;
	int rc = findmnt->flags & FL_INVERT ? 1 : 0;
	const char *m;
	void *md;

	md = get_match_data(COL_ID);
	if (md && mnt_fs_get_id(fs) != *((int *) md))
		return rc;

	md = get_match_data(COL_UNIQ_ID);
	if (md && mnt_fs_get_uniq_id(fs) != *((uint64_t *) md))
		return rc;

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
	if (m && !mnt_fs_match_target(fs, m, findmnt->cache))
		return rc;

	m = get_match(COL_SOURCE);
	if (m && !mnt_fs_match_source(fs, m, findmnt->cache))
		return rc;

	if ((findmnt->flags & FL_DF) && !(findmnt->flags & FL_ALL)) {
		const char *type = mnt_fs_get_fstype(fs);

		if (type && strstr(type, "tmpfs"))	/* tmpfs is wanted */
			return !rc;

		if (mnt_fs_is_pseudofs(fs))
			return rc;
	}

	if ((findmnt->flags & FL_REAL) && mnt_fs_is_pseudofs(fs))
	    return rc;

	if ((findmnt->flags & FL_PSEUDO) && !mnt_fs_is_pseudofs(fs))
	    return rc;

	if ((findmnt->flags & FL_SHADOWED)) {
		struct libmnt_table *tb = NULL;

		mnt_fs_get_table(fs, &tb);
		if (tb && mnt_table_over_fs(tb, fs, NULL) != 0)
			return rc;
	}

	return !rc;
}

/* iterate over filesystems in @tb */
struct libmnt_fs *get_next_fs(struct libmnt_table *tb,
				     struct libmnt_iter *itr,
				     struct findmnt *findmnt)
{
	struct libmnt_fs *fs = NULL;

	if (is_listall_mode(findmnt->flags)) {
		/*
		 * Print whole file
		 */
		if (mnt_table_next_fs(tb, itr, &fs) != 0)
			return NULL;

	} else if (is_mount_compatible_mode(findmnt->flags)) {
		/*
		 * Look up for FS in the same way how mount(8) searches in fstab
		 *
		 *   findmnt -f <spec>
		 */
		fs = mnt_table_find_source(tb, get_match(COL_SOURCE),
					mnt_iter_get_direction(itr));

		if (!fs && !(findmnt->flags & FL_NOSWAPMATCH))
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
		if (mnt_table_find_next_fs(tb, itr, match_func, findmnt, &fs) != 0)
			fs = NULL;

		if (!fs &&
		    !(findmnt->flags & FL_NOSWAPMATCH) &&
		    !is_defined_match(COL_TARGET) && is_defined_match(COL_SOURCE)) {

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
			      struct libscols_table *table, int direction,
			      struct findmnt *findmnt)
{
	struct libmnt_iter *itr;
	struct libmnt_fs *fs;
	int nlines = 0, rc = -1;

	itr = mnt_new_iter(direction);
	if (!itr) {
		warn(_("failed to initialize libmount iterator"));
		goto done;
	}

	while((fs = get_next_fs(tb, itr, findmnt))) {
		if ((findmnt->flags & FL_TREE) || (findmnt->flags & FL_SUBMOUNTS))
			rc = create_treenode(table, tb, fs, NULL, findmnt);
		else {
			bool filtered = false;
			struct libscols_line *l = add_line(table, fs, NULL, findmnt, &filtered);
			if (filtered)
				continue;
			rc = !l;
		}
		if (rc)
			goto done;
		nlines++;
		if (findmnt->flags & FL_FIRSTONLY)
			break;
		findmnt->flags |= FL_NOSWAPMATCH;
	}

	if (nlines)
		rc = 0;
done:
	mnt_free_iter(itr);
	return rc;
}

static int poll_match(struct libmnt_fs *fs, struct findmnt *findmnt)
{
	int rc = match_func(fs, findmnt);

	if (rc == 0 && !(findmnt->flags & FL_NOSWAPMATCH) &&
	    is_defined_match(COL_SOURCE) && !is_defined_match(COL_TARGET)) {
		/*
		 * findmnt --poll /foo
		 * The '/foo' maybe source as well as target.
		 */
		const char *str = get_match(COL_SOURCE);

		set_match(COL_TARGET, str);	/* swap */
		set_match(COL_SOURCE, NULL);

		rc = match_func(fs, findmnt);

		set_match(COL_TARGET, NULL);	/* restore */
		set_match(COL_SOURCE, str);

	}
	return rc;
}

static int poll_table(struct libmnt_table *tb, const char *tabfile,
		  int timeout, struct libscols_table *table, int direction, struct findmnt *findmnt)
{
	FILE *f = NULL;
	int rc = -1;
	struct libmnt_iter *itr = NULL;
	struct libmnt_table *tb_new;
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
			if (!poll_match(new ? new : old, findmnt))
				continue;
			count++;
			rc = !add_tabdiff_line(table, new, old, change, findmnt);
			if (rc)
				goto done;
			if (findmnt->flags & FL_FIRSTONLY)
				break;
		}

		if (count) {
			rc = scols_table_print_range(table, NULL, NULL);
			fflush(scols_table_get_stream(table));
			if (rc)
				goto done;
		}

		/* swap tables */
		tmp = tb;
		tb = tb_new;
		tb_new = tmp;

		/* remove already printed lines to reduce memory usage */
		scols_table_remove_lines(table);
		mnt_reset_table(tb_new);

		if (count && (findmnt->flags & FL_FIRSTONLY))
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
		struct libmnt_table *tb,
		struct libmnt_fs *a,
		struct libmnt_fs *b)
{
	struct findmnt *findmnt = mnt_table_get_userdata(tb);
	return !mnt_fs_match_target(a, mnt_fs_get_target(b), findmnt->cache);
}

static int get_column_json_type(int id, int scols_flags, int *multi, unsigned int findmnt_flags)
{
	switch (id) {
	case COL_SIZE:
	case COL_AVAIL:
	case COL_USED:
		if (multi)
			*multi = 1;
		if (!(findmnt_flags & FL_BYTES))
			break;
		FALLTHROUGH;
	case COL_ID:
	case COL_UNIQ_ID:
	case COL_PARENT:
	case COL_FREQ:
	case COL_PASSNO:
	case COL_TID:
		return SCOLS_JSON_NUMBER;

	default:
		if (scols_flags & SCOLS_FL_WRAP)
			return SCOLS_JSON_ARRAY_STRING;
		break;
	}

	return SCOLS_JSON_STRING;	/* default */
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(
	" %1$s [options]\n"
	" %1$s [options] <device> | <mountpoint>\n"
	" %1$s [options] <device> <mountpoint>\n"
	" %1$s [options] [--source <device>] [--target <path> | --mountpoint <dir>]\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Find a (mounted) filesystem.\n"), out);


	fputs(_("\nData sources:\n"), out);
	fputs(_(" -F, --tab-file <path>  alternative file for -s, -m or -k options\n"), out);
	fputs(_(" -m, --mtab             search in table of mounted filesystems\n"
		"                          (includes user space mount options)\n"), out);
	fputs(_(" -k                     alias to '--kernel mountinfo'\n"), out);
	fputs(_(" --kernel[=<method>]    search in kernel mount table (default)\n"
		"                          <method> is mountinfo or listmount\n"), out);
	fputs(_(" -N, --task <tid>       use alternative namespace (/proc/<tid>/mountinfo file)\n"), out);
	fputs(_(" -p, --poll[=<list>]    monitor changes in table of mounted filesystems\n"), out);
	fputs(_(" -s, --fstab            search in static table of filesystems\n"), out);


	fputs(_("\nData filters:\n"), out);
	fputs(_(" -A, --all              disable all built-in filters, print all filesystems\n"), out);
	fputs(_(" -d, --direction <word> direction of search, 'forward' or 'backward'\n"), out);
	fputs(_(" -f, --first-only       print the first found filesystem only\n"), out);
	fputs(_(" -i, --invert           invert the sense of matching\n"), out);
	fputs(_("     --id <num>         filter by mount node ID\n"), out);
	fputs(_("     --uniq-id <num>    filter by mount node 64-bit ID (requires --kernel=listmount)\n"), out);
	fputs(_("     --pseudo           print only pseudo-filesystems\n"), out);
	fputs(_(" -Q, --filter <expr>    apply display filter\n"), out);
	fputs(_(" -M, --mountpoint <dir> the mountpoint directory\n"), out);
	fputs(_("     --shadowed         print only filesystems over-mounted by another filesystem\n"), out);
	fputs(_(" -R, --submounts        print all submounts for the matching filesystems\n"), out);
	fputs(_("     --real             print only real filesystems\n"), out);
	fputs(_(" -S, --source <string>  the device to mount (by name, maj:min, \n"
	        "                          LABEL=, UUID=, PARTUUID=, PARTLABEL=)\n"), out);
	fputs(_(" -T, --target <path>    the path to the filesystem to use\n"), out);
	fputs(_(" -t, --types <list>     limit the set of filesystems by FS types\n"), out);
	fputs(_(" -U, --uniq             ignore filesystems with duplicate target\n"), out);


	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --ascii            use ASCII chars for tree formatting\n"), out);
	fputs(_(" -b, --bytes            print sizes in bytes rather than in human readable format\n"), out);
	fputs(_(" -C, --nocanonicalize   don't canonicalize when comparing paths\n"), out);
	fputs(_(" -c, --canonicalize     canonicalize printed paths\n"), out);
	fputs(_(" -D, --df               imitate the output of df(1)\n"), out);
	fputs(_(" -e, --evaluate         convert tags (LABEL,UUID,PARTUUID,PARTLABEL) \n"
	        "                          to device names\n"), out);
	fputs(_("     --hyperlink[=<when>]\n"
		"                        print paths as hyperlinks (always|never|auto)\n"), out);
	fputs(_(" -I, --dfi              imitate the output of df(1) with -i option\n"), out);
	fputs(_(" -J, --json             use JSON output format\n"), out);
	fputs(_(" -l, --list             use list format output\n"), out);
	fputs(_(" -n, --noheadings       don't print column headings\n"), out);
	fputs(_(" -O, --options <list>   limit the set of filesystems by mount options\n"), out);
	fputs(_(" -o, --output <list>    output columns (see --list-columns)\n"), out);
	fputs(_("     --output-all       output all available columns\n"), out);
	fputs(_(" -P, --pairs            use key=\"value\" output format\n"), out);
	fputs(_(" -r, --raw              use raw output format\n"), out);
	fputs(_("     --tree             enable tree format output if possible\n"), out);
	fputs(_(" -u, --notruncate       don't truncate text in columns\n"), out);
	fputs(_(" -v, --nofsroot         don't print [/dir] for bind or btrfs mounts\n"), out);
	fputs(_(" -w, --timeout <num>    upper limit in milliseconds that --poll will block\n"), out);
	fputs(_(" -y, --shell            use column names to be usable as shell variable identifiers\n"), out);

	fputc('\n', out);
	fputs(_(" -x, --verify           verify mount table content (default is fstab)\n"), out);
	fputs(_("     --verbose          print more details\n"), out);
	fputs(_("     --vfs-all          print all VFS options\n"), out);


	fputs(USAGE_SEPARATOR, out);
	fputs(_(" -H, --list-columns     list the available columns\n"), out);
	fprintf(out, USAGE_HELP_OPTIONS(24));

	fprintf(out, USAGE_MAN_TAIL("findmnt(8)"));

	exit(EXIT_SUCCESS);
}

static void __attribute__((__noreturn__)) list_colunms(struct findmnt *findmnt)
{
	size_t i;
	struct libscols_table *tb = xcolumn_list_table_new("findmnt-columns", stdout,
						findmnt->flags & FL_RAW,
						findmnt->flags & FL_JSON);

	for (i = 0; i < ARRAY_SIZE(infos); i++) {
		const struct colinfo *ci = &infos[i];
		int multi = 0;
		int json = get_column_json_type(i, ci->flags, &multi,
						findmnt->flags);

		xcolumn_list_table_append_line(tb, ci->name,
			multi ? -1 : json,
			multi ? "<string|number>" : NULL,
		       _(ci->help));
	}

	scols_print_table(tb);
	scols_unref_table(tb);

	exit(EXIT_SUCCESS);
}

static struct libscols_table *init_scols_table(struct findmnt *findmnt)
{
	struct libscols_table *table = scols_new_table();
	unsigned int flags = findmnt->flags;
	bool use_filter = findmnt->filter? true: false;

	if (!table) {
		warn(_("failed to allocate output table"));
		goto leave;
	}

	scols_table_enable_raw(table,        !!(flags & FL_RAW));
	scols_table_enable_export(table,     !!(flags & FL_EXPORT));
	scols_table_enable_shellvar(table,   !!(flags & FL_SHELLVAR));
	scols_table_enable_json(table,       !!(flags & FL_JSON));
	scols_table_enable_ascii(table,      !!(flags & FL_ASCII));
	scols_table_enable_noheadings(table, !!(flags & FL_NOHEADINGS));

	if (flags & FL_JSON)
		scols_table_set_name(table, "filesystems");

	for (size_t i = 0; i < ncolumns; i++) {
		struct libscols_column *cl;
		int fl = get_column_flags(i);
		int id = get_column_id(i);

		if (!(flags & FL_TREE))
			fl &= ~SCOLS_FL_TREE;

		if (!(flags & FL_POLL) && is_tabdiff_column(id)) {
			warnx(_("%s column is requested, but --poll "
			       "is not enabled"), get_column_name(i));
			goto leave;
		}
		cl = scols_table_new_column(table, get_column_name(i),
					get_column_whint(i), fl);
		if (!cl)	{
			warn(_("failed to allocate output column"));
			goto leave;
		}

		if (findmnt->uri && id == COL_TARGET)
			scols_column_set_uri(cl, findmnt->uri);

		/* multi-line cells (now used for SOURCES) */
		if (fl & SCOLS_FL_WRAP)
			scols_column_set_wrapfunc(cl,
						NULL,
						scols_wrapzero_nextchunk,
						NULL);
		if ((flags & FL_JSON) || use_filter)
	                scols_column_set_json_type(cl, get_column_json_type(id, fl, NULL,
									    flags));
	}

 leave:
	return table;
}

static struct libscols_filter *new_filter(const char *query)
{
	struct libscols_filter *f;

	f = scols_new_filter(NULL);
	if (!f)
		err(EXIT_FAILURE, _("failed to allocate filter"));
	if (query && scols_filter_parse_string(f, query) != 0)
		errx(EXIT_FAILURE, _("failed to parse \"%s\": %s"), query,
				scols_filter_get_errmsg(f));
	return f;
}

static void init_scols_filter(struct libscols_table *tb, struct libscols_filter *f, unsigned int flags)
{
	struct libscols_iter *itr;
	const char *name = NULL;
	int nerrs = 0;

	itr = scols_new_iter(SCOLS_ITER_FORWARD);
	if (!itr)
		err(EXIT_FAILURE, _("failed to allocate iterator"));

	while (scols_filter_next_holder(f, itr, &name, 0) == 0) {
		struct libscols_column *col = scols_table_get_column_by_name(tb, name);
		int id = column_name_to_id(name, strlen(name));
		const struct colinfo *ci = id >= 0 ? &infos[id] : NULL;

		if (!ci) {
			nerrs++;
			continue;	/* report all unknown columns */
		}
		if (!col) {
			int json = get_column_json_type(id, ci->flags, NULL, flags);
			add_column(id);
			col = scols_table_new_column(tb, ci->name,
						     ci->whint, SCOLS_FL_HIDDEN);
			if (!col)
				err(EXIT_FAILURE,_("failed to allocate output column"));

			scols_column_set_json_type(col, json);
		}

		scols_filter_assign_column(f, itr, name, col);
	}

	scols_free_iter(itr);

	if (!nerrs)
		return;

	errx(EXIT_FAILURE, _("failed to initialize filter"));
}

int main(int argc, char *argv[])
{
	struct findmnt findmnt = {
		.cache = NULL,
		.flags = 0,
		.parse_nerrors = 0,
		.filter = NULL,
	};
	struct libmnt_table *tb = NULL;
	char **tabfiles = NULL;
	int direction = MNT_ITER_FORWARD;
	int verify = 0, collist = 0;
	int c, rc = -1, timeout = -1;
	int ntabfiles = 0, tabtype = 0;
	char *outarg = NULL;
	size_t i;
	int force_tree = 0, istree = 0;

	struct libscols_table *table = NULL;

	enum {
		FINDMNT_OPT_VERBOSE = CHAR_MAX + 1,
		FINDMNT_OPT_TREE,
		FINDMNT_OPT_OUTPUT_ALL,
		FINDMNT_OPT_PSEUDO,
		FINDMNT_OPT_REAL,
		FINDMNT_OPT_VFS_ALL,
		FINDMNT_OPT_SHADOWED,
		FINDMNT_OPT_HYPERLINK,
		FINDMNT_OPT_ID,
		FINDMNT_OPT_UNIQ_ID,
		FINDMNT_OPT_KERNEL
	};

	static const struct option longopts[] = {
		{ "all",	    no_argument,       NULL, 'A'		 },
		{ "ascii",	    no_argument,       NULL, 'a'		 },
		{ "bytes",	    no_argument,       NULL, 'b'		 },
		{ "canonicalize",   no_argument,       NULL, 'c'		 },
		{ "direction",	    required_argument, NULL, 'd'		 },
		{ "df",		    no_argument,       NULL, 'D'		 },
		{ "dfi",	    no_argument,       NULL, 'I'		 },
		{ "evaluate",	    no_argument,       NULL, 'e'		 },
		{ "first-only",	    no_argument,       NULL, 'f'		 },
		{ "fstab",	    no_argument,       NULL, 's'		 },
		{ "help",	    no_argument,       NULL, 'h'		 },
		{ "invert",	    no_argument,       NULL, 'i'		 },
		{ "json",	    no_argument,       NULL, 'J'		 },
		{ "kernel",	    optional_argument, NULL, FINDMNT_OPT_KERNEL	 },
		{ "list",	    no_argument,       NULL, 'l'		 },
		{ "mountpoint",	    required_argument, NULL, 'M'		 },
		{ "mtab",	    no_argument,       NULL, 'm'		 },
		{ "noheadings",	    no_argument,       NULL, 'n'		 },
		{ "notruncate",	    no_argument,       NULL, 'u'		 },
		{ "options",	    required_argument, NULL, 'O'		 },
		{ "output",	    required_argument, NULL, 'o'		 },
		{ "output-all",	    no_argument,       NULL, FINDMNT_OPT_OUTPUT_ALL },
		{ "poll",	    optional_argument, NULL, 'p'		 },
		{ "pairs",	    no_argument,       NULL, 'P'		 },
		{ "filter",	    required_argument, NULL, 'Q'		 },
		{ "raw",	    no_argument,       NULL, 'r'		 },
		{ "types",	    required_argument, NULL, 't'		 },
		{ "nocanonicalize", no_argument,       NULL, 'C'		 },
		{ "nofsroot",	    no_argument,       NULL, 'v'		 },
		{ "submounts",	    no_argument,       NULL, 'R'		 },
		{ "source",	    required_argument, NULL, 'S'		 },
		{ "tab-file",	    required_argument, NULL, 'F'		 },
		{ "task",	    required_argument, NULL, 'N'		 },
		{ "target",	    required_argument, NULL, 'T'		 },
		{ "timeout",	    required_argument, NULL, 'w'		 },
		{ "uniq",	    no_argument,       NULL, 'U'		 },
		{ "verify",	    no_argument,       NULL, 'x'		 },
		{ "version",	    no_argument,       NULL, 'V'		 },
		{ "shell",          no_argument,       NULL, 'y'                 },
		{ "verbose",	    no_argument,       NULL, FINDMNT_OPT_VERBOSE },
		{ "tree",	    no_argument,       NULL, FINDMNT_OPT_TREE	 },
		{ "real",	    no_argument,       NULL, FINDMNT_OPT_REAL	 },
		{ "pseudo",	    no_argument,       NULL, FINDMNT_OPT_PSEUDO	 },
		{ "vfs-all",	    no_argument,       NULL, FINDMNT_OPT_VFS_ALL },
		{ "shadowed",       no_argument,       NULL, FINDMNT_OPT_SHADOWED },
		{ "hyperlink",      optional_argument, NULL, FINDMNT_OPT_HYPERLINK },
		{ "id",             required_argument, NULL, FINDMNT_OPT_ID },
		{ "uniq-id",        required_argument, NULL, FINDMNT_OPT_UNIQ_ID },
		{ "list-columns",   no_argument,       NULL, 'H' },
		{ NULL, 0, NULL, 0 }
	};

	static const ul_excl_t excl[] = {	/* rows and cols in ASCII order */
		{ 'C', 'c'},			/* [no]canonicalize */
		{ 'C', 'e' },			/* nocanonicalize, evaluate */
		{ 'J', 'P', 'r','x' },		/* json,pairs,raw,verify */
		{ 'M', 'T' },			/* mountpoint, target */
		{ 'N','k','m','s' },		/* task,kernel,mtab,fstab */
		{ 'P','l','r','x' },		/* pairs,list,raw,verify */
		{ 'Q', 'p' },			/* filter,poll */
		{ 'p','x' },			/* poll,verify */
		{ 'm','p','s' },		/* mtab,poll,fstab */
		{ FINDMNT_OPT_PSEUDO, FINDMNT_OPT_REAL },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	close_stdout_atexit();

	/* default output format */
	findmnt.flags |= FL_TREE;

	while ((c = getopt_long(argc, argv,
				"AabCcDd:ehIiJfF:o:O:p::PQ:klmM:nN:rst:uvRS:T:Uw:VxyH",
				longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch(c) {
		case 'A':
			findmnt.flags |= FL_ALL;
			break;
		case 'a':
			findmnt.flags |= FL_ASCII;
			break;
		case 'b':
			findmnt.flags |= FL_BYTES;
			break;
		case 'C':
			findmnt.flags |= FL_NOCACHE;
			break;
		case 'c':
			findmnt.flags |= FL_CANONICALIZE;
			break;
		case 'D':
			findmnt.flags &= ~FL_TREE;
			findmnt.flags |= FL_DF;
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
			findmnt.flags |= FL_EVALUATE;
			break;
		case 'I':
			findmnt.flags &= ~FL_TREE;
			findmnt.flags |= (FL_DF_INODES | FL_DF);
			break;
		case 'i':
			findmnt.flags |= FL_INVERT;
			break;
		case 'J':
			findmnt.flags |= FL_JSON;
			break;
		case 'f':
			findmnt.flags |= FL_FIRSTONLY;
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
		case FINDMNT_OPT_OUTPUT_ALL:
			ncolumns = 0;
			for (i = 0; i < ARRAY_SIZE(infos); i++) {
				if (is_tabdiff_column(i))
					continue;
				columns[ncolumns++] = i;
			}
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
			findmnt.flags |= FL_POLL;
			findmnt.flags &= ~FL_TREE;
			break;
		case 'P':
			findmnt.flags |= FL_EXPORT;
			findmnt.flags &= ~FL_TREE;
			break;
		case 'Q':
			findmnt.filter = new_filter(optarg);
			break;
		case 'm':		/* mtab */
			tabtype = TABTYPE_MTAB;
			findmnt.flags &= ~FL_TREE;
			break;
		case 's':		/* fstab */
			tabtype = TABTYPE_FSTAB;
			findmnt.flags &= ~FL_TREE;
			break;
		case 'k':
			tabtype = TABTYPE_KERNEL_MOUNTINFO;
			break;
		case FINDMNT_OPT_KERNEL:
			if (optarg) {
				if (strcmp(optarg, "mountinfo") == 0)
					tabtype = TABTYPE_KERNEL_MOUNTINFO;
				else if (strcmp(optarg, "listmount") == 0)
					tabtype = TABTYPE_KERNEL_LISTMOUNT;
				else
					errx(EXIT_FAILURE, _("invalid --kernel argument"));
			} else
				tabtype = TABTYPE_KERNEL_MOUNTINFO;
			break;
		case 't':
			set_match(COL_FSTYPE, optarg);
			break;
		case 'r':
			findmnt.flags &= ~FL_TREE;	/* disable the default */
			findmnt.flags |= FL_RAW;	/* enable raw */
			break;
		case 'l':
			findmnt.flags &= ~FL_TREE;	/* disable the default */
			break;
		case 'n':
			findmnt.flags |= FL_NOHEADINGS;
			break;
		case 'N':
			tabtype = TABTYPE_KERNEL_MOUNTINFO;
			tabfiles = append_pid_tabfile(tabfiles, &ntabfiles,
					strtou32_or_err(optarg,
						_("invalid TID argument")));
			break;
		case 'v':
			findmnt.flags |= FL_NOFSROOT;
			break;
		case 'R':
			findmnt.flags |= FL_SUBMOUNTS;
			break;
		case 'S':
			set_source_match(optarg, &findmnt);
			findmnt.flags |= FL_NOSWAPMATCH;
			break;
		case 'M':
			findmnt.flags |= FL_STRICTTARGET;
			FALLTHROUGH;
		case 'T':
			set_match(COL_TARGET, optarg);
			findmnt.flags |= FL_NOSWAPMATCH;
			break;
		case 'U':
			findmnt.flags |= FL_UNIQ;
			break;
		case 'w':
			timeout = strtos32_or_err(optarg, _("invalid timeout"));
			break;
		case 'x':
			verify = 1;
			break;
		case 'y':
			findmnt.flags |= FL_SHELLVAR;
			break;
		case FINDMNT_OPT_VERBOSE:
			findmnt.flags |= FL_VERBOSE;
			break;
		case FINDMNT_OPT_TREE:
			force_tree = 1;
			break;
		case FINDMNT_OPT_PSEUDO:
			findmnt.flags |= FL_PSEUDO;
			break;
		case FINDMNT_OPT_REAL:
			findmnt.flags |= FL_REAL;
			break;
		case FINDMNT_OPT_VFS_ALL:
			findmnt.flags |= FL_VFS_ALL;
			break;
		case FINDMNT_OPT_SHADOWED:
			findmnt.flags |= FL_SHADOWED;
			break;
		case FINDMNT_OPT_HYPERLINK:
			if (hyperlinkwanted(optarg))
				findmnt.uri = xgethosturi(NULL);
			break;
		case FINDMNT_OPT_ID:
			{
				int *id = xmalloc(sizeof(int));

				*id = strtos32_or_err(optarg, _("invalid id argument"));
				set_match_data(COL_ID, (void *) id);
				break;
			}
		case FINDMNT_OPT_UNIQ_ID:
			{
				uint64_t *id = xmalloc(sizeof(uint64_t));

				*id = strtou64_or_err(optarg, _("invalid uniq-id argument"));
				set_match_data(COL_UNIQ_ID, (void *) id);
				break;
			}
		case 'H':
			collist = 1;
			break;
		case 'h':
			usage();
		case 'V':
			print_version(EXIT_SUCCESS);
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (collist)
		list_colunms(&findmnt);		/* print end exit */

	if (!ncolumns && (findmnt.flags & FL_DF)) {
		add_column(COL_SOURCE);
		add_column(COL_FSTYPE);
		if (findmnt.flags & FL_DF_INODES) {
			add_column(COL_INO_TOTAL);
			add_column(COL_INO_USED);
			add_column(COL_INO_AVAIL);
			add_column(COL_INO_USEPERC);
		} else {
			add_column(COL_SIZE);
			add_column(COL_USED);
			add_column(COL_AVAIL);
			add_column(COL_USEPERC);
		}
		add_column(COL_TARGET);
	}

	/* default columns */
	if (!ncolumns) {
		if (findmnt.flags & FL_POLL)
			add_column(COL_ACTION);

		add_column(COL_TARGET);
		add_column(COL_SOURCE);
		add_column(COL_FSTYPE);
		add_column(COL_OPTIONS);
	}

	if (outarg && string_add_to_idarray(outarg, columns, ARRAY_SIZE(columns),
					 &ncolumns, column_name_to_id) < 0)
		return EXIT_FAILURE;

	if (!tabtype)
		tabtype = verify ? TABTYPE_FSTAB : TABTYPE_KERNEL_MOUNTINFO;

	if ((findmnt.flags & FL_POLL) && ntabfiles > 1)
		errx(EXIT_FAILURE, _("--poll accepts only one file, but more specified by --tab-file"));

	if (ntabfiles && tabtype == TABTYPE_KERNEL_LISTMOUNT)
		errx(EXIT_FAILURE, _(
			"options --kernel=listmount and --tab-file or --task can't be used together"));

	if (optind < argc && (is_defined_match(COL_SOURCE) || is_defined_match(COL_TARGET)))
		errx(EXIT_FAILURE, _(
			"options --target and --source can't be used together "
			"with command line element that is not an option"));

	if (optind < argc)
		set_source_match(argv[optind++], &findmnt);	/* dev/tag/mountpoint/maj:min */
	if (optind < argc)
		set_match(COL_TARGET, argv[optind++]);	/* mountpoint */

	if ((findmnt.flags & FL_SUBMOUNTS) && is_listall_mode(findmnt.flags))
		/* don't care about submounts if list all mounts */
		findmnt.flags &= ~FL_SUBMOUNTS;

	if (!(findmnt.flags & FL_SUBMOUNTS) && ((findmnt.flags & FL_FIRSTONLY)
	    || is_defined_match(COL_TARGET)
	    || is_defined_match(COL_SOURCE)
	    || is_defined_match(COL_MAJMIN)))
		findmnt.flags &= ~FL_TREE;

	if (!(findmnt.flags & FL_NOSWAPMATCH) &&
	    !is_defined_match(COL_TARGET) && is_defined_match(COL_SOURCE)) {
		/*
		 * Check if we can swap source and target; it's
		 * not possible if the source is LABEL=/UUID=
		 */
		const char *x = get_match(COL_SOURCE);

		if (!strncmp(x, "LABEL=", 6) || !strncmp(x, "UUID=", 5) ||
		    !strncmp(x, "PARTLABEL=", 10) || !strncmp(x, "PARTUUID=", 9))
			findmnt.flags |= FL_NOSWAPMATCH;
	}

	/*
	 * initialize libmount
	 */
	mnt_init_debug(0);

	if (tabtype == TABTYPE_KERNEL_LISTMOUNT)
		tb = fetch_listmount();
	else
		tb = parse_tabfiles(tabfiles, ntabfiles, tabtype);
	if (!tb)
		goto leave;
	mnt_table_set_userdata(tb, &findmnt);

	if (tabtype == TABTYPE_MTAB && tab_is_kernel(tb))
		tabtype = TABTYPE_KERNEL_MOUNTINFO;

	istree = tab_is_tree(tb);
	if (istree && force_tree)
		findmnt.flags |= FL_TREE;

	if ((findmnt.flags & FL_TREE) && (ntabfiles > 1 || !istree))
		findmnt.flags &= ~FL_TREE;

	if (!(findmnt.flags & FL_NOCACHE)) {
		findmnt.cache = mnt_new_cache();
		if (!findmnt.cache) {
			warn(_("failed to initialize libmount cache"));
			goto leave;
		}
		mnt_table_set_cache(tb, findmnt.cache);

		if (tabtype == TABTYPE_FSTAB || tabtype == TABTYPE_MTAB)
			cache_set_targets(findmnt.cache);
	}

	if (findmnt.flags & FL_UNIQ)
		mnt_table_uniq_fs(tb, MNT_UNIQ_KEEPTREE, uniq_fs_target_cmp);

	if (verify) {
		rc = verify_table(tb, &findmnt);
		goto leave;
	}

	/*
	 * initialize libsmartcols
	 */
	scols_init_debug(0);
	table = init_scols_table(&findmnt);
	if (!table)
		goto leave;

	init_scols_filter(table, findmnt.filter, findmnt.flags);


	/*
	 * Fill in data to the output table
	 */
	if (findmnt.flags & FL_POLL) {
		/* poll mode (accept the first tabfile only) */
		rc = poll_table(tb, tabfiles ? *tabfiles : _PATH_PROC_MOUNTINFO, timeout, table, direction, &findmnt);

	} else if ((findmnt.flags & FL_TREE) && !(findmnt.flags & FL_SUBMOUNTS)) {
		/* whole tree */
		rc = create_treenode(table, tb, NULL, NULL, &findmnt);
	} else {
		/* whole list of sub-tree */
		rc = add_matching_lines(tb, table, direction, &findmnt);

		if (rc != 0
		    && tabtype == TABTYPE_KERNEL_MOUNTINFO
		    && (findmnt.flags & FL_NOSWAPMATCH)
		    && !(findmnt.flags & FL_STRICTTARGET)
		    && is_defined_match(COL_TARGET)) {
			/*
			 * Found nothing, maybe the --target is regular file,
			 * try it again with extra functionality for target
			 * match
			 */
			enable_extra_target_match(tb, &findmnt);
			rc = add_matching_lines(tb, table, direction, &findmnt);
		}
	}

	/*
	 * Print the output table for non-poll modes
	 */
	if (!rc && !(findmnt.flags & FL_POLL))
		scols_print_table(table);
leave:
	scols_unref_table(table);
	scols_unref_filter(findmnt.filter);

	mnt_unref_table(tb);
	mnt_unref_cache(findmnt.cache);

	free(tabfiles);
#ifdef HAVE_LIBUDEV
	udev_unref(udev);
#endif
	return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
