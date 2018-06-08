/*
 * blkzone.c -- the block device zone commands
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
#include <blkid.h>

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
#include "sysfs.h"
#include "optutils.h"

struct blkzone_control;

static int blkzone_report(struct blkzone_control *ctl);
static int blkzone_reset(struct blkzone_control *ctl);

struct blkzone_command {
	const char *name;
	int (*handler)(struct blkzone_control *);
	const char *help;
};

struct blkzone_control {
	const char *devname;
	const struct blkzone_command *command;

	uint64_t total_sectors;
	int secsize;

	uint64_t offset;
	uint64_t length;
	uint32_t count;

	unsigned int verbose : 1;
};

static const struct blkzone_command commands[] = {
	{ "report",	blkzone_report, N_("Report zone information about the given device") },
	{ "reset",	blkzone_reset,  N_("Reset a range of zones.") }
};

static const struct blkzone_command *name_to_command(const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(commands[i].name, name) == 0)
			return &commands[i];
	}

	return NULL;
}

static int init_device(struct blkzone_control *ctl, int mode)
{
	struct stat sb;
	int fd;

	fd = open(ctl->devname, mode);
	if (fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), ctl->devname);

	if (fstat(fd, &sb) == -1)
		err(EXIT_FAILURE, _("stat of %s failed"), ctl->devname);
	if (!S_ISBLK(sb.st_mode))
		errx(EXIT_FAILURE, _("%s: not a block device"), ctl->devname);

	if (blkdev_get_sectors(fd, (unsigned long long *) &ctl->total_sectors))
		err(EXIT_FAILURE, _("%s: blkdev_get_sectors ioctl failed"), ctl->devname);

	if (blkdev_get_sector_size(fd, &ctl->secsize))
		err(EXIT_FAILURE, _("%s: BLKSSZGET ioctl failed"), ctl->devname);

	return fd;
}

/*
 * Get the device zone size indicated by chunk sectors).
 */
static unsigned long blkdev_chunk_sectors(const char *dname)
{
	struct sysfs_cxt cxt = UL_SYSFSCXT_EMPTY;
	blkid_probe pr;
	dev_t devno;
	uint64_t sz;
	int rc;

	pr = blkid_new_probe_from_filename(dname);
	if (!pr)
		return -1;

	devno = blkid_probe_get_wholedisk_devno(pr);
	if (sysfs_init(&cxt, devno, NULL)) {
		blkid_free_probe(pr);
		return 0;
	}

	rc = sysfs_read_u64(&cxt, "queue/chunk_sectors", &sz);
	sysfs_deinit(&cxt);
	blkid_free_probe(pr);
	return rc == 0 ? sz : 0;
}

/*
 * blkzone report
 */
#define DEF_REPORT_LEN		(1U << 12) /* 4k zones per report (256k kzalloc) */

static const char *type_text[] = {
	"RESERVED",
	"CONVENTIONAL",
	"SEQ_WRITE_REQUIRED",
	"SEQ_WRITE_PREFERRED",
};

static const char *condition_str[] = {
	"nw", /* Not write pointer */
	"em", /* Empty */
	"oi", /* Implicitly opened */
	"oe", /* Explicitly opened */
	"cl", /* Closed */
	"x5", "x6", "x7", "x8", "x9", "xA", "xB", "xC", /* xN: reserved */
	"ro", /* Read only */
	"fu", /* Full */
	"of"  /* Offline */
};

static int blkzone_report(struct blkzone_control *ctl)
{
	struct blk_zone_report *zi;
	unsigned long zonesize;
	uint32_t i, nr_zones;
	int fd;

	fd = init_device(ctl, O_RDONLY);

	if (ctl->offset > ctl->total_sectors)
		errx(EXIT_FAILURE, _("%s: offset is greater than device size"), ctl->devname);

	zonesize = blkdev_chunk_sectors(ctl->devname);
	if (!zonesize)
		errx(EXIT_FAILURE, _("%s: unable to determine zone size"), ctl->devname);

	if (ctl->count)
		nr_zones = ctl->count;
	else if (ctl->length)
		nr_zones = (ctl->length + zonesize - 1) / zonesize;
	else
		nr_zones = 1 + (ctl->total_sectors - ctl->offset) / zonesize;

	zi = xmalloc(sizeof(struct blk_zone_report) +
		     (DEF_REPORT_LEN * sizeof(struct blk_zone)));

	while (nr_zones && ctl->offset < ctl->total_sectors) {

		zi->nr_zones = min(nr_zones, DEF_REPORT_LEN);
		zi->sector = ctl->offset;

		if (ioctl(fd, BLKREPORTZONE, zi) == -1)
			err(EXIT_FAILURE, _("%s: BLKREPORTZONE ioctl failed"), ctl->devname);

		if (ctl->verbose)
			printf(_("Found %d zones from 0x%"PRIx64"\n"),
				zi->nr_zones, ctl->offset);

		if (!zi->nr_zones) {
			nr_zones = 0;
			break;
		}

		for (i = 0; i < zi->nr_zones; i++) {
			const struct blk_zone *entry = &zi->zones[i];
			unsigned int type = entry->type;
			uint64_t start = entry->start;
			uint64_t wp = entry->wp;
			uint8_t cond = entry->cond;
			uint64_t len = entry->len;

			if (!len) {
				nr_zones = 0;
				break;
			}

			printf(_("  start: 0x%09"PRIx64", len 0x%06"PRIx64", wptr 0x%06"PRIx64
			 	" reset:%u non-seq:%u, zcond:%2u(%s) [type: %u(%s)]\n"),
				start, len, (type == 0x1) ? 0 : wp - start,
				entry->reset, entry->non_seq,
				cond, condition_str[cond & (ARRAY_SIZE(condition_str) - 1)],
				type, type_text[type]);

			nr_zones--;
			ctl->offset = start + len;

		}

	}

	free(zi);
	close(fd);

	return 0;
}

/*
 * blkzone reset
 */
static int blkzone_reset(struct blkzone_control *ctl)
{
	struct blk_zone_range za = { .sector = 0 };
	unsigned long zonesize;
	uint64_t zlen;
	int fd;

	zonesize = blkdev_chunk_sectors(ctl->devname);
	if (!zonesize)
		errx(EXIT_FAILURE, _("%s: unable to determine zone size"), ctl->devname);

	fd = init_device(ctl, O_WRONLY);

	if (ctl->offset & (zonesize - 1))
		errx(EXIT_FAILURE, _("%s: offset %" PRIu64 " is not aligned "
			"to zone size %lu"),
			ctl->devname, ctl->offset, zonesize);

	if (ctl->offset > ctl->total_sectors)
		errx(EXIT_FAILURE, _("%s: offset is greater than device size"), ctl->devname);

	if (ctl->count)
		zlen = ctl->count * zonesize;
	else if (ctl->length)
		zlen = ctl->length;
	else
		zlen = ctl->total_sectors;
	if (ctl->offset + zlen > ctl->total_sectors)
		zlen = ctl->total_sectors - ctl->offset;

	if (ctl->length &&
	    (zlen & (zonesize - 1)) &&
	    ctl->offset + zlen != ctl->total_sectors)
		errx(EXIT_FAILURE, _("%s: number of sectors %" PRIu64 " is not aligned "
			"to zone size %lu"),
			ctl->devname, ctl->length, zonesize);

	za.sector = ctl->offset;
	za.nr_sectors = zlen;

	if (ioctl(fd, BLKRESETZONE, &za) == -1)
		err(EXIT_FAILURE, _("%s: BLKRESETZONE ioctl failed"), ctl->devname);
	else if (ctl->verbose)
		printf(_("%s: successfully reset in range from %" PRIu64 ", to %" PRIu64),
			ctl->devname,
			ctl->offset,
			ctl->offset + zlen);
	close(fd);
	return 0;
}

static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	size_t i;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s <command> [options] <device>\n"), program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Run zone command on the given block device.\n"), out);

	fputs(USAGE_COMMANDS, out);
	for (i = 0; i < ARRAY_SIZE(commands); i++)
		fprintf(out, " %-11s  %s\n", commands[i].name, _(commands[i].help));

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -o, --offset <sector>  start sector of zone to act (in 512-byte sectors)\n"), out);
	fputs(_(" -l, --length <sectors> maximum sectors to act (in 512-byte sectors)\n"), out);
	fputs(_(" -c, --count <number>   maximum number of zones\n"), out);
	fputs(_(" -v, --verbose          display more details\n"), out);
	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(24));

	printf(USAGE_MAN_TAIL("blkzone(8)"));
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int c;
	struct blkzone_control ctl = {
		.devname = NULL,
		.offset = 0,
		.count = 0,
		.length = 0
	};

	static const struct option longopts[] = {
	    { "help",    no_argument,       NULL, 'h' },
	    { "count",   required_argument, NULL, 'c' }, /* max #of zones to operate on */
	    { "length",  required_argument, NULL, 'l' }, /* max of sectors to operate on */
	    { "offset",  required_argument, NULL, 'o' }, /* starting LBA */
	    { "verbose", no_argument,       NULL, 'v' },
	    { "version", no_argument,       NULL, 'V' },
	    { NULL, 0, NULL, 0 }
	};
	static const ul_excl_t excl[] = {       /* rows and cols in ASCII order */
		{ 'c', 'l' },
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;


	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	if (argc >= 2 && *argv[1] != '-') {
		ctl.command = name_to_command(argv[1]);
		if (!ctl.command)
			errx(EXIT_FAILURE, _("%s is not valid command name"), argv[1]);
		argv++;
		argc--;
	}

	while ((c = getopt_long(argc, argv, "hc:l:o:vV", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch (c) {
		case 'h':
			usage();
			break;
		case 'c':
			ctl.count = strtou32_or_err(optarg,
					_("failed to parse number of zones"));
			break;
		case 'l':
			ctl.length = strtosize_or_err(optarg,
					_("failed to parse number of sectors"));
			break;
		case 'o':
			ctl.offset = strtosize_or_err(optarg,
					_("failed to parse zone offset"));
			break;
		case 'v':
			ctl.verbose = 1;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		default:
			errtryhelp(EXIT_FAILURE);
		}
	}

	if (!ctl.command)
		errx(EXIT_FAILURE, _("no command specified"));

	if (optind == argc)
		errx(EXIT_FAILURE, _("no device specified"));
	ctl.devname = argv[optind++];

	if (optind != argc)
		errx(EXIT_FAILURE,_("unexpected number of arguments"));

	if (ctl.command->handler(&ctl) < 0)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;

}
