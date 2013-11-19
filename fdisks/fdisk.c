/*
 * Copyright (C) 1992  A. V. Le Blanc (LeBlanc@mcc.ac.uk)
 * Copyright (C) 2012  Davidlohr Bueso <dave@gnu.org>
 *
 * Copyright (C) 2007-2013 Karel Zak <kzak@redhat.com>
 *
 * This program is free software.  You can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation: either version 1 or
 * (at your option) any later version.
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <limits.h>

#include "c.h"
#include "xalloc.h"
#include "all-io.h"
#include "nls.h"
#include "rpmatch.h"
#include "blkdev.h"
#include "mbsalign.h"
#include "fdisk.h"
#include "wholedisk.h"
#include "pathnames.h"
#include "canonicalize.h"
#include "strutils.h"
#include "closestream.h"

#include "pt-sun.h"		/* to toggle flags */

#ifdef HAVE_LINUX_COMPILER_H
# include <linux/compiler.h>
#endif
#ifdef HAVE_LINUX_BLKPG_H
# include <linux/blkpg.h>
#endif

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);

	fprintf(out,
	      _(" %1$s [options] <disk>      change partition table\n"
	        " %1$s [options] -l [<disk>] list partition table(s)\n"),
	       program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -b <size>         sector size (512, 1024, 2048 or 4096)\n"), out);
	fputs(_(" -c[=<mode>]       compatible mode: 'dos' or 'nondos' (default)\n"), out);
	fputs(_(" -h                print this help text\n"), out);
	fputs(_(" -c[=<mode>]       compatible mode: 'dos' or 'nondos' (default)\n"), out);
	fputs(_(" -L[=<when>]       colorize output (auto, always or never)\n"), out);
	fputs(_(" -t <type>         force fdisk to recognize specified partition table type only\n"), out);
	fputs(_(" -u[=<unit>]       display units: 'cylinders' or 'sectors' (default)\n"), out);
	fputs(_(" -v                print program version\n"), out);
	fputs(_(" -C <number>       specify the number of cylinders\n"), out);
	fputs(_(" -H <number>       specify the number of heads\n"), out);
	fputs(_(" -S <number>       specify the number of sectors per track\n"), out);

	fprintf(out, USAGE_MAN_TAIL("fdisk(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

void list_partition_types(struct fdisk_context *cxt)
{
	struct fdisk_parttype *types;
	size_t ntypes = 0;

	if (!cxt || !cxt->label || !cxt->label->parttypes)
		return;

	types = cxt->label->parttypes;
	ntypes = cxt->label->nparttypes;

	if (types[0].typestr == NULL) {
		/*
		 * Prints in 4 columns in format <hex> <name>
		 */
		size_t last[4], done = 0, next = 0, size;
		int i;

		size = ntypes;
		if (types[ntypes - 1].name == NULL)
			size--;

		for (i = 3; i >= 0; i--)
			last[3 - i] = done += (size + i - done) / (i + 1);
		i = done = 0;

		do {
			#define NAME_WIDTH 15
			char name[NAME_WIDTH * MB_LEN_MAX];
			size_t width = NAME_WIDTH;
			struct fdisk_parttype *t = &types[next];
			size_t ret;

			if (t->name) {
				printf("%c%2x  ", i ? ' ' : '\n', t->type);
				ret = mbsalign(_(t->name), name, sizeof(name),
						      &width, MBS_ALIGN_LEFT, 0);

				if (ret == (size_t)-1 || ret >= sizeof(name))
					printf("%-15.15s", _(t->name));
				else
					fputs(name, stdout);
			}

			next = last[i++] + done;
			if (i > 3 || next >= last[i]) {
				i = 0;
				next = ++done;
			}
		} while (done < last[0]);

	} else {
		/*
		 * Prints 1 column in format <idx> <name> <typestr>
		 */
		struct fdisk_parttype *t;
		size_t i;

		for (i = 0, t = types; t && i < ntypes; t++, i++) {
			if (t->name)
				printf("%3zu %-30s %s\n", i + 1,
						t->name, t->typestr);
		}
	}
	putchar('\n');
}

void toggle_dos_compatibility_flag(struct fdisk_context *cxt)
{
	struct fdisk_label *lb = fdisk_context_get_label(cxt, "dos");
	int flag;

	if (!lb)
		return;

	flag = !fdisk_dos_is_compatible(lb);
	fdisk_info(cxt, flag ?
			_("DOS Compatibility flag is set (DEPRECATED!)") :
			_("DOS Compatibility flag is not set"));

	fdisk_dos_enable_compatible(lb, flag);

	if (fdisk_is_disklabel(cxt, DOS))
		fdisk_reset_alignment(cxt);	/* reset the current label */
}

void change_partition_type(struct fdisk_context *cxt)
{
	size_t i;
	struct fdisk_parttype *t = NULL, *org_t = NULL;

	assert(cxt);
	assert(cxt->label);

	if (fdisk_ask_partnum(cxt, &i, FALSE))
		return;

	org_t = t = fdisk_get_partition_type(cxt, i);
	if (!t)
                fdisk_warnx(cxt, _("Partition %zu does not exist yet!"), i + 1);
        else {
		do {
			t = ask_partition_type(cxt);
		} while (!t);

		if (fdisk_set_partition_type(cxt, i, t) == 0)
			fdisk_sinfo(cxt, FDISK_INFO_SUCCESS,
				_("Changed type of partition '%s' to '%s'."),
				org_t ? org_t->name : _("Unknown"),
				    t ?     t->name : _("Unknown"));
		else
			fdisk_info(cxt,
				_("Type of partition %zu is unchanged: %s."),
				i + 1,
				org_t ? org_t->name : _("Unknown"));
        }

	fdisk_free_parttype(t);
	fdisk_free_parttype(org_t);
}

void list_disk_geometry(struct fdisk_context *cxt)
{
	char *id = NULL;
	uint64_t bytes = cxt->total_sectors * cxt->sector_size;
	char *strsz = size_to_human_string(SIZE_SUFFIX_SPACE
					   | SIZE_SUFFIX_3LETTER, bytes);

	fdisk_colon(cxt,	_("Disk %s: %s, %ju bytes, %ju sectors"),
			cxt->dev_path, strsz,
			bytes, (uintmax_t) cxt->total_sectors);
	free(strsz);

	if (fdisk_require_geometry(cxt) || fdisk_context_use_cylinders(cxt))
		fdisk_colon(cxt, _("Geometry: %d heads, %llu sectors/track, %llu cylinders"),
			       cxt->geom.heads, cxt->geom.sectors, cxt->geom.cylinders);

	fdisk_colon(cxt, _("Units: %s of %d * %ld = %ld bytes"),
	       fdisk_context_get_unit(cxt, PLURAL),
	       fdisk_context_get_units_per_sector(cxt),
	       cxt->sector_size,
	       fdisk_context_get_units_per_sector(cxt) * cxt->sector_size);

	fdisk_colon(cxt, _("Sector size (logical/physical): %lu bytes / %lu bytes"),
				cxt->sector_size, cxt->phy_sector_size);
	fdisk_colon(cxt, _("I/O size (minimum/optimal): %lu bytes / %lu bytes"),
				cxt->min_io_size, cxt->io_size);
	if (cxt->alignment_offset)
		fdisk_colon(cxt, _("Alignment offset: %lu bytes"),
				cxt->alignment_offset);
	if (fdisk_dev_has_disklabel(cxt))
		fdisk_colon(cxt, _("Disklabel type: %s"), cxt->label->name);

	if (fdisk_get_disklabel_id(cxt, &id) == 0 && id)
		fdisk_colon(cxt, _("Disk identifier: %s"), id);
}

static size_t skip_empty(const unsigned char *buf, size_t i, size_t sz)
{
	size_t next;
	const unsigned char *p0 = buf + i;

	for (next = i + 16; next < sz; next += 16) {
		if (memcmp(p0, buf + next, 16) != 0)
			break;
	}

	return next == i + 16 ? i : next;
}

static void dump_buffer(off_t base, unsigned char *buf, size_t sz, int all)
{
	size_t i, l, next = 0;

	if (!buf)
		return;
	for (i = 0, l = 0; i < sz; i++, l++) {
		if (l == 0) {
			if (all == 0 && !next)
				next = skip_empty(buf, i, sz);
			printf("%08jx ", base + i);
		}
		printf(" %02x", buf[i]);
		if (l == 7)				/* words separator */
			fputs(" ", stdout);
		else if (l == 15) {
			fputc('\n', stdout);		/* next line */
			l = -1;
			if (next > i) {
				printf("*\n");
				i = next - 1;
			}
			next = 0;
		}
	}
	if (l > 0)
		printf("\n");
}

static void dump_blkdev(struct fdisk_context *cxt, const char *name,
			off_t offset, size_t size, int all)
{
	fdisk_colon(cxt, _("\n%s: offset = %ju, size = %zu bytes."),
			name, offset, size);

	if (lseek(cxt->dev_fd, offset, SEEK_SET) == (off_t) -1)
		fdisk_warn(cxt, _("cannot seek"));
	else {
		unsigned char *buf = xmalloc(size);

		if (read_all(cxt->dev_fd, (char *) buf, size) != (ssize_t) size)
			fdisk_warn(cxt, _("cannot read"));
		else
			dump_buffer(offset, buf, size, all);
		free(buf);
	}
}

void dump_firstsector(struct fdisk_context *cxt)
{
	int all = !isatty(STDOUT_FILENO);

	assert(cxt);
	assert(cxt->label);

	dump_blkdev(cxt, _("First sector"), 0, cxt->sector_size, all);
}

void dump_disklabel(struct fdisk_context *cxt)
{
	int all = !isatty(STDOUT_FILENO);
	int i = 0;
	const char *name = NULL;
	off_t offset = 0;
	size_t size = 0;

	assert(cxt);
	assert(cxt->label);

	while (fdisk_locate_disklabel(cxt, i++, &name, &offset, &size) == 0 && size)
		dump_blkdev(cxt, name, offset, size, all);
}

static int is_ide_cdrom_or_tape(char *device)
{
	int fd, ret;

	if ((fd = open(device, O_RDONLY)) < 0)
		return 0;
	ret = blkdev_is_cdrom(fd);

	close(fd);
	return ret;
}

static void print_device_pt(struct fdisk_context *cxt, char *device)
{
	if (fdisk_context_assign_device(cxt, device, 1) != 0)	/* read-only */
		err(EXIT_FAILURE, _("cannot open %s"), device);

	list_disk_geometry(cxt);

	if (fdisk_dev_has_disklabel(cxt))
		fdisk_list_disklabel(cxt);
	fputc('\n', stdout);
}

static void print_all_devices_pt(struct fdisk_context *cxt)
{
	FILE *f;
	char line[128 + 1];

	f = fopen(_PATH_PROC_PARTITIONS, "r");
	if (!f) {
		warn(_("cannot open %s"), _PATH_PROC_PARTITIONS);
		return;
	}

	DBG(FRONTEND, dbgprint("reading "_PATH_PROC_PARTITIONS));

	while (fgets(line, sizeof(line), f)) {
		char ptname[128 + 1], devname[256];

		if (sscanf(line, " %*d %*d %*d %128[^\n ]", ptname) != 1)
			continue;

		snprintf(devname, sizeof(devname), "/dev/%s", ptname);

		DBG(FRONTEND, dbgprint("listing %s", devname));

		if (is_whole_disk(devname)) {
			char *cn = canonicalize_path(devname);
			if (cn) {
				if (!is_ide_cdrom_or_tape(cn))
					print_device_pt(cxt, cn);
				free(cn);
			}
		}
	}
	fclose(f);
}

static sector_t get_dev_blocks(char *dev)
{
	int fd;
	sector_t size;

	if ((fd = open(dev, O_RDONLY)) < 0)
		err(EXIT_FAILURE, _("cannot open %s"), dev);
	if (blkdev_get_sectors(fd, &size) == -1) {
		close(fd);
		err(EXIT_FAILURE, _("BLKGETSIZE ioctl failed on %s"), dev);
	}
	close(fd);
	return size/2;
}

enum {
	ACT_FDISK = 0,	/* default */
	ACT_LIST,
	ACT_SHOWSIZE
};

int main(int argc, char **argv)
{
	int i, c, act = ACT_FDISK;
	int colormode = UL_COLORMODE_AUTO;
	struct fdisk_context *cxt;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	fdisk_init_debug(0);
	cxt = fdisk_new_context();
	if (!cxt)
		err(EXIT_FAILURE, _("failed to allocate libfdisk context"));

	fdisk_context_set_ask(cxt, ask_callback, NULL);

	while ((c = getopt(argc, argv, "b:c::C:hH:lL::sS:t:u::vV")) != -1) {
		switch (c) {
		case 'b':
		{
			size_t sz = strtou32_or_err(optarg,
					_("invalid sector size argument"));
			if (sz != 512 && sz != 1024 && sz != 2048 && sz != 4096)
				usage(stderr);
			fdisk_save_user_sector_size(cxt, sz, sz);
			break;
		}
		case 'C':
			fdisk_save_user_geometry(cxt,
				strtou32_or_err(optarg,
						_("invalid cylinders argument")),
				0, 0);
			break;
		case 'c':
			if (optarg) {
				/* this setting is independent on the current
				 * actively used label */
				struct fdisk_label *lb = fdisk_context_get_label(cxt, "dos");
				if (!lb)
					err(EXIT_FAILURE, _("not found DOS label driver"));
				if (strcmp(optarg, "=dos") == 0)
					fdisk_dos_enable_compatible(lb, TRUE);
				else if (strcmp(optarg, "=nondos") == 0)
					fdisk_dos_enable_compatible(lb, FALSE);
				else
					usage(stderr);
			}
			/* use default if no optarg specified */
			break;
		case 'H':
			fdisk_save_user_geometry(cxt, 0,
				strtou32_or_err(optarg,
						_("invalid heads argument")),
				0);
			break;
		case 'S':
			fdisk_save_user_geometry(cxt, 0, 0,
				strtou32_or_err(optarg,
					_("invalid sectors argument")));
			break;
		case 'l':
			act = ACT_LIST;
			break;
		case 'L':
			if (optarg)
				colormode = colormode_or_err(optarg,
						_("unsupported color mode"));
			break;
		case 's':
			act = ACT_SHOWSIZE;
			break;
		case 't':
		{
			struct fdisk_label *lb = NULL;

			while (fdisk_context_next_label(cxt, &lb) == 0)
				fdisk_label_set_disabled(lb, 1);

			lb = fdisk_context_get_label(cxt, optarg);
			if (!lb)
				errx(EXIT_FAILURE, _("unsupported disklabel: %s"), optarg);
			fdisk_label_set_disabled(lb, 0);
		}
		case 'u':
			if (optarg && *optarg == '=')
				optarg++;
			if (fdisk_context_set_unit(cxt, optarg) != 0)
				usage(stderr);
			break;
		case 'V':
		case 'v':
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage(stdout);
		default:
			usage(stderr);
		}
	}

	if (argc-optind != 1 && fdisk_has_user_device_properties(cxt))
		warnx(_("The device properties (sector size and geometry) should"
			" be used with one specified device only."));

	colors_init(colormode);

	switch (act) {
	case ACT_LIST:
		fdisk_context_enable_listonly(cxt, 1);

		if (argc > optind) {
			int k;
			for (k = optind; k < argc; k++)
				print_device_pt(cxt, argv[k]);
		} else
			print_all_devices_pt(cxt);
		break;

	case ACT_SHOWSIZE:
		/* deprecated */
		if (argc - optind <= 0)
			usage(stderr);

		for (i = optind; i < argc; i++) {
			if (argc - optind == 1)
				printf("%llu\n", get_dev_blocks(argv[i]));
			else
				printf("%s: %llu\n", argv[i], get_dev_blocks(argv[i]));
		}
		break;

	case ACT_FDISK:
		if (argc-optind != 1)
			usage(stderr);

		if (fdisk_context_assign_device(cxt, argv[optind], 0) != 0)
			err(EXIT_FAILURE, _("cannot open %s"), argv[optind]);

		/* Here starts interactive mode, use fdisk_{warn,info,..} functions */
		color_enable(UL_COLOR_GREEN);
		fdisk_info(cxt, _("Welcome to fdisk (%s)."), PACKAGE_STRING);
		color_disable();
		fdisk_info(cxt, _("Changes will remain in memory only, until you decide to write them.\n"
				  "Be careful before using the write command.\n"));
		fflush(stdout);

		if (!fdisk_dev_has_disklabel(cxt)) {
			fdisk_warnx(cxt, _("Device does not contain a recognized partition table."));
			fdisk_create_disklabel(cxt, NULL);
		}

		while (1)
			process_fdisk_menu(&cxt);
	}

	fdisk_free_context(cxt);
	return EXIT_SUCCESS;
}
