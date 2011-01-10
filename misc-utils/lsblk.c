/*
 * lsblk(8) - list block devices
 *
 * Copyright (C) 2010 Red Hat, Inc. All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <err.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <locale.h>
#include <pwd.h>
#include <grp.h>

#include <blkid.h>

#include <assert.h>

#include "pathnames.h"
#include "blkdev.h"
#include "canonicalize.h"
#include "ismounted.h"
#include "nls.h"
#include "tt.h"
#include "xalloc.h"
#include "strutils.h"

/* column IDs */
enum {
	COL_NAME = 0,
	COL_KNAME,
	COL_MAJMIN,
	COL_FSTYPE,
	COL_TARGET,
	COL_LABEL,
	COL_UUID,
	COL_RO,
	COL_RM,
	COL_MODEL,
	COL_SIZE,
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

	__NCOLUMNS
};

/* column names */
struct colinfo {
	const char	*name;		/* header */
	double		whint;		/* width hint (N < 1 is in percent of termwidth) */
	int		flags;		/* TT_FL_* */
	const char      *help;
};

/* columns descriptions */
static struct colinfo infos[__NCOLUMNS] = {
	[COL_NAME]   = { "NAME",    0.25, TT_FL_TREE, N_("device name") },
	[COL_KNAME]  = { "KNAME",   0.3, 0, N_("internel kernel device name") },
	[COL_MAJMIN] = { "MAJ:MIN", 6, 0, N_("major:minor device number") },
	[COL_FSTYPE] = { "FSTYPE",  0.1, TT_FL_TRUNC, N_("filesystem type") },
	[COL_TARGET] = { "MOUNTPOINT", 0.10, TT_FL_TRUNC, N_("where the device is mounted") },
	[COL_LABEL]  = { "LABEL",   0.1, 0, N_("filesystem LABEL") },
	[COL_UUID]   = { "UUID",    36,  0, N_("filesystem UUID") },
	[COL_RO]     = { "RO",      1, TT_FL_RIGHT, N_("read-only device") },
	[COL_RM]     = { "RM",      1, TT_FL_RIGHT, N_("removable device") },
	[COL_ROTA]   = { "ROTA",    1, TT_FL_RIGHT, N_("rotational device") },
	[COL_MODEL]  = { "MODEL",   0.1, TT_FL_TRUNC, N_("device identifier") },
	[COL_SIZE]   = { "SIZE",    6, TT_FL_RIGHT, N_("size of the device") },
	[COL_OWNER]  = { "OWNER",   0.1, TT_FL_TRUNC, N_("user name"), },
	[COL_GROUP]  = { "GROUP",   0.1, TT_FL_TRUNC, N_("group name") },
	[COL_MODE]   = { "MODE",    10,   0, N_("device node permissions") },
	[COL_ALIOFF] = { "ALIGNMENT", 6, TT_FL_RIGHT, N_("alignment offset") },
	[COL_MINIO]  = { "MIN-IO",  6, TT_FL_RIGHT, N_("minimum I/O size") },
	[COL_OPTIO]  = { "OPT-IO",  6, TT_FL_RIGHT, N_("optimal I/O size") },
	[COL_PHYSEC] = { "PHY-SEC", 7, TT_FL_RIGHT, N_("physical sector size") },
	[COL_LOGSEC] = { "LOG-SEC", 7, TT_FL_RIGHT, N_("logical sector size") },
	[COL_SCHED]  = { "SCHED",   0.1, 0, N_("I/O scheduler name") }

};

struct lsblk {
	struct tt *tt;		/* output table */
	int all_devices:1;	/* print all devices */
	int bytes:1;		/* print SIZE in bytes */
	int nodeps:1;		/* don't print slaves/holders */
};

struct lsblk *lsblk;	/* global handler */
int columns[__NCOLUMNS];/* enabled columns */
int ncolumns;		/* number of enabled columns */

unsigned int excludes[256];
int nexcludes;

struct blkdev_cxt {
	struct blkdev_cxt *parent;

	struct tt_line *tt_line;
	struct stat	st;

	char *name;		/* kernel name in /sys/block */
	char *dm_name;		/* DM name (dm/block) */

	char *filename;		/* path to device node */
	int sysfs_fd;		/* O_RDONLY file desciptor to /sys/block/<dev> */

	int partition;		/* is partition? TRUE/FALSE */

	int probed;		/* already probed */
	char *fstype;		/* detected fs, NULL or "?" if cannot detect */
	char *uuid;		/* UUID of device / filesystem */
	char *label;		/* FS label */

	int nholders;		/* # of devices mapped directly to this device
				 * /sys/block/.../holders + number of partition */
	int nslaves;		/* # of devices this device maps to */
	int maj, min;		/* devno */

	uint64_t size;		/* device size */
};

static int is_maj_excluded(int maj)
{
	int i;

	assert(ARRAY_SIZE(excludes) > nexcludes);

	for (i = 0; i < nexcludes; i++)
		if (excludes[i] == maj)
			return 1;
	return 0;
}


/* array with IDs of enabled columns */
static int get_column_id(int num)
{
	assert(ARRAY_SIZE(columns) == __NCOLUMNS);
	assert(num < ncolumns);
	assert(columns[num] < __NCOLUMNS);
	return columns[num];
}

static struct colinfo *get_column_info(int num)
{
	return &infos[ get_column_id(num) ];
}


static int column_name_to_id(const char *name, size_t namesz)
{
	int i;

	for (i = 0; i < __NCOLUMNS; i++) {
		const char *cn = infos[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}

static void reset_blkdev_cxt(struct blkdev_cxt *cxt)
{
	if (!cxt)
		return;
	free(cxt->name);
	free(cxt->dm_name);
	free(cxt->filename);
	free(cxt->fstype);
	free(cxt->uuid);
	free(cxt->label);

	if (cxt->sysfs_fd >= 0)
		close(cxt->sysfs_fd);

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


static int is_partition_dirent(DIR *dir, struct dirent *d, const char *parent_name)
{
	char path[256];

	assert(dir);
	assert(d);

#ifdef _DIRENT_HAVE_D_TYPE
	if (d->d_type != DT_DIR)
		return 0;
#endif
	if (strncmp(parent_name, d->d_name, strlen(parent_name)))
		return 0;

	/* Cannot use /partition file, not supported on old sysfs */
	snprintf(path, sizeof(path), "%s/start", d->d_name);

	return faccessat(dirfd(dir), path, R_OK, 0) == 0;
}

static char *get_device_path(struct blkdev_cxt *cxt)
{
	char path[PATH_MAX];

	assert(cxt);
	assert(cxt->name);

	if (is_dm(cxt->name))
		return canonicalize_dm_name(cxt->name);

	snprintf(path, sizeof(path), "/dev/%s", cxt->name);
	return xstrdup(path);
}

static char *get_sysfs_path(struct blkdev_cxt *cxt)
{
	char path[PATH_MAX];

	assert(cxt);
	assert(cxt->name);

	if (cxt->partition && cxt->parent)
		snprintf(path, sizeof(path), _PATH_SYS_BLOCK "/%s/%s",
			 cxt->parent->name, cxt->name);
	else
		snprintf(path, sizeof(path), _PATH_SYS_BLOCK "/%s", cxt->name);

	return xstrdup(path);
}

static int sysfs_open(struct blkdev_cxt *cxt, const char *attr)
{
	int fd;

	assert(cxt);
	assert(cxt->sysfs_fd >= 0);

	fd = openat(cxt->sysfs_fd, attr, O_RDONLY);
	if (fd == -1 && errno == ENOENT && !strncmp(attr, "queue/", 6) && cxt->parent) {
		fd = openat(cxt->parent->sysfs_fd, attr, O_RDONLY);
	}
	return fd;
}

static FILE *sysfs_fopen(struct blkdev_cxt *cxt, const char *attr)
{
	int fd = sysfs_open(cxt, attr);

	return fd < 0 ? NULL : fdopen(fd, "r");
}

static DIR *sysfs_opendir(struct blkdev_cxt *cxt, const char *attr)
{
	DIR *dir;
	int fd;

	if (attr)
		fd = sysfs_open(cxt, attr);
	else {
		/* request to open root of device in sysfs (/sys/block/<dev>)
		 * -- we cannot use cxt->sysfs_fd directly, because closedir()
		 * will close this our persistent file descriptor.
		 */
		assert(cxt);
		assert(cxt->sysfs_fd >= 0);

		fd = dup(cxt->sysfs_fd);
	}

	if (fd < 0)
		return NULL;
	dir = fdopendir(fd);
	if (!dir) {
		close(fd);
		return NULL;
	}
	if (!attr)
		 rewinddir(dir);
	return dir;
}

static __attribute__ ((format (scanf, 3, 4)))
int sysfs_scanf(struct blkdev_cxt *cxt,  const char *attr, const char *fmt, ...)
{
	FILE *f = sysfs_fopen(cxt, attr);
	va_list ap;
	int rc;

	if (!f)
		return -EINVAL;
	va_start(ap, fmt);
	rc = vfscanf(f, fmt, ap);
	va_end(ap);

	fclose(f);
	return rc;
}

static uint64_t sysfs_read_u64(struct blkdev_cxt *cxt, const char *attr)
{
	uint64_t x;
	return sysfs_scanf(cxt, attr, "%"SCNu64, &x) == 1 ? x : 0;
}

static char *sysfs_strdup(struct blkdev_cxt *cxt, const char *attr)
{
	char buf[1024];
	return sysfs_scanf(cxt, attr, "%1024[^\n]", buf) == 1 ?
						xstrdup(buf) : NULL;
}

static int sysfs_count_dirents(struct blkdev_cxt *cxt, const char *attr)
{
	DIR *dir;
	int r = 0;

	if (!(dir = sysfs_opendir(cxt, attr)))
		return 0;

	while (xreaddir(dir)) r++;

	closedir(dir);
	return r;
}

static int sysfs_count_partitions(struct blkdev_cxt *cxt)
{
	DIR *dir;
	struct dirent *d;
	int r = 0;

	if (!(dir = sysfs_opendir(cxt, NULL)))
		return 0;

	while ((d = xreaddir(dir))) {
		if (is_partition_dirent(dir, d, cxt->name))
			r++;
	}

	closedir(dir);
	return r;
}

static char *get_device_mountpoint(struct blkdev_cxt *cxt)
{
	int fl = 0;
	char mnt[PATH_MAX];

	*mnt = '\0';

	/*
	 * TODO: use libmount and parse /proc/mountinfo only once
	 */
	if (check_mount_point(cxt->filename, &fl, mnt, sizeof(mnt)) == 0 &&
	    (fl & MF_MOUNTED)) {
		if (fl & MF_SWAP)
			strcpy(mnt, "[SWAP]");
	}
	return strlen(mnt) ? xstrdup(mnt) : NULL;
}

/* TODO: read info from udev db (if possible) for non-root users
 */
static void probe_device(struct blkdev_cxt *cxt)
{
	char *path = NULL;
	blkid_probe pr = NULL;

	if (cxt->probed)
		return;
	cxt->probed = 1;

	if (!cxt->size)
		return;

	pr = blkid_new_probe_from_filename(cxt->filename);
	if (!pr)
		return;

	/* TODO: we have to enable partitions probing to avoid conflicts
	 *       between raids and PT -- see blkid(8) code for more details
	 */
	blkid_probe_enable_superblocks(pr, 1);
	blkid_probe_set_superblocks_flags(pr, BLKID_SUBLKS_LABEL |
					      BLKID_SUBLKS_UUID |
					      BLKID_SUBLKS_TYPE);
	if (!blkid_do_safeprobe(pr)) {
		const char *data = NULL;

		if (!blkid_probe_lookup_value(pr, "TYPE", &data, NULL))
			cxt->fstype = xstrdup(data);
		if (!blkid_probe_lookup_value(pr, "UUID", &data, NULL))
			cxt->uuid = xstrdup(data);
		if (!blkid_probe_lookup_value(pr, "LABEL", &data, NULL))
			cxt->label = xstrdup(data);
	}

	free(path);
	blkid_free_probe(pr);
	return;
}

static int is_readonly_device(struct blkdev_cxt *cxt)
{
	int fd, ro = 0;

	if (sysfs_scanf(cxt, "ro", "%d", &ro) == 0)
		return ro;

	/* fallback if "ro" attribute does not exist */
	fd = open(cxt->filename, O_RDONLY);
	if (fd != -1) {
		ioctl(fd, BLKROGET, &ro);
		close(fd);
	}
	return ro;
}

static char *get_scheduler(struct blkdev_cxt *cxt)
{
	char *str = sysfs_strdup(cxt, "queue/scheduler");
	char *p, *res = NULL;

	if (!str)
		return NULL;
	p = strchr(str, '[');
	if (p) {
		res = p + 1;
		p = strchr(res, ']');
		if (p) {
			*p = '\0';
			res = xstrdup(res);
		} else
			res = NULL;
	}
	free(str);
	return res;
}

static void set_tt_data(struct blkdev_cxt *cxt, int col, int id, struct tt_line *ln)
{
	char buf[1024];
	char *p;

	if (!cxt->st.st_rdev && (id == COL_OWNER || id == COL_GROUP ||
				 id == COL_MODE))
		stat(cxt->filename, &cxt->st);

	switch(id) {
	case COL_NAME:
		if (cxt->dm_name) {
			snprintf(buf, sizeof(buf), "%s (%s)",
					cxt->dm_name, cxt->name);
			tt_line_set_data(ln, col, xstrdup(buf));
			break;
		}
	case COL_KNAME:
		tt_line_set_data(ln, col, xstrdup(cxt->name));
		break;
	case COL_OWNER:
	{
		struct passwd *pw = getpwuid(cxt->st.st_uid);
		if (pw)
			tt_line_set_data(ln, col, xstrdup(pw->pw_name));
		break;
	}
	case COL_GROUP:
	{
		struct group *gr = getgrgid(cxt->st.st_gid);
		if (gr)
			tt_line_set_data(ln, col, xstrdup(gr->gr_name));
		break;
	}
	case COL_MODE:
	{
		char md[11];
		strmode(cxt->st.st_mode, md);
		tt_line_set_data(ln, col, xstrdup(md));
		break;
	}
	case COL_MAJMIN:
		if (lsblk->tt->flags & TT_FL_RAW)
			snprintf(buf, sizeof(buf), "%u:%u", cxt->maj, cxt->min);
		else
			snprintf(buf, sizeof(buf), "%3u:%-3u", cxt->maj, cxt->min);
		tt_line_set_data(ln, col, xstrdup(buf));
		break;
	case COL_FSTYPE:
		probe_device(cxt);
		if (cxt->fstype)
			tt_line_set_data(ln, col, xstrdup(cxt->fstype));
		break;
	case COL_TARGET:
		if (!cxt->nholders) {
			p = get_device_mountpoint(cxt);
			if (p)
				tt_line_set_data(ln, col, p);
		}
		break;
	case COL_LABEL:
		probe_device(cxt);
		if (cxt->label)
			tt_line_set_data(ln, col, xstrdup(cxt->label));
		break;
	case COL_UUID:
		probe_device(cxt);
		if (cxt->uuid)
			tt_line_set_data(ln, col, xstrdup(cxt->uuid));
		break;
	case COL_RO:
		tt_line_set_data(ln, col, is_readonly_device(cxt) ?
					xstrdup("1") : xstrdup("0"));
		break;
	case COL_RM:
		p = sysfs_strdup(cxt, "removable");
		if (!p && cxt->parent)
			p = sysfs_strdup(cxt->parent, "removable");
		if (p)
			tt_line_set_data(ln, col, p);
		break;
	case COL_ROTA:
		p = sysfs_strdup(cxt, "queue/rotational");
		if (p)
			tt_line_set_data(ln, col, p);
		break;
	case COL_MODEL:
		if (!cxt->partition && cxt->nslaves == 0) {
			p = sysfs_strdup(cxt, "device/model");
			if (p)
				tt_line_set_data(ln, col, p);
		}
		break;
	case COL_SIZE:
		if (cxt->size) {
			if (lsblk->bytes) {
				if (asprintf(&p, "%jd", cxt->size) < 0)
					p = NULL;
			} else
				p = size_to_human_string(cxt->size);
			if (p)
				tt_line_set_data(ln, col, p);
		}
		break;
	case COL_ALIOFF:
		p = sysfs_strdup(cxt, "alignment_offset");
		if (p)
			tt_line_set_data(ln, col, p);
		break;
	case COL_MINIO:
		p = sysfs_strdup(cxt, "queue/minimum_io_size");
		if (p)
			tt_line_set_data(ln, col, p);
		break;
	case COL_OPTIO:
		p = sysfs_strdup(cxt, "queue/optimal_io_size");
		if (p)
			tt_line_set_data(ln, col, p);
		break;
	case COL_PHYSEC:
		p = sysfs_strdup(cxt, "queue/physical_block_size");
		if (p)
			tt_line_set_data(ln, col, p);
		break;
	case COL_LOGSEC:
		p = sysfs_strdup(cxt, "queue/logical_block_size");
		if (p)
			tt_line_set_data(ln, col, p);
		break;
	case COL_SCHED:
		p = get_scheduler(cxt);
		if (p)
			tt_line_set_data(ln, col, p);
		break;
	};
}

static void print_device(struct blkdev_cxt *cxt, struct tt_line *tt_parent)
{
	int i;

	cxt->tt_line = tt_add_line(lsblk->tt, tt_parent);

	for (i = 0; i < ncolumns; i++)
		set_tt_data(cxt, i, get_column_id(i), cxt->tt_line);
}

static int set_cxt(struct blkdev_cxt *cxt,
		    struct blkdev_cxt *parent,
		    const char *name,
		    int partition)
{
	char *p;

	cxt->parent = parent;
	cxt->name = xstrdup(name);
	cxt->partition = partition;

	cxt->filename = get_device_path(cxt);

	/* open /sys/block/<name> */
	p = get_sysfs_path(cxt);
	cxt->sysfs_fd = open(p, O_RDONLY);
	if (cxt->sysfs_fd < 0)
		err(EXIT_FAILURE, _("%s: open failed"), p);
	free(p);

	if (sysfs_scanf(cxt, "dev", "%u:%u", &cxt->maj, &cxt->min) != 2)
		return -1;

	cxt->size = sysfs_read_u64(cxt, "size") << 9;

	/* Ignore devices of zero size */
	if (!lsblk->all_devices && cxt->size == 0)
		return -1;

	if (is_dm(name))
		cxt->dm_name = sysfs_strdup(cxt, "dm/name");

	cxt->nholders = sysfs_count_dirents(cxt, "holders") +
			sysfs_count_partitions(cxt);
	cxt->nslaves = sysfs_count_dirents(cxt, "slaves");

	return 0;
}

/*
 * List devices (holders) mapped to device
 */
static int list_holders(struct blkdev_cxt *cxt)
{
	DIR *dir;
	struct dirent *d;
	struct blkdev_cxt holder = {};

	assert(cxt);
	assert(cxt->sysfs_fd >= 0);

	if (lsblk->nodeps)
		return 0;

	if (!cxt->nholders)
		return 0;

	/* Partitions */
	dir = sysfs_opendir(cxt, NULL);
	if (!dir)
		err(EXIT_FAILURE, _("failed to open device directory in sysfs"));

	while ((d = xreaddir(dir))) {
		if (!is_partition_dirent(dir, d, cxt->name))
			continue;

		set_cxt(&holder, cxt, d->d_name, 1);
		print_device(&holder, cxt->tt_line);
		list_holders(&holder);
		reset_blkdev_cxt(&holder);
	}
	closedir(dir);

	/* Holders */
	dir = sysfs_opendir(cxt, "holders");
	if (!dir)
		return 0;

	while ((d = xreaddir(dir))) {
		set_cxt(&holder, cxt, d->d_name, 0);
		print_device(&holder, cxt->tt_line);
		list_holders(&holder);
		reset_blkdev_cxt(&holder);
	}
	closedir(dir);

	return 0;
}

/* Iterate top-level devices in sysfs */
static int iterate_block_devices(void)
{
	DIR *dir;
	struct dirent *d;
	struct blkdev_cxt cxt = {};

	if (!(dir = opendir(_PATH_SYS_BLOCK)))
		return EXIT_FAILURE;

	while ((d = xreaddir(dir))) {

		if (set_cxt(&cxt, NULL, d->d_name, 0))
			goto next;

		/* Skip devices in the middle of dependence tree */
		if (cxt.nslaves > 0)
			goto next;

		if (!lsblk->all_devices && is_maj_excluded(cxt.maj))
			goto next;

		print_device(&cxt, NULL);
		list_holders(&cxt);
	next:
		reset_blkdev_cxt(&cxt);
	}

	closedir(dir);

	return EXIT_SUCCESS;
}

static int process_one_device(char *devname)
{
	struct blkdev_cxt parent = {}, cxt = {};
	struct stat st;
	char buf[PATH_MAX];
	dev_t disk = 0;

	if (stat(devname, &st) || !S_ISBLK(st.st_mode)) {
		warnx(_("%s: not a block device"), devname);
		return EXIT_FAILURE;
	}
	if (blkid_devno_to_wholedisk(st.st_rdev, buf, sizeof(buf), &disk)) {
		warn(_("%s: failed to get whole-list devno"), devname);
		return EXIT_FAILURE;
	}
	if (st.st_rdev == disk)
		/*
		 * unpartitioned device
		 */
		set_cxt(&cxt, NULL, buf, 0);
	else {
		/*
		 * Parititioned, read sysfs name of the device
		 */
		size_t len;
		char path[PATH_MAX], *diskname, *name;

		snprintf(path, sizeof(path), "/sys/dev/block/%d:%d",
				    major(st.st_rdev), minor(st.st_rdev));
		diskname = xstrdup(buf);

		len = readlink(path, buf, sizeof(buf));
		if (len < 0) {
			warn(_("%s: failed to read link"), path);
			return EXIT_FAILURE;
		}
		buf[len] = '\0';

		/* sysfs device name */
		name = strrchr(buf, '/') + 1;

		set_cxt(&parent, NULL, diskname, 0);
		set_cxt(&cxt, &parent, name, 1);

		free(diskname);
	}

	print_device(&cxt, NULL);
	list_holders(&cxt);
	reset_blkdev_cxt(&cxt);

	if (st.st_rdev != disk)
		reset_blkdev_cxt(&parent);

	return EXIT_SUCCESS;
}

static void parse_excludes(const char *str)
{
	nexcludes = 0;

	while (str && *str) {
		char *end = NULL;
		unsigned int n;

		errno = 0;
		n = strtoul(str, &end, 10);

		if (end == str || (errno != 0 && (n == ULONG_MAX || n == 0)))
			err(EXIT_FAILURE, _("failed to parse list '%s'"), str);
		excludes[nexcludes++] = n;

		if (nexcludes == ARRAY_SIZE(excludes))
			errx(EXIT_FAILURE, _("the list of excluded devices is "
					"too large (limit is %d devices)"),
					(int)ARRAY_SIZE(excludes));
		str = end && *end ? end + 1 : NULL;
	}
}

static void __attribute__((__noreturn__)) help(FILE *out)
{
	int i;

	fprintf(out, _(
		"\nUsage:\n"
		" %s [options] [<device> ...]\n"), program_invocation_short_name);

	fprintf(out, _(
		"\nOptions:\n"
		" -a, --all            print all devices\n"
		" -b, --bytes          print SIZE in bytes rather than in human readable format\n"
		" -d, --nodeps         don't print slaves or holders\n"
		" -e, --exclude <list> exclude devices by major number (default: RAM disks)\n"
		" -f, --fs             output info about filesystems\n"
		" -h, --help           usage information (this)\n"
		" -i, --ascii          use ascii characters only\n"
		" -m, --perms          output info about permissions\n"
		" -l, --list           use list format ouput\n"
		" -n, --noheadings     don't print headings\n"
		" -o, --output <list>  output columns\n"
		" -r, --raw            use raw format output\n"
		" -t, --topology       output info about topology\n"));

	fprintf(out, _("\nAvailable columns:\n"));

	for (i = 0; i < __NCOLUMNS; i++)
		fprintf(out, " %10s  %s\n", infos[i].name, gettext(infos[i].help));

	fprintf(out, _("\nFor more information see lsblk(8).\n"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void __attribute__((__noreturn__))
errx_mutually_exclusive(const char *opts)
{
	errx(EXIT_FAILURE, "%s %s", opts, _("options are mutually exclusive"));
}

int main(int argc, char *argv[])
{
	struct lsblk _ls;
	int tt_flags = TT_FL_TREE;
	int i, c, status = EXIT_FAILURE;

	struct option longopts[] = {
		{ "all",	0, 0, 'a' },
		{ "bytes",      0, 0, 'b' },
		{ "nodeps",     0, 0, 'd' },
		{ "help",	0, 0, 'h' },
		{ "output",     1, 0, 'o' },
		{ "perms",      0, 0, 'm' },
		{ "noheadings",	0, 0, 'n' },
		{ "list",       0, 0, 'l' },
		{ "ascii",	0, 0, 'i' },
		{ "raw",        0, 0, 'r' },
		{ "fs",         0, 0, 'f' },
		{ "exclude",    1, 0, 'e' },
		{ "topology",   0, 0, 't' },
		{ NULL, 0, 0, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	lsblk = &_ls;
	memset(lsblk, 0, sizeof(*lsblk));

	while((c = getopt_long(argc, argv, "abde:fhlnmo:irt", longopts, NULL)) != -1) {
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
		case 'e':
			parse_excludes(optarg);
			break;
		case 'h':
			help(stdout);
			break;
		case 'l':
			if (tt_flags & TT_FL_RAW)
				errx_mutually_exclusive("--{raw,list}");

			tt_flags &= ~TT_FL_TREE; /* disable the default */
			break;
		case 'n':
			tt_flags |= TT_FL_NOHEADINGS;
			break;
		case 'o':
			if (tt_parse_columns_list(optarg, columns, &ncolumns,
						column_name_to_id))
				return EXIT_FAILURE;
			break;
		case 'i':
			tt_flags = TT_FL_ASCII;
			break;
		case 'r':
			tt_flags &= ~TT_FL_TREE;	/* disable the default */
			tt_flags |= TT_FL_RAW;		/* enable raw */
			break;
		case 'f':
			columns[ncolumns++] = COL_NAME;
			columns[ncolumns++] = COL_FSTYPE;
			columns[ncolumns++] = COL_LABEL;
			columns[ncolumns++] = COL_TARGET;
			break;
		case 'm':
			columns[ncolumns++] = COL_NAME;
			columns[ncolumns++] = COL_SIZE;
			columns[ncolumns++] = COL_OWNER;
			columns[ncolumns++] = COL_GROUP;
			columns[ncolumns++] = COL_MODE;
			break;
		case 't':
			columns[ncolumns++] = COL_NAME;
			columns[ncolumns++] = COL_ALIOFF;
			columns[ncolumns++] = COL_MINIO;
			columns[ncolumns++] = COL_OPTIO;
			columns[ncolumns++] = COL_PHYSEC;
			columns[ncolumns++] = COL_LOGSEC;
			columns[ncolumns++] = COL_ROTA;
			columns[ncolumns++] = COL_SCHED;
			break;
		default:
			help(stderr);
		}
	}

	if (!ncolumns) {
		columns[ncolumns++] = COL_NAME;
		columns[ncolumns++] = COL_MAJMIN;
		columns[ncolumns++] = COL_RM;
		columns[ncolumns++] = COL_SIZE;
		columns[ncolumns++] = COL_RO;
		columns[ncolumns++] = COL_TARGET;
	}

	if (nexcludes && lsblk->all_devices)
		errx_mutually_exclusive("--{all,exclude}");
	else if (!nexcludes)
		excludes[nexcludes++] = 1;	/* default: ignore RAM disks */
	/*
	 * initialize output columns
	 */
	if (!(lsblk->tt = tt_new_table(tt_flags)))
		errx(EXIT_FAILURE, _("failed to initialize output table"));

	for (i = 0; i < ncolumns; i++) {
		struct colinfo *ci = get_column_info(i);
		int fl = ci->flags;

		if (!(tt_flags & TT_FL_TREE) && get_column_id(i) == COL_NAME)
			fl &= ~TT_FL_TREE;

		if (!tt_define_column(lsblk->tt, ci->name, ci->whint, fl)) {
			warn(_("failed to initialize output column"));
			goto leave;
		}
	}

	if (optind == argc)
		status = iterate_block_devices();
	else while (optind < argc)
		status = process_one_device(argv[optind++]);

	tt_print_table(lsblk->tt);

leave:
	tt_free_table(lsblk->tt);
	return status;
}
