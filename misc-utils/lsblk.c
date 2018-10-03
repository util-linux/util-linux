/*
 * lsblk(8) - list block devices
 *
 * Copyright (C) 2010,2011,2012 Red Hat, Inc. All rights reserved.
 * Written by Milan Broz <mbroz@redhat.com>
 *            Karel Zak <kzak@redhat.com>
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
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <locale.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>
#include <assert.h>

#include <blkid.h>

#include "c.h"
#include "pathnames.h"
#include "blkdev.h"
#include "canonicalize.h"
#include "nls.h"
#include "xalloc.h"
#include "strutils.h"
#include "sysfs.h"
#include "closestream.h"
#include "optutils.h"

#include "lsblk.h"

UL_DEBUG_DEFINE_MASK(lsblk);
UL_DEBUG_DEFINE_MASKNAMES(lsblk) = UL_DEBUG_EMPTY_MASKNAMES;

#define LSBLK_EXIT_SOMEOK 64
#define LSBLK_EXIT_ALLFAILED 32

static int column_id_to_number(int id);

/* column IDs */
enum {
	COL_NAME = 0,
	COL_KNAME,
	COL_PATH,
	COL_MAJMIN,
	COL_FSAVAIL,
	COL_FSSIZE,
	COL_FSTYPE,
	COL_FSUSED,
	COL_FSUSEPERC,
	COL_TARGET,
	COL_LABEL,
	COL_UUID,
	COL_PTUUID,
	COL_PTTYPE,
	COL_PARTTYPE,
	COL_PARTLABEL,
	COL_PARTUUID,
	COL_PARTFLAGS,
	COL_RA,
	COL_RO,
	COL_RM,
	COL_HOTPLUG,
	COL_MODEL,
	COL_SERIAL,
	COL_SIZE,
	COL_STATE,
	COL_OWNER,
	COL_GROUP,
	COL_MODE,
	COL_ALIOFF,
	COL_MINIO,
	COL_OPTIO,
	COL_PHYSEC,
	COL_LOGSEC,
	COL_ROTA,
	COL_SCHED,
	COL_RQ_SIZE,
	COL_TYPE,
	COL_DALIGN,
	COL_DGRAN,
	COL_DMAX,
	COL_DZERO,
	COL_WSAME,
	COL_WWN,
	COL_RAND,
	COL_PKNAME,
	COL_HCTL,
	COL_TRANSPORT,
	COL_SUBSYS,
	COL_REV,
	COL_VENDOR,
	COL_ZONED,
};

/* basic table settings */
enum {
	LSBLK_ASCII =		(1 << 0),
	LSBLK_RAW =		(1 << 1),
	LSBLK_NOHEADINGS =	(1 << 2),
	LSBLK_EXPORT =		(1 << 3),
	LSBLK_TREE =		(1 << 4),
	LSBLK_JSON =		(1 << 5),
};

/* Types used for qsort() and JSON */
enum {
	COLTYPE_STR	= 0,	/* default */
	COLTYPE_NUM	= 1,	/* always u64 number */
	COLTYPE_SORTNUM = 2,	/* string on output, u64 for qsort() */
	COLTYPE_SIZE	= 3,	/* srring by default, number when --bytes */
	COLTYPE_BOOL	= 4	/* 0 or 1 */
};

/* column names */
struct colinfo {
	const char	*name;		/* header */
	double		whint;		/* width hint (N < 1 is in percent of termwidth) */
	int		flags;		/* SCOLS_FL_* */
	const char      *help;

	int		type;		/* COLTYPE_* */
};

/* columns descriptions */
static struct colinfo infos[] = {
	[COL_NAME]   = { "NAME",    0.25, SCOLS_FL_TREE | SCOLS_FL_NOEXTREMES, N_("device name") },
	[COL_KNAME]  = { "KNAME",   0.3, 0, N_("internal kernel device name") },
	[COL_PKNAME] = { "PKNAME",  0.3, 0, N_("internal parent kernel device name") },
	[COL_PATH]   = { "PATH",    0.3,  0, N_("path to the device node") },
	[COL_MAJMIN] = { "MAJ:MIN", 6, 0, N_("major:minor device number"), COLTYPE_SORTNUM },

	[COL_FSAVAIL]   = { "FSAVAIL", 5, SCOLS_FL_RIGHT, N_("filesystem size available") },
	[COL_FSSIZE]    = { "FSSIZE", 5, SCOLS_FL_RIGHT, N_("filesystem size") },
	[COL_FSTYPE]    = { "FSTYPE", 0.1, SCOLS_FL_TRUNC, N_("filesystem type") },
	[COL_FSUSED]    = { "FSUSED", 5, SCOLS_FL_RIGHT, N_("filesystem size used") },
	[COL_FSUSEPERC] = { "FSUSE%", 3, SCOLS_FL_RIGHT, N_("filesystem use percentage") },

	[COL_TARGET] = { "MOUNTPOINT", 0.10, SCOLS_FL_TRUNC, N_("where the device is mounted") },
	[COL_LABEL]  = { "LABEL",   0.1, 0, N_("filesystem LABEL") },
	[COL_UUID]   = { "UUID",    36,  0, N_("filesystem UUID") },

	[COL_PTUUID] = { "PTUUID",  36,  0, N_("partition table identifier (usually UUID)") },
	[COL_PTTYPE] = { "PTTYPE",  0.1, 0, N_("partition table type") },

	[COL_PARTTYPE]  = { "PARTTYPE",  36,  0, N_("partition type UUID") },
	[COL_PARTLABEL] = { "PARTLABEL", 0.1, 0, N_("partition LABEL") },
	[COL_PARTUUID]  = { "PARTUUID",  36,  0, N_("partition UUID") },
	[COL_PARTFLAGS] = { "PARTFLAGS",  36,  0, N_("partition flags") },

	[COL_RA]     = { "RA",      3, SCOLS_FL_RIGHT, N_("read-ahead of the device"), COLTYPE_NUM },
	[COL_RO]     = { "RO",      1, SCOLS_FL_RIGHT, N_("read-only device"), COLTYPE_BOOL },
	[COL_RM]     = { "RM",      1, SCOLS_FL_RIGHT, N_("removable device"), COLTYPE_BOOL },
	[COL_HOTPLUG]= { "HOTPLUG", 1, SCOLS_FL_RIGHT, N_("removable or hotplug device (usb, pcmcia, ...)"), COLTYPE_BOOL },
	[COL_ROTA]   = { "ROTA",    1, SCOLS_FL_RIGHT, N_("rotational device"), COLTYPE_BOOL },
	[COL_RAND]   = { "RAND",    1, SCOLS_FL_RIGHT, N_("adds randomness"), COLTYPE_BOOL },
	[COL_MODEL]  = { "MODEL",   0.1, SCOLS_FL_TRUNC, N_("device identifier") },
	[COL_SERIAL] = { "SERIAL",  0.1, SCOLS_FL_TRUNC, N_("disk serial number") },
	[COL_SIZE]   = { "SIZE",    5, SCOLS_FL_RIGHT, N_("size of the device"), COLTYPE_SIZE },
	[COL_STATE]  = { "STATE",   7, SCOLS_FL_TRUNC, N_("state of the device") },
	[COL_OWNER]  = { "OWNER",   0.1, SCOLS_FL_TRUNC, N_("user name"), },
	[COL_GROUP]  = { "GROUP",   0.1, SCOLS_FL_TRUNC, N_("group name") },
	[COL_MODE]   = { "MODE",    10,   0, N_("device node permissions") },
	[COL_ALIOFF] = { "ALIGNMENT", 6, SCOLS_FL_RIGHT, N_("alignment offset"), COLTYPE_NUM },
	[COL_MINIO]  = { "MIN-IO",  6, SCOLS_FL_RIGHT, N_("minimum I/O size"), COLTYPE_NUM },
	[COL_OPTIO]  = { "OPT-IO",  6, SCOLS_FL_RIGHT, N_("optimal I/O size"), COLTYPE_NUM },
	[COL_PHYSEC] = { "PHY-SEC", 7, SCOLS_FL_RIGHT, N_("physical sector size"), COLTYPE_NUM },
	[COL_LOGSEC] = { "LOG-SEC", 7, SCOLS_FL_RIGHT, N_("logical sector size"), COLTYPE_NUM },
	[COL_SCHED]  = { "SCHED",   0.1, 0, N_("I/O scheduler name") },
	[COL_RQ_SIZE]= { "RQ-SIZE", 5, SCOLS_FL_RIGHT, N_("request queue size"), COLTYPE_NUM },
	[COL_TYPE]   = { "TYPE",    4, 0, N_("device type") },
	[COL_DALIGN] = { "DISC-ALN", 6, SCOLS_FL_RIGHT, N_("discard alignment offset"), COLTYPE_NUM },
	[COL_DGRAN]  = { "DISC-GRAN", 6, SCOLS_FL_RIGHT, N_("discard granularity"), COLTYPE_SIZE },
	[COL_DMAX]   = { "DISC-MAX", 6, SCOLS_FL_RIGHT, N_("discard max bytes"), COLTYPE_SIZE },
	[COL_DZERO]  = { "DISC-ZERO", 1, SCOLS_FL_RIGHT, N_("discard zeroes data"), COLTYPE_BOOL },
	[COL_WSAME]  = { "WSAME",   6, SCOLS_FL_RIGHT, N_("write same max bytes"), COLTYPE_SIZE },
	[COL_WWN]    = { "WWN",     18, 0, N_("unique storage identifier") },
	[COL_HCTL]   = { "HCTL", 10, 0, N_("Host:Channel:Target:Lun for SCSI") },
	[COL_TRANSPORT] = { "TRAN", 6, 0, N_("device transport type") },
	[COL_SUBSYS] = { "SUBSYSTEMS", 0.1, SCOLS_FL_NOEXTREMES, N_("de-duplicated chain of subsystems") },
	[COL_REV]    = { "REV",   4, SCOLS_FL_RIGHT, N_("device revision") },
	[COL_VENDOR] = { "VENDOR", 0.1, SCOLS_FL_TRUNC, N_("device vendor") },
	[COL_ZONED]  = { "ZONED", 0.3, 0, N_("zone model") },
};

struct lsblk *lsblk;	/* global handler */

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

static inline void add_uniq_column(int id)
{
	if (column_id_to_number(id) < 0)
		add_column(id);
}

static int excludes[256];
static size_t nexcludes;

static int includes[256];
static size_t nincludes;

static void lsblk_init_debug(void)
{
	__UL_INIT_DEBUG_FROM_ENV(lsblk, LSBLK_DEBUG_, 0, LSBLK_DEBUG);
}

static int is_maj_excluded(int maj)
{
	size_t i;

	assert(ARRAY_SIZE(excludes) > nexcludes);

	if (!nexcludes)
		return 0;	/* filter not enabled, device not excluded */

	for (i = 0; i < nexcludes; i++) {
		if (excludes[i] == maj) {
			DBG(FILTER, ul_debug("exclude: maj=%d", maj));
			return 1;
		}
	}
	return 0;
}

static int is_maj_included(int maj)
{
	size_t i;

	assert(ARRAY_SIZE(includes) > nincludes);

	if (!nincludes)
		return 1;	/* filter not enabled, device is included */

	for (i = 0; i < nincludes; i++) {
		if (includes[i] == maj) {
			DBG(FILTER, ul_debug("include: maj=%d", maj));
			return 1;
		}
	}
	return 0;
}

/* array with IDs of enabled columns */
static int get_column_id(int num)
{
	assert(num >= 0);
	assert((size_t) num < ncolumns);
	assert(columns[num] < (int) ARRAY_SIZE(infos));
	return columns[num];
}

static struct colinfo *get_column_info(int num)
{
	return &infos[ get_column_id(num) ];
}

static int column_name_to_id(const char *name, size_t namesz)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(infos); i++) {
		const char *cn = infos[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}

static int column_id_to_number(int id)
{
	size_t i;

	for (i = 0; i < ncolumns; i++)
		if (columns[i] == id)
			return i;
	return -1;
}

static void reset_blkdev_cxt(struct blkdev_cxt *cxt)
{
	if (!cxt)
		return;

	DBG(CXT, ul_debugobj(cxt, "reset"));

	free(cxt->name);
	free(cxt->dm_name);
	free(cxt->filename);
	free(cxt->mountpoint);

	lsblk_device_free_properties(cxt->properties);
	ul_unref_path(cxt->sysfs);

	memset(cxt, 0, sizeof(*cxt));
}

static int is_dm(const char *name)
{
	return strncmp(name, "dm-", 3) ? 0 : 1;
}

static struct dirent *xreaddir(DIR *dp)
{
	struct dirent *d;

	assert(dp);

	while ((d = readdir(dp))) {
		if (!strcmp(d->d_name, ".") ||
		    !strcmp(d->d_name, ".."))
			continue;

		/* blacklist here? */
		break;
	}
	return d;
}

static char *get_device_path(struct blkdev_cxt *cxt)
{
	char path[PATH_MAX];

	assert(cxt);
	assert(cxt->name);

	if (is_dm(cxt->name))
		return __canonicalize_dm_name(lsblk->sysroot, cxt->name);

	snprintf(path, sizeof(path), "/dev/%s", cxt->name);
	sysfs_devname_sys_to_dev(path);
	return xstrdup(path);
}

static int is_readonly_device(struct blkdev_cxt *cxt)
{
	int fd, ro = 0;

	if (ul_path_scanf(cxt->sysfs, "ro", "%d", &ro) == 1)
		return ro;

	/* fallback if "ro" attribute does not exist */
	fd = open(cxt->filename, O_RDONLY);
	if (fd != -1) {
		if (ioctl(fd, BLKROGET, &ro) != 0)
			ro = 0;
		close(fd);
	}
	return ro;
}

static char *get_scheduler(struct blkdev_cxt *cxt)
{
	char buf[128];
	char *p, *res = NULL;

	if (ul_path_read_buffer(cxt->sysfs, buf, sizeof(buf), "queue/scheduler") == 0)
		return NULL;
	p = strchr(buf, '[');
	if (p) {
		res = p + 1;
		p = strchr(res, ']');
		if (p) {
			*p = '\0';
			res = xstrdup(res);
		} else
			res = NULL;
	}
	return res;
}

static char *get_type(struct blkdev_cxt *cxt)
{
	char *res = NULL, *p;

	if (cxt->partition)
		return xstrdup("part");

	if (is_dm(cxt->name)) {
		char *dm_uuid = NULL;

		/* The DM_UUID prefix should be set to subsystem owning
		 * the device - LVM, CRYPT, DMRAID, MPATH, PART */
		if (ul_path_read_string(cxt->sysfs, &dm_uuid, "dm/uuid") > 0
		    && dm_uuid) {
			char *tmp = dm_uuid;
			char *dm_uuid_prefix = strsep(&tmp, "-");

			if (dm_uuid_prefix) {
				/* kpartx hack to remove partition number */
				if (strncasecmp(dm_uuid_prefix, "part", 4) == 0)
					dm_uuid_prefix[4] = '\0';

				res = xstrdup(dm_uuid_prefix);
			}
		}

		free(dm_uuid);
		if (!res)
			/* No UUID or no prefix - just mark it as DM device */
			res = xstrdup("dm");

	} else if (!strncmp(cxt->name, "loop", 4)) {
		res = xstrdup("loop");

	} else if (!strncmp(cxt->name, "md", 2)) {
		char *md_level = NULL;

		ul_path_read_string(cxt->sysfs, &md_level, "md/level");
		res = md_level ? md_level : xstrdup("md");

	} else {
		const char *type = NULL;
		int x = 0;

		if (ul_path_read_s32(cxt->sysfs, &x, "device/type") == 0)
			type = blkdev_scsi_type_to_name(x);
		if (!type)
			type = "disk";
		res = xstrdup(type);
	}

	for (p = res; p && *p; p++)
		*p = tolower((unsigned char) *p);
	return res;
}

/* Thanks to lsscsi code for idea of detection logic used here */
static char *get_transport(struct blkdev_cxt *cxt)
{
	struct path_cxt *sysfs = cxt->sysfs;
	char *attr = NULL;
	const char *trans = NULL;


	/* SCSI - Serial Peripheral Interface */
	if (sysfs_blkdev_scsi_host_is(sysfs, "spi"))
		trans = "spi";

	/* FC/FCoE - Fibre Channel / Fibre Channel over Ethernet */
	else if (sysfs_blkdev_scsi_host_is(sysfs, "fc")) {
		attr = sysfs_blkdev_scsi_host_strdup_attribute(sysfs, "fc", "symbolic_name");
		if (!attr)
			return NULL;
		trans = strstr(attr, " over ") ? "fcoe" : "fc";
		free(attr);
	}

	/* SAS - Serial Attached SCSI */
	else if (sysfs_blkdev_scsi_host_is(sysfs, "sas") ||
		 sysfs_blkdev_scsi_has_attribute(sysfs, "sas_device"))
		trans = "sas";


	/* SBP - Serial Bus Protocol (FireWire) */
	else if (sysfs_blkdev_scsi_has_attribute(sysfs, "ieee1394_id"))
		trans = "sbp";

	/* iSCSI */
	else if (sysfs_blkdev_scsi_host_is(sysfs, "iscsi"))
		trans ="iscsi";

	/* USB - Universal Serial Bus */
	else if (sysfs_blkdev_scsi_path_contains(sysfs, "usb"))
		trans = "usb";

	/* ATA, SATA */
	else if (sysfs_blkdev_scsi_host_is(sysfs, "scsi")) {
		attr = sysfs_blkdev_scsi_host_strdup_attribute(sysfs, "scsi", "proc_name");
		if (!attr)
			return NULL;
		if (!strncmp(attr, "ahci", 4) || !strncmp(attr, "sata", 4))
			trans = "sata";
		else if (strstr(attr, "ata"))
			trans = "ata";
		free(attr);

	} else if (strncmp(cxt->name, "nvme", 4) == 0)
		trans = "nvme";

	return trans ? xstrdup(trans) : NULL;
}

static char *get_subsystems(struct blkdev_cxt *cxt)
{
	char path[PATH_MAX];
	char *sub, *chain, *res = NULL;
	size_t len = 0, last = 0;

	chain = sysfs_blkdev_get_devchain(cxt->sysfs, path, sizeof(path));
	if (!chain)
		return NULL;

	while (sysfs_blkdev_next_subsystem(cxt->sysfs, chain, &sub) == 0) {
		size_t sz;

		/* don't create "block:scsi:scsi", but "block:scsi" */
		if (len && strcmp(res + last, sub) == 0) {
			free(sub);
			continue;
		}

		sz = strlen(sub);
		res = xrealloc(res, len + sz + 2);
		if (len)
			res[len++] = ':';

		memcpy(res + len, sub, sz + 1);
		last = len;
		len += sz;
		free(sub);
	}

	return res;
}


#define is_parsable(_l)	(scols_table_is_raw((_l)->table) || \
			 scols_table_is_export((_l)->table) || \
			 scols_table_is_json((_l)->table))

static char *mk_name(const char *name)
{
	char *p;
	if (!name)
		return NULL;
	if (lsblk->paths)
		xasprintf(&p, "/dev/%s", name);
	else
		p = xstrdup(name);
	if (p)
		sysfs_devname_sys_to_dev(p);
	return p;
}

static char *mk_dm_name(const char *name)
{
	char *p;
	if (!name)
		return NULL;
	if (lsblk->paths)
		xasprintf(&p, "/dev/mapper/%s", name);
	else
		p = xstrdup(name);
	return p;
}

/* stores data to scols cell userdata (invisible and independent on output)
 * to make the original values accessible for sort functions
 */
static void set_sortdata_u64(struct libscols_line *ln, int col, uint64_t x)
{
	struct libscols_cell *ce = scols_line_get_cell(ln, col);
	uint64_t *data;

	if (!ce)
		return;
	data = xmalloc(sizeof(uint64_t));
	*data = x;
	scols_cell_set_userdata(ce, data);
}

static void set_sortdata_u64_from_string(struct libscols_line *ln, int col, const char *str)
{
	uint64_t x;

	if (!str || sscanf(str, "%"SCNu64, &x) != 1)
		return;

	set_sortdata_u64(ln, col, x);
}

static void unref_sortdata(struct libscols_table *tb)
{
	struct libscols_iter *itr;
	struct libscols_line *ln;

	if (!tb || !lsblk->sort_col)
		return;
	itr = scols_new_iter(SCOLS_ITER_FORWARD);
	if (!itr)
		return;
	while (scols_table_next_line(tb, itr, &ln) == 0) {
		struct libscols_cell *ce = scols_line_get_column_cell(ln,
								lsblk->sort_col);
		void *data = scols_cell_get_userdata(ce);
		free(data);
	}

	scols_free_iter(itr);
}

static char *get_vfs_attribute(struct blkdev_cxt *cxt, int id)
{
	char *sizestr;
	uint64_t vfs_attr = 0;
	char *mnt;

	if (!cxt->fsstat.f_blocks) {
		mnt = lsblk_device_get_mountpoint(cxt);
		if (!mnt || cxt->is_swap)
			return NULL;
		if (statvfs(mnt, &cxt->fsstat) != 0)
			return NULL;
	}

	switch(id) {
	case COL_FSSIZE:
		vfs_attr = cxt->fsstat.f_frsize * cxt->fsstat.f_blocks;
		break;
	case COL_FSAVAIL:
		vfs_attr = cxt->fsstat.f_frsize * cxt->fsstat.f_bavail;
		break;
	case COL_FSUSED:
		vfs_attr = cxt->fsstat.f_frsize * (cxt->fsstat.f_blocks - cxt->fsstat.f_bfree);
		break;
	case COL_FSUSEPERC:
		if (cxt->fsstat.f_blocks == 0)
			return xstrdup("-");

		xasprintf(&sizestr, "%.0f%%",
				(double)(cxt->fsstat.f_blocks - cxt->fsstat.f_bfree) /
				cxt->fsstat.f_blocks * 100);
		return sizestr;
	}

	if (!vfs_attr)
		sizestr = xstrdup("0");
	else if (lsblk->bytes)
		xasprintf(&sizestr, "%ju", vfs_attr);
	else
		sizestr = size_to_human_string(SIZE_SUFFIX_1LETTER, vfs_attr);

	return sizestr;
}

static struct stat *device_get_stat(struct blkdev_cxt *cxt)
{
	if (!cxt->st.st_rdev)
		stat(cxt->filename, &cxt->st);

	return &cxt->st;
}

static void set_scols_data(struct blkdev_cxt *cxt, int col, int id, struct libscols_line *ln)
{
	struct lsblk_devprop *prop;
	int sort = 0;
	char *str = NULL;

	if (lsblk->sort_id == id)
		sort = 1;

	switch(id) {
	case COL_NAME:
		str = cxt->dm_name ? mk_dm_name(cxt->dm_name) :	mk_name(cxt->name);
		break;
	case COL_KNAME:
		str = mk_name(cxt->name);
		break;
	case COL_PKNAME:
		if (cxt->parent)
			str = mk_name(cxt->parent->name);
		break;
	case COL_PATH:
		if (cxt->filename)
			str = xstrdup(cxt->filename);
		break;
	case COL_OWNER:
	{
		struct stat *st = device_get_stat(cxt);
		struct passwd *pw = st ? NULL : getpwuid(st->st_uid);
		if (pw)
			str = xstrdup(pw->pw_name);
		break;
	}
	case COL_GROUP:
	{
		struct stat *st = device_get_stat(cxt);
		struct group *gr = st ? NULL : getgrgid(st->st_gid);
		if (gr)
			str = xstrdup(gr->gr_name);
		break;
	}
	case COL_MODE:
	{
		struct stat *st = device_get_stat(cxt);
		char md[11] = { '\0' };

		if (st)
			str = xstrdup(xstrmode(st->st_mode, md));
		break;
	}
	case COL_MAJMIN:
		if (is_parsable(lsblk))
			xasprintf(&str, "%u:%u", cxt->maj, cxt->min);
		else
			xasprintf(&str, "%3u:%-3u", cxt->maj, cxt->min);
		if (sort)
			set_sortdata_u64(ln, col, makedev(cxt->maj, cxt->min));
		break;
	case COL_FSTYPE:
		prop = lsblk_device_get_properties(cxt);
		if (prop && prop->fstype)
			str = xstrdup(prop->fstype);
		break;
	case COL_FSSIZE:
	case COL_FSAVAIL:
	case COL_FSUSED:
	case COL_FSUSEPERC:
		str = get_vfs_attribute(cxt, id);
		break;
	case COL_TARGET:
		str = xstrdup(lsblk_device_get_mountpoint(cxt));
		break;
	case COL_LABEL:
		prop = lsblk_device_get_properties(cxt);
		if (prop && prop->label)
			str = xstrdup(prop->label);
		break;
	case COL_UUID:
		prop = lsblk_device_get_properties(cxt);
		if (prop && prop->uuid)
			str = xstrdup(prop->uuid);
		break;
	case COL_PTUUID:
		prop = lsblk_device_get_properties(cxt);
		if (prop && prop->ptuuid)
			str = xstrdup(prop->ptuuid);
		break;
	case COL_PTTYPE:
		prop = lsblk_device_get_properties(cxt);
		if (prop && prop->pttype)
			str = xstrdup(prop->pttype);
		break;
	case COL_PARTTYPE:
		prop = lsblk_device_get_properties(cxt);
		if (prop && prop->parttype)
			str = xstrdup(prop->parttype);
		break;
	case COL_PARTLABEL:
		prop = lsblk_device_get_properties(cxt);
		if (prop && prop->partlabel)
			str = xstrdup(prop->partlabel);
		break;
	case COL_PARTUUID:
		prop = lsblk_device_get_properties(cxt);
		if (prop && prop->partuuid)
			str = xstrdup(prop->partuuid);
		break;
	case COL_PARTFLAGS:
		prop = lsblk_device_get_properties(cxt);
		if (prop && prop->partflags)
			str = xstrdup(prop->partflags);
		break;
	case COL_WWN:
		prop = lsblk_device_get_properties(cxt);
		if (prop && prop->wwn)
			str = xstrdup(prop->wwn);
		break;
	case COL_RA:
		ul_path_read_string(cxt->sysfs, &str, "queue/read_ahead_kb");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_RO:
		str = xstrdup(is_readonly_device(cxt) ? "1" : "0");
		break;
	case COL_RM:
		ul_path_read_string(cxt->sysfs, &str, "removable");
		if (!str && sysfs_blkdev_get_parent(cxt->sysfs))
			ul_path_read_string(sysfs_blkdev_get_parent(cxt->sysfs),
					    &str,
					    "removable");
		break;
	case COL_HOTPLUG:
		str = sysfs_blkdev_is_hotpluggable(cxt->sysfs) ? xstrdup("1") : xstrdup("0");
		break;
	case COL_ROTA:
		ul_path_read_string(cxt->sysfs, &str, "queue/rotational");
		break;
	case COL_RAND:
		ul_path_read_string(cxt->sysfs, &str, "queue/add_random");
		break;
	case COL_MODEL:
		if (!cxt->partition && cxt->nslaves == 0) {
			prop = lsblk_device_get_properties(cxt);
			if (prop && prop->model)
				str = xstrdup(prop->model);
			else
				ul_path_read_string(cxt->sysfs, &str, "device/model");
		}
		break;
	case COL_SERIAL:
		if (!cxt->partition && cxt->nslaves == 0) {
			prop = lsblk_device_get_properties(cxt);
			if (prop && prop->serial)
				str = xstrdup(prop->serial);
			else
				ul_path_read_string(cxt->sysfs, &str, "device/serial");
		}
		break;
	case COL_REV:
		if (!cxt->partition && cxt->nslaves == 0)
			ul_path_read_string(cxt->sysfs, &str, "device/rev");
		break;
	case COL_VENDOR:
		if (!cxt->partition && cxt->nslaves == 0)
			ul_path_read_string(cxt->sysfs, &str, "device/vendor");
		break;
	case COL_SIZE:
		if (!cxt->size)
			break;
		if (lsblk->bytes)
			xasprintf(&str, "%ju", cxt->size);
		else
			str = size_to_human_string(SIZE_SUFFIX_1LETTER, cxt->size);
		if (sort)
			set_sortdata_u64(ln, col, cxt->size);
		break;
	case COL_STATE:
		if (!cxt->partition && !cxt->dm_name)
			ul_path_read_string(cxt->sysfs, &str, "device/state");
		else if (cxt->dm_name) {
			int x = 0;
			if (ul_path_read_s32(cxt->sysfs, &x, "dm/suspended") == 0)
				str = xstrdup(x ? "suspended" : "running");
		}
		break;
	case COL_ALIOFF:
		ul_path_read_string(cxt->sysfs, &str, "alignment_offset");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_MINIO:
		ul_path_read_string(cxt->sysfs, &str, "queue/minimum_io_size");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_OPTIO:
		ul_path_read_string(cxt->sysfs, &str, "queue/optimal_io_size");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_PHYSEC:
		ul_path_read_string(cxt->sysfs, &str, "queue/physical_block_size");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_LOGSEC:
		ul_path_read_string(cxt->sysfs, &str, "queue/logical_block_size");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_SCHED:
		str = get_scheduler(cxt);
		break;
	case COL_RQ_SIZE:
		ul_path_read_string(cxt->sysfs, &str, "queue/nr_requests");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_TYPE:
		str = get_type(cxt);
		break;
	case COL_HCTL:
	{
		int h, c, t, l;
		if (sysfs_blkdev_scsi_get_hctl(cxt->sysfs, &h, &c, &t, &l) == 0)
			xasprintf(&str, "%d:%d:%d:%d", h, c, t, l);
		break;
	}
	case COL_TRANSPORT:
		str = get_transport(cxt);
		break;
	case COL_SUBSYS:
		str = get_subsystems(cxt);
		break;
	case COL_DALIGN:
		if (cxt->discard)
			ul_path_read_string(cxt->sysfs, &str, "discard_alignment");
		if (!str)
			str = xstrdup("0");
		if (sort)
			set_sortdata_u64_from_string(ln, col, str);
		break;
	case COL_DGRAN:
		if (lsblk->bytes) {
			ul_path_read_string(cxt->sysfs, &str, "queue/discard_granularity");
			if (sort)
				set_sortdata_u64_from_string(ln, col, str);
		} else {
			uint64_t x;
			if (ul_path_read_u64(cxt->sysfs, &x, "queue/discard_granularity") == 0) {
				str = size_to_human_string(SIZE_SUFFIX_1LETTER, x);
				if (sort)
					set_sortdata_u64(ln, col, x);
			}
		}
		break;
	case COL_DMAX:
		if (lsblk->bytes) {
			ul_path_read_string(cxt->sysfs, &str, "queue/discard_max_bytes");
			if (sort)
				set_sortdata_u64_from_string(ln, col, str);
		} else {
			uint64_t x;
			if (ul_path_read_u64(cxt->sysfs, &x, "queue/discard_max_bytes") == 0) {
				str = size_to_human_string(SIZE_SUFFIX_1LETTER, x);
				if (sort)
					set_sortdata_u64(ln, col, x);
			}
		}
		break;
	case COL_DZERO:
		if (cxt->discard)
			ul_path_read_string(cxt->sysfs, &str, "queue/discard_zeroes_data");
		if (!str)
			str = xstrdup("0");
		break;
	case COL_WSAME:
		if (lsblk->bytes) {
			ul_path_read_string(cxt->sysfs, &str, "queue/write_same_max_bytes");
			if (sort)
				set_sortdata_u64_from_string(ln, col, str);
		} else {
			uint64_t x;

			if (ul_path_read_u64(cxt->sysfs, &x, "queue/write_same_max_bytes") == 0) {
				str = size_to_human_string(SIZE_SUFFIX_1LETTER, x);
				if (sort)
					set_sortdata_u64(ln, col, x);
			}
		}
		if (!str)
			str = xstrdup("0");
		break;
	case COL_ZONED:
		ul_path_read_string(cxt->sysfs, &str, "queue/zoned");
		break;
	};

	if (str && scols_line_refer_data(ln, col, str))
		err(EXIT_FAILURE, _("failed to add output data"));
}

static void fill_table_line(struct blkdev_cxt *cxt, struct libscols_line *scols_parent)
{
	size_t i;

	cxt->scols_line = scols_table_new_line(lsblk->table, scols_parent);
	if (!cxt->scols_line)
		err(EXIT_FAILURE, _("failed to allocate output line"));

	for (i = 0; i < ncolumns; i++)
		set_scols_data(cxt, i, get_column_id(i), cxt->scols_line);
}

static int set_cxt(struct blkdev_cxt *cxt,
		    struct blkdev_cxt *parent,
		    struct blkdev_cxt *wholedisk,
		    const char *name)
{
	dev_t devno;

	DBG(CXT, ul_debugobj(cxt, "setting context for %s [parent=%p, wholedisk=%p]",
				name, parent, wholedisk));

	cxt->parent = parent;
	cxt->name = xstrdup(name);
	cxt->partition = wholedisk != NULL;

	cxt->filename = get_device_path(cxt);
	if (!cxt->filename) {
		DBG(CXT, ul_debugobj(cxt, "%s: failed to get device path", cxt->name));
		return -1;
	}
	DBG(CXT, ul_debugobj(cxt, "%s: filename=%s", cxt->name, cxt->filename));

	devno = __sysfs_devname_to_devno(lsblk->sysroot, cxt->name, wholedisk ? wholedisk->name : NULL);

	if (!devno) {
		DBG(CXT, ul_debugobj(cxt, "%s: unknown device name", cxt->name));
		return -1;
	}

	if (lsblk->inverse) {
		cxt->sysfs = ul_new_sysfs_path(devno, wholedisk ? wholedisk->sysfs : NULL, lsblk->sysroot);
		if (!cxt->sysfs) {
			DBG(CXT, ul_debugobj(cxt, "%s: failed to initialize sysfs handler", cxt->name));
			return -1;
		}
		if (parent)
			sysfs_blkdev_set_parent(parent->sysfs, cxt->sysfs);
	} else {
		cxt->sysfs = ul_new_sysfs_path(devno, parent ? parent->sysfs : NULL, lsblk->sysroot);
		if (!cxt->sysfs) {
			DBG(CXT, ul_debugobj(cxt, "%s: failed to initialize sysfs handler", cxt->name));
			return -1;
		}
	}

	cxt->maj = major(devno);
	cxt->min = minor(devno);
	cxt->size = 0;

	if (ul_path_read_u64(cxt->sysfs, &cxt->size, "size") == 0)	/* in sectors */
		cxt->size <<= 9;					/* in bytes */

	if (ul_path_read_s32(cxt->sysfs, &cxt->discard,
			   "queue/discard_granularity") != 0)
		cxt->discard = 0;

	/* Ignore devices of zero size */
	if (!lsblk->all_devices && cxt->size == 0) {
		DBG(CXT, ul_debugobj(cxt, "zero size device -- ignore"));
		return -1;
	}
	if (is_dm(cxt->name)) {
		ul_path_read_string(cxt->sysfs, &cxt->dm_name, "dm/name");
		if (!cxt->dm_name) {
			DBG(CXT, ul_debugobj(cxt, "%s: failed to get dm name", cxt->name));
			return -1;
		}
	}

	cxt->npartitions = sysfs_blkdev_count_partitions(cxt->sysfs, cxt->name);
	cxt->nholders = ul_path_count_dirents(cxt->sysfs, "holders");
	cxt->nslaves = ul_path_count_dirents(cxt->sysfs, "slaves");

	DBG(CXT, ul_debugobj(cxt, "%s: npartitions=%d, nholders=%d, nslaves=%d",
			cxt->name, cxt->npartitions, cxt->nholders, cxt->nslaves));

	/* ignore non-SCSI devices */
	if (lsblk->scsi && sysfs_blkdev_scsi_get_hctl(cxt->sysfs, NULL, NULL, NULL, NULL)) {
		DBG(CXT, ul_debugobj(cxt, "non-scsi device -- ignore"));
		return -1;
	}

	DBG(CXT, ul_debugobj(cxt, "%s: context successfully initialized", cxt->name));
	return 0;
}

static int process_blkdev(struct blkdev_cxt *cxt, struct blkdev_cxt *parent,
			  int do_partitions, const char *part_name);

/*
 * List device partitions if any.
 */
static int list_partitions(struct blkdev_cxt *wholedisk_cxt, struct blkdev_cxt *parent_cxt,
			   const char *part_name)
{
	DIR *dir;
	struct dirent *d;
	struct blkdev_cxt part_cxt = { NULL };
	int r = -1;

	assert(wholedisk_cxt);

	/*
	 * Do not process further if there are no partitions for
	 * this device or the device itself is a partition.
	 */
	if (!wholedisk_cxt->npartitions || wholedisk_cxt->partition)
		return -1;

	DBG(CXT, ul_debugobj(wholedisk_cxt, "probe whole-disk for partitions"));

	dir = ul_path_opendir(wholedisk_cxt->sysfs, NULL);
	if (!dir)
		err(EXIT_FAILURE, _("failed to open device directory in sysfs"));

	while ((d = xreaddir(dir))) {
		/* Process particular partition only? */
		if (part_name && strcmp(part_name, d->d_name))
			continue;

		if (!(sysfs_blkdev_is_partition_dirent(dir, d, wholedisk_cxt->name)))
			continue;

		DBG(CXT, ul_debugobj(wholedisk_cxt, "  checking %s", d->d_name));

		if (lsblk->inverse) {
			/*
			 * <parent_cxt>
			 * `-<part_cxt>
			 *   `-<wholedisk_cxt>
			 *    `-...
			 */
			if (set_cxt(&part_cxt, parent_cxt, wholedisk_cxt, d->d_name))
				goto next;

			if (!parent_cxt && part_cxt.nholders)
				goto next;

			wholedisk_cxt->parent = &part_cxt;
			fill_table_line(&part_cxt, parent_cxt ? parent_cxt->scols_line : NULL);
			if (!lsblk->nodeps)
				process_blkdev(wholedisk_cxt, &part_cxt, 0, NULL);
		} else {
			/*
			 * <parent_cxt>
			 * `-<wholedisk_cxt>
			 *   `-<part_cxt>
			 *    `-...
			 */
			int ps = set_cxt(&part_cxt, wholedisk_cxt, wholedisk_cxt, d->d_name);

			/* Print whole disk only once */
			if (r)
				fill_table_line(wholedisk_cxt, parent_cxt ? parent_cxt->scols_line : NULL);
			if (ps == 0 && !lsblk->nodeps)
				process_blkdev(&part_cxt, wholedisk_cxt, 0, NULL);
		}
	next:
		reset_blkdev_cxt(&part_cxt);
		r = 0;
	}

	DBG(CXT, ul_debugobj(wholedisk_cxt, "probe whole-disk for partitions -- done"));
	closedir(dir);
	return r;
}

static int get_wholedisk_from_partition_dirent(DIR *dir,
				struct dirent *d, struct blkdev_cxt *cxt)
{
	char path[PATH_MAX];
	char *p;
	int len;

	if ((len = readlinkat(dirfd(dir), d->d_name, path, sizeof(path) - 1)) < 0)
		return 0;

	path[len] = '\0';

	/* The path ends with ".../<device>/<partition>" */
	p = strrchr(path, '/');
	if (!p)
		return 0;
	*p = '\0';

	p = strrchr(path, '/');
	if (!p)
		return 0;
	p++;

	return set_cxt(cxt, NULL, NULL, p);
}

/*
 * List device dependencies: partitions, holders (inverse = 0) or slaves (inverse = 1).
 */
static int list_deps(struct blkdev_cxt *cxt)
{
	DIR *dir;
	struct dirent *d;
	struct blkdev_cxt dep = { NULL };
	const char *depname;

	assert(cxt);

	if (lsblk->nodeps)
		return 0;

	DBG(CXT, ul_debugobj(cxt, "%s: list dependencies", cxt->name));

	if (!(lsblk->inverse ? cxt->nslaves : cxt->nholders))
		return 0;

	depname = lsblk->inverse ? "slaves" : "holders";
	dir = ul_path_opendir(cxt->sysfs, depname);
	if (!dir)
		return 0;

	DBG(CXT, ul_debugobj(cxt, "%s: checking for '%s' dependence", cxt->name, depname));

	while ((d = xreaddir(dir))) {
		/* Is the dependency a partition? */
		if (sysfs_blkdev_is_partition_dirent(dir, d, NULL)) {
			if (!get_wholedisk_from_partition_dirent(dir, d, &dep)) {
				DBG(CXT, ul_debugobj(cxt, "%s: %s: dependence is partition",
								cxt->name, d->d_name));
				process_blkdev(&dep, cxt, 1, d->d_name);
			}
		}
		/* The dependency is a whole device. */
		else if (!set_cxt(&dep, cxt, NULL, d->d_name)) {
			DBG(CXT, ul_debugobj(cxt, "%s: %s: dependence is whole-disk",
								cxt->name, d->d_name));
			/* For inverse tree we don't want to show partitions
			 * if the dependence is on whole-disk */
			process_blkdev(&dep, cxt, lsblk->inverse ? 0 : 1, NULL);
		}
		reset_blkdev_cxt(&dep);
	}
	closedir(dir);

	DBG(CXT, ul_debugobj(cxt, "%s: checking for '%s' -- done", cxt->name, depname));
	return 0;
}

static int process_blkdev(struct blkdev_cxt *cxt, struct blkdev_cxt *parent,
			  int do_partitions, const char *part_name)
{
	if (do_partitions && cxt->npartitions)
		list_partitions(cxt, parent, part_name);		/* partitions + whole-disk */
	else
		fill_table_line(cxt, parent ? parent->scols_line : NULL); /* whole-disk only */

	return list_deps(cxt);
}

/* Iterate devices in sysfs */
static int iterate_block_devices(void)
{
	DIR *dir;
	struct dirent *d;
	struct blkdev_cxt cxt = { NULL };
	struct path_cxt *pc = ul_new_path(_PATH_SYS_BLOCK);

	if (!pc)
		err(EXIT_FAILURE, _("failed to allocate /sys handler"));

	ul_path_set_prefix(pc, lsblk->sysroot);

	/* TODO: reuse @pc in set_cxt(), etc. */
	dir = ul_path_opendir(pc, NULL);
	if (!dir)
		goto done;

	DBG(DEV, ul_debug("iterate on " _PATH_SYS_BLOCK));

	while ((d = xreaddir(dir))) {

		DBG(DEV, ul_debug(" %s dentry", d->d_name));

		if (set_cxt(&cxt, NULL, NULL, d->d_name))
			goto next;

		if (is_maj_excluded(cxt.maj) || !is_maj_included(cxt.maj))
			goto next;

		/* Skip devices in the middle of dependency tree. */
		if ((lsblk->inverse ? cxt.nholders : cxt.nslaves) > 0)
			goto next;

		process_blkdev(&cxt, NULL, 1, NULL);
	next:
		reset_blkdev_cxt(&cxt);
	}

	closedir(dir);
done:
	ul_unref_path(pc);
	DBG(DEV, ul_debug("iterate on " _PATH_SYS_BLOCK " -- done"));
	return 0;
}

static int process_one_device(char *devname)
{
	struct blkdev_cxt parent = { NULL }, cxt = { NULL };
	struct stat st;
	char buf[PATH_MAX + 1], *name = NULL, *diskname = NULL;
	dev_t disk = 0;
	int real_part = 0, rc = -EINVAL;

	if (stat(devname, &st) || !S_ISBLK(st.st_mode)) {
		warnx(_("%s: not a block device"), devname);
		goto leave;
	}

	/* TODO: sysfs_devno_to_devname() internally initializes path_cxt, it
	 * would be better to use ul_new_sysfs_path() + sysfs_blkdev_get_name()
	 * and reuse path_cxt for set_cxt()
	 */
	name = sysfs_devno_to_devname(st.st_rdev, buf, sizeof(buf));
	if (!name) {
		warn(_("%s: failed to get sysfs name"), devname);
		goto leave;
	}
	name = xstrdup(name);

	if (!strncmp(name, "dm-", 3)) {
		/* dm mapping is never a real partition! */
		real_part = 0;
	} else {
		if (blkid_devno_to_wholedisk(st.st_rdev, buf, sizeof(buf), &disk)) {
			warn(_("%s: failed to get whole-disk device number"), devname);
			goto leave;
		}
		diskname = buf;
		real_part = st.st_rdev != disk;
	}

	if (!real_part) {
		/*
		 * Device is not a partition.
		 */
		if (set_cxt(&cxt, NULL, NULL, name))
			goto leave;
		process_blkdev(&cxt, NULL, !lsblk->inverse, NULL);
	} else {
		/*
		 * Partition, read sysfs name of the device.
		 */
		if (set_cxt(&parent, NULL, NULL, diskname))
			goto leave;
		if (set_cxt(&cxt, &parent, &parent, name))
			goto leave;

		if (lsblk->inverse)
			process_blkdev(&parent, &cxt, 1, cxt.name);
		else
			process_blkdev(&cxt, &parent, 1, NULL);
	}

	rc = 0;
leave:
	free(name);
	reset_blkdev_cxt(&cxt);

	if (real_part)
		reset_blkdev_cxt(&parent);

	return rc;
}

static void parse_excludes(const char *str0)
{
	const char *str = str0;

	while (str && *str) {
		char *end = NULL;
		unsigned long n;

		errno = 0;
		n = strtoul(str, &end, 10);

		if (end == str || (end && *end && *end != ','))
			errx(EXIT_FAILURE, _("failed to parse list '%s'"), str0);
		if (errno != 0 && (n == ULONG_MAX || n == 0))
			err(EXIT_FAILURE, _("failed to parse list '%s'"), str0);
		excludes[nexcludes++] = n;

		if (nexcludes == ARRAY_SIZE(excludes))
			/* TRANSLATORS: The standard value for %d is 256. */
			errx(EXIT_FAILURE, _("the list of excluded devices is "
					"too large (limit is %d devices)"),
					(int)ARRAY_SIZE(excludes));

		str = end && *end ? end + 1 : NULL;
	}
}

static void parse_includes(const char *str0)
{
	const char *str = str0;

	while (str && *str) {
		char *end = NULL;
		unsigned long n;

		errno = 0;
		n = strtoul(str, &end, 10);

		if (end == str || (end && *end && *end != ','))
			errx(EXIT_FAILURE, _("failed to parse list '%s'"), str0);
		if (errno != 0 && (n == ULONG_MAX || n == 0))
			err(EXIT_FAILURE, _("failed to parse list '%s'"), str0);
		includes[nincludes++] = n;

		if (nincludes == ARRAY_SIZE(includes))
			/* TRANSLATORS: The standard value for %d is 256. */
			errx(EXIT_FAILURE, _("the list of included devices is "
					"too large (limit is %d devices)"),
					(int)ARRAY_SIZE(includes));
		str = end && *end ? end + 1 : NULL;
	}
}

/*
 * see set_sortdata_u64() and columns initialization in main()
 */
static int cmp_u64_cells(struct libscols_cell *a,
			 struct libscols_cell *b,
			 __attribute__((__unused__)) void *data)
{
	uint64_t *adata = (uint64_t *) scols_cell_get_userdata(a),
		 *bdata = (uint64_t *) scols_cell_get_userdata(b);

	if (adata == NULL && bdata == NULL)
		return 0;
	if (adata == NULL)
		return -1;
	if (bdata == NULL)
		return 1;
	return *adata == *bdata ? 0 : *adata >= *bdata ? 1 : -1;
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	size_t i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] [<device> ...]\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("List information about block devices.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --all            print all devices\n"), out);
	fputs(_(" -b, --bytes          print SIZE in bytes rather than in human readable format\n"), out);
	fputs(_(" -d, --nodeps         don't print slaves or holders\n"), out);
	fputs(_(" -D, --discard        print discard capabilities\n"), out);
	fputs(_(" -z, --zoned          print zone model\n"), out);
	fputs(_(" -e, --exclude <list> exclude devices by major number (default: RAM disks)\n"), out);
	fputs(_(" -f, --fs             output info about filesystems\n"), out);
	fputs(_(" -i, --ascii          use ascii characters only\n"), out);
	fputs(_(" -I, --include <list> show only devices with specified major numbers\n"), out);
	fputs(_(" -J, --json           use JSON output format\n"), out);
	fputs(_(" -l, --list           use list format output\n"), out);
	fputs(_(" -T, --tree           use tree format output\n"), out);
	fputs(_(" -m, --perms          output info about permissions\n"), out);
	fputs(_(" -n, --noheadings     don't print headings\n"), out);
	fputs(_(" -o, --output <list>  output columns\n"), out);
	fputs(_(" -O, --output-all     output all columns\n"), out);
	fputs(_(" -p, --paths          print complete device path\n"), out);
	fputs(_(" -P, --pairs          use key=\"value\" output format\n"), out);
	fputs(_(" -r, --raw            use raw output format\n"), out);
	fputs(_(" -s, --inverse        inverse dependencies\n"), out);
	fputs(_(" -S, --scsi           output info about SCSI devices\n"), out);
	fputs(_(" -t, --topology       output info about topology\n"), out);
	fputs(_(" -x, --sort <column>  sort output by <column>\n"), out);
	fputs(_("     --sysroot <dir>  use specified directory as system root\n"), out);
	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(22));

	fprintf(out, USAGE_COLUMNS);

	for (i = 0; i < ARRAY_SIZE(infos); i++)
		fprintf(out, " %11s  %s\n", infos[i].name, _(infos[i].help));

	printf(USAGE_MAN_TAIL("lsblk(8)"));

	exit(EXIT_SUCCESS);
}

static void check_sysdevblock(void)
{
	if (access(_PATH_SYS_DEVBLOCK, R_OK) != 0)
		err(EXIT_FAILURE, _("failed to access sysfs directory: %s"),
		    _PATH_SYS_DEVBLOCK);
}

int main(int argc, char *argv[])
{
	struct lsblk _ls = { .sort_id = -1, .flags = LSBLK_TREE };
	int c, status = EXIT_FAILURE;
	char *outarg = NULL;
	size_t i;
	int force_tree = 0;

	enum {
		OPT_SYSROOT = CHAR_MAX + 1
	};

	static const struct option longopts[] = {
		{ "all",	no_argument,       NULL, 'a' },
		{ "bytes",      no_argument,       NULL, 'b' },
		{ "nodeps",     no_argument,       NULL, 'd' },
		{ "discard",    no_argument,       NULL, 'D' },
		{ "zoned",      no_argument,       NULL, 'z' },
		{ "help",	no_argument,       NULL, 'h' },
		{ "json",       no_argument,       NULL, 'J' },
		{ "output",     required_argument, NULL, 'o' },
		{ "output-all", no_argument,       NULL, 'O' },
		{ "perms",      no_argument,       NULL, 'm' },
		{ "noheadings",	no_argument,       NULL, 'n' },
		{ "list",       no_argument,       NULL, 'l' },
		{ "ascii",	no_argument,       NULL, 'i' },
		{ "raw",        no_argument,       NULL, 'r' },
		{ "inverse",	no_argument,       NULL, 's' },
		{ "fs",         no_argument,       NULL, 'f' },
		{ "exclude",    required_argument, NULL, 'e' },
		{ "include",    required_argument, NULL, 'I' },
		{ "topology",   no_argument,       NULL, 't' },
		{ "paths",      no_argument,       NULL, 'p' },
		{ "pairs",      no_argument,       NULL, 'P' },
		{ "scsi",       no_argument,       NULL, 'S' },
		{ "sort",	required_argument, NULL, 'x' },
		{ "sysroot",    required_argument, NULL, OPT_SYSROOT },
		{ "tree",       no_argument,       NULL, 'T' },
		{ "version",    no_argument,       NULL, 'V' },
		{ NULL, 0, NULL, 0 },
	};

	static const ul_excl_t excl[] = {       /* rows and cols in ASCII order */
		{ 'D','O' },
		{ 'I','e' },
		{ 'J', 'P', 'r' },
		{ 'O','S' },
		{ 'O','f' },
		{ 'O','m' },
		{ 'O','t' },
		{ 'P','T', 'l','r' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	lsblk = &_ls;

	lsblk_init_debug();

	while((c = getopt_long(argc, argv,
			       "abdDze:fhJlnmo:OpPiI:rstVSTx:", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch(c) {
		case 'a':
			lsblk->all_devices = 1;
			break;
		case 'b':
			lsblk->bytes = 1;
			break;
		case 'd':
			lsblk->nodeps = 1;
			break;
		case 'D':
			add_uniq_column(COL_NAME);
			add_uniq_column(COL_DALIGN);
			add_uniq_column(COL_DGRAN);
			add_uniq_column(COL_DMAX);
			add_uniq_column(COL_DZERO);
			break;
		case 'z':
			add_uniq_column(COL_NAME);
			add_uniq_column(COL_ZONED);
			break;
		case 'e':
			parse_excludes(optarg);
			break;
		case 'h':
			usage();
			break;
		case 'J':
			lsblk->flags |= LSBLK_JSON;
			break;
		case 'l':
			lsblk->flags &= ~LSBLK_TREE; /* disable the default */
			break;
		case 'n':
			lsblk->flags |= LSBLK_NOHEADINGS;
			break;
		case 'o':
			outarg = optarg;
			break;
		case 'O':
			for (ncolumns = 0 ; ncolumns < ARRAY_SIZE(infos); ncolumns++)
				columns[ncolumns] = ncolumns;
			break;
		case 'p':
			lsblk->paths = 1;
			break;
		case 'P':
			lsblk->flags |= LSBLK_EXPORT;
			lsblk->flags &= ~LSBLK_TREE;	/* disable the default */
			break;
		case 'i':
			lsblk->flags |= LSBLK_ASCII;
			break;
		case 'I':
			parse_includes(optarg);
			break;
		case 'r':
			lsblk->flags &= ~LSBLK_TREE;	/* disable the default */
			lsblk->flags |= LSBLK_RAW;		/* enable raw */
			break;
		case 's':
			lsblk->inverse = 1;
			break;
		case 'f':
			add_uniq_column(COL_NAME);
			add_uniq_column(COL_FSTYPE);
			add_uniq_column(COL_LABEL);
			add_uniq_column(COL_UUID);
			add_uniq_column(COL_FSAVAIL);
			add_uniq_column(COL_FSUSEPERC);
			add_uniq_column(COL_TARGET);
			break;
		case 'm':
			add_uniq_column(COL_NAME);
			add_uniq_column(COL_SIZE);
			add_uniq_column(COL_OWNER);
			add_uniq_column(COL_GROUP);
			add_uniq_column(COL_MODE);
			break;
		case 't':
			add_uniq_column(COL_NAME);
			add_uniq_column(COL_ALIOFF);
			add_uniq_column(COL_MINIO);
			add_uniq_column(COL_OPTIO);
			add_uniq_column(COL_PHYSEC);
			add_uniq_column(COL_LOGSEC);
			add_uniq_column(COL_ROTA);
			add_uniq_column(COL_SCHED);
			add_uniq_column(COL_RQ_SIZE);
			add_uniq_column(COL_RA);
			add_uniq_column(COL_WSAME);
			break;
		case 'S':
			lsblk->nodeps = 1;
			lsblk->scsi = 1;
			add_uniq_column(COL_NAME);
			add_uniq_column(COL_HCTL);
			add_uniq_column(COL_TYPE);
			add_uniq_column(COL_VENDOR);
			add_uniq_column(COL_MODEL);
			add_uniq_column(COL_REV);
			add_uniq_column(COL_TRANSPORT);
			break;
		case 'T':
			force_tree = 1;
			break;

		case OPT_SYSROOT:
			lsblk->sysroot = optarg;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'x':
			lsblk->flags &= ~LSBLK_TREE; /* disable the default */
			lsblk->sort_id = column_name_to_id(optarg, strlen(optarg));
			if (lsblk->sort_id >= 0)
				break;
			/* fallthrough */
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (force_tree)
		lsblk->flags |= LSBLK_TREE;

	check_sysdevblock();

	if (!ncolumns) {
		add_column(COL_NAME);
		add_column(COL_MAJMIN);
		add_column(COL_RM);
		add_column(COL_SIZE);
		add_column(COL_RO);
		add_column(COL_TYPE);
		add_column(COL_TARGET);
	}

	if (outarg && string_add_to_idarray(outarg, columns, ARRAY_SIZE(columns),
					 &ncolumns, column_name_to_id) < 0)
		return EXIT_FAILURE;

	if (lsblk->all_devices == 0 && nexcludes == 0 && nincludes == 0)
		excludes[nexcludes++] = 1;	/* default: ignore RAM disks */

	if (lsblk->sort_id < 0)
		/* Since Linux 4.8 we have sort devices by default, because
		 * /sys is no more sorted */
		lsblk->sort_id = COL_MAJMIN;

	/* For --inverse --list we still follow parent->child relation */
	if (lsblk->inverse && !(lsblk->flags & LSBLK_TREE))
		lsblk->force_tree_order = 1;

	if (lsblk->sort_id >= 0 && column_id_to_number(lsblk->sort_id) < 0) {
		/* the sort column is not between output columns -- add as hidden */
		add_column(lsblk->sort_id);
		lsblk->sort_hidden = 1;
	}

	lsblk_mnt_init();
	scols_init_debug(0);
	ul_path_init_debug();

	/*
	 * initialize output columns
	 */
	if (!(lsblk->table = scols_new_table()))
		errx(EXIT_FAILURE, _("failed to allocate output table"));
	scols_table_enable_raw(lsblk->table, !!(lsblk->flags & LSBLK_RAW));
	scols_table_enable_export(lsblk->table, !!(lsblk->flags & LSBLK_EXPORT));
	scols_table_enable_ascii(lsblk->table, !!(lsblk->flags & LSBLK_ASCII));
	scols_table_enable_json(lsblk->table, !!(lsblk->flags & LSBLK_JSON));
	scols_table_enable_noheadings(lsblk->table, !!(lsblk->flags & LSBLK_NOHEADINGS));

	if (lsblk->flags & LSBLK_JSON)
		scols_table_set_name(lsblk->table, "blockdevices");

	for (i = 0; i < ncolumns; i++) {
		struct colinfo *ci = get_column_info(i);
		struct libscols_column *cl;
		int id = get_column_id(i), fl = ci->flags;

		if (!(lsblk->flags & LSBLK_TREE) && id == COL_NAME)
			fl &= ~SCOLS_FL_TREE;
		if (lsblk->sort_hidden && lsblk->sort_id == id)
			fl |= SCOLS_FL_HIDDEN;

		cl = scols_table_new_column(lsblk->table, ci->name, ci->whint, fl);
		if (!cl) {
			warn(_("failed to allocate output column"));
			goto leave;
		}
		if (!lsblk->sort_col && lsblk->sort_id == id) {
			lsblk->sort_col = cl;
			scols_column_set_cmpfunc(cl,
				ci->type == COLTYPE_NUM     ? cmp_u64_cells :
				ci->type == COLTYPE_SIZE    ? cmp_u64_cells :
			        ci->type == COLTYPE_SORTNUM ? cmp_u64_cells : scols_cmpstr_cells,
				NULL);
		}
		if (lsblk->flags & LSBLK_JSON) {
			switch (ci->type) {
			case COLTYPE_SIZE:
				if (!lsblk->bytes)
					break;
				/* fallthrough */
			case COLTYPE_NUM:
				 scols_column_set_json_type(cl, SCOLS_JSON_NUMBER);
				break;
			case COLTYPE_BOOL:
				scols_column_set_json_type(cl, SCOLS_JSON_BOOLEAN);
				break;
			default:
				scols_column_set_json_type(cl, SCOLS_JSON_STRING);
				break;
			}
		}
	}

	if (optind == argc)
		status = iterate_block_devices() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	else {
		int cnt = 0, cnt_err = 0;

		while (optind < argc) {
			if (process_one_device(argv[optind++]) != 0)
				cnt_err++;
			cnt++;
		}
		status = cnt == 0	? EXIT_FAILURE :	/* nothing */
			 cnt == cnt_err	? LSBLK_EXIT_ALLFAILED :/* all failed */
			 cnt_err	? LSBLK_EXIT_SOMEOK :	/* some ok */
					  EXIT_SUCCESS;		/* all success */
	}

	if (lsblk->sort_col)
		scols_sort_table(lsblk->table, lsblk->sort_col);
	if (lsblk->force_tree_order)
		scols_sort_table_by_tree(lsblk->table);

	scols_print_table(lsblk->table);

leave:
	if (lsblk->sort_col)
		unref_sortdata(lsblk->table);

	scols_unref_table(lsblk->table);

	lsblk_mnt_deinit();
	lsblk_properties_deinit();

	return status;
}
