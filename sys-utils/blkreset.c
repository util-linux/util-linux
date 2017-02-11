/*
 * blkreset.c -- Reset the WP on a range of zones.
 *
 * Copyright (C) 2015,2016 Seagate Technology PLC
 * Written by Shaun Tancheff <shaun.tancheff@seagate.com>
 *
 * Copyright (C) 2017 Karel Zak <kzak@redhat.com>
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
 * This program uses BLKREPORT ioctl to query zone information about part of
 * or a whole block device, if the device supports it.
 * You can specify range (start and length) to be queried.
 */

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <limits.h>
#include <getopt.h>
#include <time.h>
#include <ctype.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/blkzoned.h>

#include "sysfs.h"
#include "nls.h"
#include "strutils.h"
#include "c.h"
#include "closestream.h"
#include "blkdev.h"

static unsigned long blkdev_chunk_sectors(const char *dname)
{
	struct sysfs_cxt cxt = UL_SYSFSCXT_EMPTY;
	dev_t devno = sysfs_devname_to_devno(dname, NULL);
	int major_no = major(devno);
	int block_no = minor(devno) & ~0x0f;
	uint64_t sz;

	/*
	 * Mapping /dev/sdXn -> /sys/block/sdX to read the chunk_size entry.
	 * This method masks off the partition specified by the minor device
	 * component.
	 */
	devno = makedev(major_no, block_no);
	if (sysfs_init(&cxt, devno, NULL))
		return 0;

	if (sysfs_read_u64(&cxt, "queue/chunk_sectors", &sz) != 0)
		warnx(_("%s: failed to read chunk size"), dname);

	sysfs_deinit(&cxt);
	return sz;
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options] <device>\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Discard the content of sectors on a device.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -z, --zone <offset>    start sector of zone to act upon\n"
		"                          (in 512-byte sectors, default is 0))\n"
		" -c, --count <length>   number of zones to reset (default is 1)"),
		out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("blkreset(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	char *path;
	int c, fd;
	uint64_t blksectors = 0;
	struct stat sb;
	struct blk_zone_range za;
	uint64_t zsector = 0;
	uint64_t zlen;
	uint64_t zcount = 1;
	unsigned long zsize;
	int rc;

	static const struct option longopts[] = {
	    { "help",    no_argument,       NULL, 'h' },
	    { "version", no_argument,       NULL, 'V' },
	    { "zone",    required_argument, NULL, 'z' },
	    { "count",   required_argument, NULL, 'c' },
	    { NULL, 0, NULL, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "hVz:c:", longopts, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage(stdout);
			break;
		case 'c':
			zcount = strtosize_or_err(optarg,
					_("failed to parse number of zones"));
			break;
		case 'z':
			zsector = strtosize_or_err(optarg,
					_("failed to parse zone offset"));
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		default:
			usage(stderr);
			break;
		}
	}

	if (optind == argc)
		errx(EXIT_FAILURE, _("no device specified"));

	path = argv[optind++];

	if (optind != argc) {
		warnx(_("unexpected number of arguments"));
		usage(stderr);
	}

	zsize = blkdev_chunk_sectors(path);
	if (zsize == 0)
		err(EXIT_FAILURE, _("%s: Unable to determine zone size"), path);

	fd = open(path, O_WRONLY);
	if (fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), path);

	if (fstat(fd, &sb) == -1)
		err(EXIT_FAILURE, _("stat of %s failed"), path);
	if (!S_ISBLK(sb.st_mode))
		errx(EXIT_FAILURE, _("%s: not a block device"), path);

	if (blkdev_get_sectors(fd, (unsigned long long *) &blksectors))
		err(EXIT_FAILURE, _("%s: blkdev_get_sectors ioctl failed"), path);

	/* check offset alignment to the chunk size */
	if (zsector & (zsize - 1))
		errx(EXIT_FAILURE, _("%s: zone %" PRIu64 " is not aligned "
			 "to zone size %" PRIu64), path, zsector, zsize);
	if (zsector > blksectors)
		errx(EXIT_FAILURE, _("%s: offset is greater than device size"), path);

	zlen = zcount * zsize;
	if (zsector + zlen > blksectors)
		zlen = blksectors - zsector;

	za.sector = zsector;
	za.nr_sectors = zlen;
	rc = ioctl(fd, BLKRESETZONE, &za);
	if (rc == -1)
		err(EXIT_FAILURE, _("%s: BLKRESETZONE ioctl failed"), path);

	close(fd);
	return EXIT_SUCCESS;
}
