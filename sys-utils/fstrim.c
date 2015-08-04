/*
 * fstrim.c -- discard the part (or whole) of mounted filesystem.
 *
 * Copyright (C) 2010 Red Hat, Inc. All rights reserved.
 * Written by Lukas Czerner <lczerner@redhat.com>
 *            Karel Zak <kzak@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * This program uses FITRIM ioctl to discard parts or the whole filesystem
 * online (mounted). You can specify range (start and length) to be
 * discarded, or simply discard whole filesystem.
 */

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <limits.h>
#include <getopt.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/fs.h>

#include "nls.h"
#include "strutils.h"
#include "c.h"
#include "closestream.h"
#include "pathnames.h"
#include "sysfs.h"
#include "exitcodes.h"

#include <libmount.h>

#ifndef FITRIM
struct fstrim_range {
	uint64_t start;
	uint64_t len;
	uint64_t minlen;
};
#define FITRIM		_IOWR('X', 121, struct fstrim_range)
#endif

/* returns: 0 = success, 1 = unsupported, < 0 = error */
static int fstrim_filesystem(const char *path, struct fstrim_range *rangetpl,
			    int verbose)
{
	int fd, rc;
	struct stat sb;
	struct fstrim_range range;

	/* kernel modifies the range */
	memcpy(&range, rangetpl, sizeof(range));

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		warn(_("cannot open %s"), path);
		rc = -errno;
		goto done;
	}
	if (fstat(fd, &sb) == -1) {
		warn(_("stat of %s failed"), path);
		rc = -errno;
		goto done;
	}
	if (!S_ISDIR(sb.st_mode)) {
		warnx(_("%s: not a directory"), path);
		rc = -EINVAL;
		goto done;
	}
	errno = 0;
	if (ioctl(fd, FITRIM, &range)) {
		rc = errno == EOPNOTSUPP || errno == ENOTTY ? 1 : -errno;

		if (rc != 1)
			warn(_("%s: FITRIM ioctl failed"), path);
		goto done;
	}

	if (verbose) {
		char *str = size_to_human_string(
				SIZE_SUFFIX_3LETTER | SIZE_SUFFIX_SPACE,
				(uint64_t) range.len);
		/* TRANSLATORS: The standard value here is a very large number. */
		printf(_("%s: %s (%" PRIu64 " bytes) trimmed\n"),
				path, str, (uint64_t) range.len);
		free(str);
	}

	rc = 0;
done:
	if (fd >= 0)
		close(fd);
	return rc;
}

static int has_discard(const char *devname, struct sysfs_cxt *wholedisk)
{
	struct sysfs_cxt cxt, *parent = NULL;
	uint64_t dg = 0;
	dev_t disk = 0, dev;
	int rc;

	dev = sysfs_devname_to_devno(devname, NULL);
	if (!dev)
		return 1;
	/*
	 * This is tricky to read the info from sys/, because the queue
	 * atrributes are provided for whole devices (disk) only. We're trying
	 * to reuse the whole-disk sysfs context to optimize this stuff (as
	 * system usually have just one disk only).
	 */
	if (sysfs_devno_to_wholedisk(dev, NULL, 0, &disk) || !disk)
		return 1;
	if (dev != disk) {
		if (wholedisk->devno != disk) {
			sysfs_deinit(wholedisk);
			if (sysfs_init(wholedisk, disk, NULL))
				return 1;
		}
		parent = wholedisk;
	}

	rc = sysfs_init(&cxt, dev, parent);
	if (!rc)
		rc = sysfs_read_u64(&cxt, "queue/discard_granularity", &dg);

	sysfs_deinit(&cxt);
	return rc == 0 && dg > 0;
}


static int uniq_fs_target_cmp(
		struct libmnt_table *tb __attribute__((__unused__)),
		struct libmnt_fs *a,
		struct libmnt_fs *b)
{
	return !mnt_fs_streq_target(a, mnt_fs_get_target(b));
}

static int uniq_fs_source_cmp(
		struct libmnt_table *tb __attribute__((__unused__)),
		struct libmnt_fs *a,
		struct libmnt_fs *b)
{
	int eq;

	if (mnt_fs_is_pseudofs(a) || mnt_fs_is_netfs(a) ||
	    mnt_fs_is_pseudofs(b) || mnt_fs_is_netfs(b))
		return 1;

	eq = mnt_fs_streq_srcpath(a, mnt_fs_get_srcpath(b));
	if (eq) {
		const char *aroot = mnt_fs_get_root(a),
			   *broot = mnt_fs_get_root(b);
		if (!aroot || !broot)
			eq = 0;
		else if (strcmp(aroot, broot) != 0)
			eq = 0;
	}

	return !eq;
}

/*
 * fstrim --all follows "mount -a" return codes:
 *
 * 0  = all success
 * 32 = all failed
 * 64 = some failed, some success
 */
static int fstrim_all(struct fstrim_range *rangetpl, int verbose)
{
	struct libmnt_fs *fs;
	struct libmnt_iter *itr;
	struct libmnt_table *tab;
	struct sysfs_cxt wholedisk = UL_SYSFSCXT_EMPTY;
	int cnt = 0, cnt_err = 0;

	mnt_init_debug(0);

	itr = mnt_new_iter(MNT_ITER_BACKWARD);
	if (!itr)
		err(MOUNT_EX_FAIL, _("failed to initialize libmount iterator"));

	tab = mnt_new_table_from_file(_PATH_PROC_MOUNTINFO);
	if (!tab)
		err(MOUNT_EX_FAIL, _("failed to parse %s"), _PATH_PROC_MOUNTINFO);

	/* de-duplicate by mountpoints */
	mnt_table_uniq_fs(tab, 0, uniq_fs_target_cmp);

	/* de-duplicate by source and root */
	mnt_table_uniq_fs(tab, 0, uniq_fs_source_cmp);

	while (mnt_table_next_fs(tab, itr, &fs) == 0) {
		const char *src = mnt_fs_get_srcpath(fs),
			   *tgt = mnt_fs_get_target(fs);
		char *path;
		int rc = 1;

		if (!src || !tgt || *src != '/' ||
		    mnt_fs_is_pseudofs(fs) ||
		    mnt_fs_is_netfs(fs))
			continue;

		/* Is it really accessible mountpoint? Not all mountpoints are
		 * accessible (maybe over mounted by another fylesystem) */
		path = mnt_get_mountpoint(tgt);
		if (path && strcmp(path, tgt) == 0)
			rc = 0;
		free(path);
		if (rc)
			continue;	/* overlaying mount */

		if (!has_discard(src, &wholedisk))
			continue;
		cnt++;

		/*
		 * We're able to detect that the device supports discard, but
		 * things also depend on filesystem or device mapping, for
		 * example vfat or LUKS (by default) does not support FSTRIM.
		 *
		 * This is reason why we ignore EOPNOTSUPP and ENOTTY errors
		 * from discard ioctl.
		 */
		if (fstrim_filesystem(tgt, rangetpl, verbose) < 0)
		       cnt_err++;
	}

	sysfs_deinit(&wholedisk);
	mnt_unref_table(tab);
	mnt_free_iter(itr);

	if (cnt && cnt == cnt_err)
		return MOUNT_EX_FAIL;		/* all failed */
	if (cnt && cnt_err)
		return MOUNT_EX_SOMEOK;		/* some ok */

	return EXIT_SUCCESS;
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] <mount point>\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Discard unused blocks on a mounted filesystem.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --all           trim all mounted filesystems that are supported\n"), out);
	fputs(_(" -o, --offset <num>  the offset in bytes to start discarding from\n"), out);
	fputs(_(" -l, --length <num>  the number of bytes to discard\n"), out);
	fputs(_(" -m, --minimum <num> the minimum extent length to discard\n"), out);
	fputs(_(" -v, --verbose       print number of discarded bytes\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("fstrim(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	char *path = NULL;
	int c, rc, verbose = 0, all = 0;
	struct fstrim_range range;

	static const struct option longopts[] = {
	    { "all",       0, 0, 'a' },
	    { "help",      0, 0, 'h' },
	    { "version",   0, 0, 'V' },
	    { "offset",    1, 0, 'o' },
	    { "length",    1, 0, 'l' },
	    { "minimum",   1, 0, 'm' },
	    { "verbose",   0, 0, 'v' },
	    { NULL,        0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	memset(&range, 0, sizeof(range));
	range.len = ULLONG_MAX;

	while ((c = getopt_long(argc, argv, "ahVo:l:m:v", longopts, NULL)) != -1) {
		switch(c) {
		case 'a':
			all = 1;
			break;
		case 'h':
			usage(stdout);
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'l':
			range.len = strtosize_or_err(optarg,
					_("failed to parse length"));
			break;
		case 'o':
			range.start = strtosize_or_err(optarg,
					_("failed to parse offset"));
			break;
		case 'm':
			range.minlen = strtosize_or_err(optarg,
					_("failed to parse minimum extent length"));
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage(stderr);
			break;
		}
	}

	if (!all) {
		if (optind == argc)
			errx(EXIT_FAILURE, _("no mountpoint specified"));
		path = argv[optind++];
	}

	if (optind != argc) {
		warnx(_("unexpected number of arguments"));
		usage(stderr);
	}

	if (all)
		rc = fstrim_all(&range, verbose);
	else {
		rc = fstrim_filesystem(path, &range, verbose);
		if (rc == 1) {
			warnx(_("%s: the discard operation is not supported"), path);
			rc = EXIT_FAILURE;
		}
	}

	return rc;
}
