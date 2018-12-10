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

#include <libmount.h>


#ifndef FITRIM
struct fstrim_range {
	uint64_t start;
	uint64_t len;
	uint64_t minlen;
};
#define FITRIM		_IOWR('X', 121, struct fstrim_range)
#endif

struct fstrim_control {
	struct fstrim_range range;

	unsigned int verbose : 1,
		     fstab   : 1,
		     dryrun : 1;
};

/* returns: 0 = success, 1 = unsupported, < 0 = error */
static int fstrim_filesystem(struct fstrim_control *ctl, const char *path, const char *devname)
{
	int fd, rc;
	struct stat sb;
	struct fstrim_range range;

	/* kernel modifies the range */
	memcpy(&range, &ctl->range, sizeof(range));

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

	if (ctl->dryrun) {
		if (devname)
			printf(_("%s: 0 B (dry run) trimmed on %s\n"), path, devname);
		else
			printf(_("%s: 0 B (dry run) trimmed\n"), path);
		rc = 0;
		goto done;
	}

	errno = 0;
	if (ioctl(fd, FITRIM, &range)) {
		rc = errno == EOPNOTSUPP || errno == ENOTTY ? 1 : -errno;

		if (rc != 1)
			warn(_("%s: FITRIM ioctl failed"), path);
		goto done;
	}

	if (ctl->verbose) {
		char *str = size_to_human_string(
				SIZE_SUFFIX_3LETTER | SIZE_SUFFIX_SPACE,
				(uint64_t) range.len);
		if (devname)
			/* TRANSLATORS: The standard value here is a very large number. */
			printf(_("%s: %s (%" PRIu64 " bytes) trimmed on %s\n"),
				path, str, (uint64_t) range.len, devname);
		else
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

static int has_discard(const char *devname, struct path_cxt **wholedisk)
{
	struct path_cxt *pc = NULL;
	uint64_t dg = 0;
	dev_t disk = 0, dev;
	int rc = -1;

	dev = sysfs_devname_to_devno(devname);
	if (!dev)
		goto fail;

	pc = ul_new_sysfs_path(dev, NULL, NULL);
	if (!pc)
		goto fail;

	/*
	 * This is tricky to read the info from sys/, because the queue
	 * attributes are provided for whole devices (disk) only. We're trying
	 * to reuse the whole-disk sysfs context to optimize this stuff (as
	 * system usually have just one disk only).
	 */
	rc = sysfs_blkdev_get_wholedisk(pc, NULL, 0, &disk);
	if (rc != 0 || !disk)
		goto fail;

	if (dev != disk) {
		/* Partition, try reuse whole-disk context if valid for the
		 * current device, otherwise create new context for the
		 * whole-disk.
		 */
		if (*wholedisk && sysfs_blkdev_get_devno(*wholedisk) != disk) {
			ul_unref_path(*wholedisk);
			*wholedisk = NULL;
		}
		if (!*wholedisk) {
			*wholedisk = ul_new_sysfs_path(disk, NULL, NULL);
			if (!*wholedisk)
				goto fail;
		}
		sysfs_blkdev_set_parent(pc, *wholedisk);
	}

	rc = ul_path_read_u64(pc, &dg, "queue/discard_granularity");

	ul_unref_path(pc);
	return rc == 0 && dg > 0;
fail:
	ul_unref_path(pc);
	return 1;
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
	if (mnt_fs_is_pseudofs(a) || mnt_fs_is_netfs(a) ||
	    mnt_fs_is_pseudofs(b) || mnt_fs_is_netfs(b))
		return 1;

	return !mnt_fs_streq_srcpath(a, mnt_fs_get_srcpath(b));
}

/*
 * fstrim --all follows "mount -a" return codes:
 *
 * 0  = all success
 * 32 = all failed
 * 64 = some failed, some success
 */
static int fstrim_all(struct fstrim_control *ctl)
{
	struct libmnt_fs *fs;
	struct libmnt_iter *itr;
	struct libmnt_table *tab;
	struct libmnt_cache *cache = NULL;
	struct path_cxt *wholedisk = NULL;
	int cnt = 0, cnt_err = 0;
	const char *filename = _PATH_PROC_MOUNTINFO;

	mnt_init_debug(0);
	ul_path_init_debug();

	itr = mnt_new_iter(MNT_ITER_BACKWARD);
	if (!itr)
		err(MNT_EX_FAIL, _("failed to initialize libmount iterator"));

	if (ctl->fstab)
		filename = mnt_get_fstab_path();

	tab = mnt_new_table_from_file(filename);
	if (!tab)
		err(MNT_EX_FAIL, _("failed to parse %s"), filename);

	/* de-duplicate by mountpoints */
	mnt_table_uniq_fs(tab, 0, uniq_fs_target_cmp);

	/* de-duplicate by source */
	mnt_table_uniq_fs(tab, MNT_UNIQ_FORWARD, uniq_fs_source_cmp);

	if (ctl->fstab) {
		cache = mnt_new_cache();
		if (!cache)
			err(MNT_EX_FAIL, _("failed to initialize libmount cache"));
	}

	while (mnt_table_next_fs(tab, itr, &fs) == 0) {
		const char *src = mnt_fs_get_srcpath(fs),
			   *tgt = mnt_fs_get_target(fs);
		char *path;
		int rc = 1;

		if (!tgt || mnt_fs_is_pseudofs(fs) || mnt_fs_is_netfs(fs))
			continue;

		if (!src && cache) {
			/* convert LABEL= (etc.) from fstab to paths */
			const char *spec = mnt_fs_get_source(fs);

			if (!spec)
				continue;
			src = mnt_resolve_spec(spec, cache);
		}

		if (!src || *src != '/')
			continue;

		/* Is it really accessible mountpoint? Not all mountpoints are
		 * accessible (maybe over mounted by another filesystem) */
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
		if (fstrim_filesystem(ctl, tgt, src) < 0)
		       cnt_err++;
	}

	ul_unref_path(wholedisk);
	mnt_unref_table(tab);
	mnt_free_iter(itr);
	mnt_unref_cache(cache);

	if (cnt && cnt == cnt_err)
		return MNT_EX_FAIL;		/* all failed */
	if (cnt && cnt_err)
		return MNT_EX_SOMEOK;		/* some ok */

	return MNT_EX_SUCCESS;
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] <mount point>\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Discard unused blocks on a mounted filesystem.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --all           trim all supported mounted filesystems\n"), out);
	fputs(_(" -A, --fstab         trim all supported mounted filesystems from /etc/fstab\n"), out);
	fputs(_(" -o, --offset <num>  the offset in bytes to start discarding from\n"), out);
	fputs(_(" -l, --length <num>  the number of bytes to discard\n"), out);
	fputs(_(" -m, --minimum <num> the minimum extent length to discard\n"), out);
	fputs(_(" -v, --verbose       print number of discarded bytes\n"), out);
	fputs(_(" -n, --dry-run       does everything, but trim\n"), out);

	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(21));
	printf(USAGE_MAN_TAIL("fstrim(8)"));
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	char *path = NULL;
	int c, rc, all = 0;
	struct fstrim_control ctl = {
			.range = { .len = ULLONG_MAX }
	};

	static const struct option longopts[] = {
	    { "all",       no_argument,       NULL, 'a' },
	    { "fstab",     no_argument,       NULL, 'A' },
	    { "help",      no_argument,       NULL, 'h' },
	    { "version",   no_argument,       NULL, 'V' },
	    { "offset",    required_argument, NULL, 'o' },
	    { "length",    required_argument, NULL, 'l' },
	    { "minimum",   required_argument, NULL, 'm' },
	    { "verbose",   no_argument,       NULL, 'v' },
	    { "dry-run",   no_argument,       NULL, 'n' },
	    { NULL, 0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "Aahl:m:no:Vv", longopts, NULL)) != -1) {
		switch(c) {
		case 'A':
			ctl.fstab = 1;
			/* fallthrough */
		case 'a':
			all = 1;
			break;
		case 'n':
			ctl.dryrun = 1;
			break;
		case 'h':
			usage();
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'l':
			ctl.range.len = strtosize_or_err(optarg,
					_("failed to parse length"));
			break;
		case 'o':
			ctl.range.start = strtosize_or_err(optarg,
					_("failed to parse offset"));
			break;
		case 'm':
			ctl.range.minlen = strtosize_or_err(optarg,
					_("failed to parse minimum extent length"));
			break;
		case 'v':
			ctl.verbose = 1;
			break;
		default:
			errtryhelp(EXIT_FAILURE);
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
		errtryhelp(EXIT_FAILURE);
	}

	if (all)
		return fstrim_all(&ctl);	/* MNT_EX_* codes */

	rc = fstrim_filesystem(&ctl, path, NULL);
	if (rc == 1)
		warnx(_("%s: the discard operation is not supported"), path);

	return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
