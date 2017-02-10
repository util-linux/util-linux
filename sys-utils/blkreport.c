/*
 * blkreport.c -- request a zone report on part (or all) of the block device.
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

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <linux/fs.h>
#include <linux/blkzoned.h>

#include "nls.h"
#include "strutils.h"
#include "xalloc.h"
#include "c.h"
#include "closestream.h"
#include "blkdev.h"

static const char * type_text[] = {
	"RESERVED",
	"CONVENTIONAL",
	"SEQ_WRITE_REQUIRED",
	"SEQ_WRITE_PREFERRED",
};

#define ARRAY_COUNT(x) (sizeof((x))/sizeof((*x)))

const char * condition_str[] = {
	"cv", /* conventional zone */
	"e0", /* empty */
	"Oi", /* open implicit */
	"Oe", /* open explicit */
	"Cl", /* closed */
	"x5", "x6", "x7", "x8", "x9", "xA", "xB", /* xN: reserved */
	"ro", /* read only */
	"fu", /* full */
	"OL"  /* offline */
	};

static const char * zone_condition_str(uint8_t cond)
{
	return condition_str[cond & 0x0f];
}

static void print_zones(struct blk_zone *info, uint32_t count)
{
	uint32_t iter;

	printf(_("Zones returned: %u\n"), count);

	for (iter = 0; iter < count; iter++ ) {
		struct blk_zone * entry = &info[iter];
		unsigned int type  = entry->type;
		uint64_t start = entry->start;
		uint64_t wp = entry->wp;
		uint8_t cond = entry->cond;
		uint64_t len = entry->len;

		if (!len)
			break;

		printf(_("  start: %9lx, len %6lx, wptr %6lx"
			 " reset:%u non-seq:%u, zcond:%2u(%s) [type: %u(%s)]\n"),
			start, len, wp - start,
			entry->reset, entry->non_seq,
			cond, zone_condition_str(cond),
			type, type_text[type]);
	}
}

static int do_report(int fd, uint64_t lba, uint32_t len, int verbose)
{
	int rc = -4;
	struct blk_zone_report *zi;

	zi = xmalloc(sizeof(struct blk_zone_report) + (len * sizeof(struct blk_zone)));
	zi->nr_zones = len;
	zi->sector = lba; /* maybe shift 4Kn -> 512e */
	rc = ioctl(fd, BLKREPORTZONE, zi);
	if (rc != -1) {
		if (verbose)
			printf(_("Found %d zones\n"), zi->nr_zones);
		print_zones(zi->zones, zi->nr_zones);
	} else {
		warn(_("ERR: %d -> %s"), errno, strerror(errno));
	}
	free(zi);

	return rc;
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] <device>\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Report zone information about the given device.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -z, --zone <offset>    start sector of first zone (in 512-byte sectors)\n"), out);
	fputs(_(" -c, --count <number>   maximum number of zones in the report\n"), out);
	fputs(_(" -v, --verbose          display the number of reported zones"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, USAGE_MAN_TAIL("blkreport(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

#define DEF_REPORT_LEN		(1 << 12) /* 4k zones (256k kzalloc) */
#define MAX_REPORT_LEN		(1 << 16) /* 64k zones */

int main(int argc, char **argv)
{
	char *path;
	int c;
	int fd;
	int secsize;
	uint64_t blksectors = 0;
	struct stat sb;
	int verbose = 0;
	uint64_t offset = 0ul;
	uint32_t length = DEF_REPORT_LEN;
	static const struct option longopts[] = {
	    { "help",      0, 0, 'h' },
	    { "version",   0, 0, 'V' },
	    { "zone",      1, 0, 'z' }, /* starting LBA */
	    { "count",     1, 0, 'c' }, /* max #of zones (entries) for result */
	    { "verbose",   0, 0, 'v' },
	    { NULL,        0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "hc:z:vV", longopts, NULL)) != -1) {
		switch(c) {
		case 'h':
			usage(stdout);
			break;
		case 'c':
			length = strtosize_or_err(optarg,
					_("failed to parse number of zones"));
			break;
		case 'z':
			offset = strtosize_or_err(optarg,
					_("failed to parse zone offset"));
			break;
		case 'v':
			verbose = 1;
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

	fd = open(path, O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), path);

	if (fstat(fd, &sb) == -1)
		err(EXIT_FAILURE, _("stat of %s failed"), path);
	if (!S_ISBLK(sb.st_mode))
		errx(EXIT_FAILURE, _("%s: not a block device"), path);

	if (blkdev_get_sectors(fd, (unsigned long long *) &blksectors))
		err(EXIT_FAILURE, _("%s: blkdev_get_sectors ioctl failed"), path);
	if (blkdev_get_sector_size(fd, &secsize))
		err(EXIT_FAILURE, _("%s: BLKSSZGET ioctl failed"), path);

	if (offset > blksectors)
		errx(EXIT_FAILURE, _("%s: offset is greater than device size"), path);
	if (length < 1)
		length = 1;
	if (length > MAX_REPORT_LEN) {
		length = MAX_REPORT_LEN;
		warnx(_("limiting report to %u entries"), length);
	}

	if (do_report(fd, offset, length, verbose))
		 err(EXIT_FAILURE, _("%s: BLKREPORTZONE ioctl failed"), path);

	close(fd);
	return EXIT_SUCCESS;
}
