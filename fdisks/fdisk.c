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
#include "nls.h"
#include "rpmatch.h"
#include "blkdev.h"
#include "common.h"
#include "mbsalign.h"
#include "fdisk.h"
#include "wholedisk.h"
#include "pathnames.h"
#include "canonicalize.h"
#include "strutils.h"
#include "closestream.h"

#include "pt-sun.h"		/* to toggle flags */
#include "fdiskdoslabel.h"
#include "fdiskbsdlabel.h"

#ifdef HAVE_LINUX_COMPILER_H
# include <linux/compiler.h>
#endif
#ifdef HAVE_LINUX_BLKPG_H
# include <linux/blkpg.h>
#endif

int	nowarn = 0;			/* no warnings for fdisk -l/-s */

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);

	fprintf(out,
	      _(" %1$s [options] <disk>    change partition table\n"
	        " %1$s [options] -l <disk> list partition table(s)\n"
	        " %1$s -s <partition>      give partition size(s) in blocks\n"),
	       program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -b <size>         sector size (512, 1024, 2048 or 4096)\n"), out);
	fputs(_(" -c[=<mode>]       compatible mode: 'dos' or 'nondos' (default)\n"), out);
	fputs(_(" -h                print this help text\n"), out);
	fputs(_(" -u[=<unit>]       display units: 'cylinders' or 'sectors' (default)\n"), out);
	fputs(_(" -v                print program version\n"), out);
	fputs(_(" -C <number>       specify the number of cylinders\n"), out);
	fputs(_(" -H <number>       specify the number of heads\n"), out);
	fputs(_(" -S <number>       specify the number of sectors per track\n"), out);

	fprintf(out, USAGE_MAN_TAIL("fdisk(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

void toggle_units(struct fdisk_context *cxt)
{
	fdisk_context_set_unit(cxt,
		fdisk_context_use_cylinders(cxt) ? "sectors" :
						   "cylinders");
	if (fdisk_context_use_cylinders(cxt))
		fdisk_info(cxt, _("Changing display/entry units to cylinders (DEPRECATED!)."));
	else
		fdisk_info(cxt, _("Changing display/entry units to sectors."));
}

struct partition *
get_part_table(int i) {
	return ptes[i].part_table;
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

static int
test_c(char **m, char *mesg) {
	int val = 0;
	if (!*m)
		fprintf(stderr, _("You must set"));
	else {
		fprintf(stderr, " %s", *m);
		val = 1;
	}
	*m = mesg;
	return val;
}

int warn_geometry(struct fdisk_context *cxt)
{
	char *m = NULL;
	int prev = 0;

	if (fdisk_is_disklabel(cxt, SGI)) /* cannot set cylinders etc anyway */
		return 0;
	if (!cxt->geom.heads)
		prev = test_c(&m, _("heads"));
	if (!cxt->geom.sectors)
		prev = test_c(&m, _("sectors"));
	if (!cxt->geom.cylinders)
		prev = test_c(&m, _("cylinders"));
	if (!m)
		return 0;
	fprintf(stderr,
		_("%s%s.\nYou can do this from the extra functions menu.\n"),
		prev ? _(" and ") : " ", m);
	return 1;
}

void warn_limits(struct fdisk_context *cxt)
{
	if (cxt->total_sectors > UINT_MAX && !nowarn) {
		unsigned long long bytes = cxt->total_sectors * cxt->sector_size;
		int giga = bytes / 1000000000;
		int hectogiga = (giga + 50) / 100;

		fprintf(stderr, _("\n"
"WARNING: The size of this disk is %d.%d TB (%llu bytes).\n"
"DOS partition table format can not be used on drives for volumes\n"
"larger than (%llu bytes) for %ld-byte sectors. Use parted(1) and GUID \n"
"partition table format (GPT).\n\n"),
			hectogiga / 10, hectogiga % 10,
			bytes,
			(sector_t ) UINT_MAX * cxt->sector_size,
			cxt->sector_size);
	}
}

static void toggle_dos_compatibility_flag(struct fdisk_context *cxt)
{
	struct fdisk_label *lb = fdisk_context_get_label(cxt, "dos");
	int flag;

	if (!lb)
		return;

	flag = !fdisk_dos_is_compatible(lb);

	if (flag)
		printf(_("DOS Compatibility flag is set (DEPRECATED!)\n"));
	else
		printf(_("DOS Compatibility flag is not set\n"));

	fdisk_dos_enable_compatible(lb, flag);

	if (fdisk_is_disklabel(cxt, DOS))
		fdisk_reset_alignment(cxt);
}

static void delete_partition(struct fdisk_context *cxt, int partnum)
{
	if (partnum < 0 || warn_geometry(cxt))
		return;

	if (fdisk_delete_partition(cxt, partnum) != 0)
		printf(_("Could not delete partition %d\n"), partnum + 1);
	else
		printf(_("Partition %d is deleted\n"), partnum + 1);
}

static void change_partition_type(struct fdisk_context *cxt)
{
	size_t i;
	struct fdisk_parttype *t, *org_t;

	assert(cxt);
	assert(cxt->label);

	if (fdisk_ask_partnum(cxt, &i, FALSE))
		return;

	org_t = t = fdisk_get_partition_type(cxt, i);
	if (!t)
                printf(_("Partition %zu does not exist yet!\n"), i + 1);

        else do {
		t = ask_partition_type(cxt);
		if (!t)
			continue;

		if (fdisk_set_partition_type(cxt, i, t) == 0) {
			printf (_("Changed type of partition '%s' to '%s'\n"),
				org_t ? org_t->name : _("Unknown"),
				    t ?     t->name : _("Unknown"));
		} else {
			printf (_("Type of partition %zu is unchanged: %s\n"),
				i + 1,
				org_t ? org_t->name : _("Unknown"));
		}
		break;
        } while (1);

	fdisk_free_parttype(t);
	fdisk_free_parttype(org_t);
}

static void
list_disk_geometry(struct fdisk_context *cxt) {
	unsigned long long bytes = cxt->total_sectors * cxt->sector_size;
	long megabytes = bytes/1000000;

	if (megabytes < 10000)
		printf(_("\nDisk %s: %ld MB, %lld bytes"),
		       cxt->dev_path, megabytes, bytes);
	else {
		long hectomega = (megabytes + 50) / 100;
		printf(_("\nDisk %s: %ld.%ld GB, %llu bytes"),
		       cxt->dev_path, hectomega / 10, hectomega % 10, bytes);
	}
	printf(_(", %llu sectors\n"), cxt->total_sectors);

	if (is_dos_compatible(cxt)
	    || fdisk_is_disklabel(cxt, SUN)
	    || fdisk_is_disklabel(cxt, SGI))
		printf(_("Geometry: %d heads, %llu sectors/track, %llu cylinders\n"),
		       cxt->geom.heads, cxt->geom.sectors, cxt->geom.cylinders);

	printf(_("Units = %s of %d * %ld = %ld bytes\n"),
	       fdisk_context_get_unit(cxt, PLURAL),
	       fdisk_context_get_units_per_sector(cxt),
	       cxt->sector_size,
	       fdisk_context_get_units_per_sector(cxt) * cxt->sector_size);

	printf(_("Sector size (logical/physical): %lu bytes / %lu bytes\n"),
				cxt->sector_size, cxt->phy_sector_size);
	printf(_("I/O size (minimum/optimal): %lu bytes / %lu bytes\n"),
				cxt->min_io_size, cxt->io_size);
	if (cxt->alignment_offset)
		printf(_("Alignment offset: %lu bytes\n"), cxt->alignment_offset);
	if (fdisk_dev_has_disklabel(cxt))
		printf(_("Disk label type: %s\n"), cxt->label->name);
	if (fdisk_is_disklabel(cxt, DOS))
		dos_print_mbr_id(cxt);
	printf("\n");
}

static void list_table(struct fdisk_context *cxt, int xtra)
{
	list_disk_geometry(cxt);

	if (fdisk_is_disklabel(cxt, OSF))
		xbsd_print_disklabel(cxt, xtra);
	else if (fdisk_is_disklabel(cxt, DOS))
		dos_list_table(cxt, xtra);
	else
		fdisk_list_disklabel(cxt);

}

static void verify(struct fdisk_context *cxt)
{
	if (warn_geometry(cxt))
		return;

	fdisk_verify_disklabel(cxt);
}

static void new_partition(struct fdisk_context *cxt)
{
	assert(cxt);
	assert(cxt->label);

	if (warn_geometry(cxt))
		return;

	fdisk_add_partition(cxt, NULL);
}

static void write_table(struct fdisk_context *cxt)
{
	int rc;

	rc = fdisk_write_disklabel(cxt);
	if (rc)
		err(EXIT_FAILURE, _("cannot write disk label"));

	printf(_("The partition table has been altered!\n\n"));
	reread_partition_table(cxt, 1);
}

void
reread_partition_table(struct fdisk_context *cxt, int leave) {
	int i;
	struct stat statbuf;

	i = fstat(cxt->dev_fd, &statbuf);
	if (i == 0 && S_ISBLK(statbuf.st_mode)) {
		sync();
#ifdef BLKRRPART
		printf(_("Calling ioctl() to re-read partition table.\n"));
		i = ioctl(cxt->dev_fd, BLKRRPART);
#else
		errno = ENOSYS;
		i = 1;
#endif
        }

	if (i) {
		printf(_("\nWARNING: Re-reading the partition table failed with error %d: %m.\n"
			 "The kernel still uses the old table. The new table will be used at\n"
			 "the next reboot or after you run partprobe(8) or kpartx(8)\n"),
			errno);
	}

	if (leave) {
		if (fsync(cxt->dev_fd) || close(cxt->dev_fd)) {
			fprintf(stderr, _("\nError closing file\n"));
			exit(1);
		}

		printf(_("Syncing disks.\n"));
		sync();
		exit(!!i);
	}
}

#define MAX_PER_LINE	16
static void
print_buffer(struct fdisk_context *cxt, unsigned char pbuffer[]) {
	unsigned int i, l;

	for (i = 0, l = 0; i < cxt->sector_size; i++, l++) {
		if (l == 0)
			printf("0x%03X:", i);
		printf(" %02X", pbuffer[i]);
		if (l == MAX_PER_LINE - 1) {
			printf("\n");
			l = -1;
		}
	}
	if (l > 0)
		printf("\n");
	printf("\n");
}

static void print_raw(struct fdisk_context *cxt)
{
	size_t i;

	assert(cxt);
	assert(cxt->label);

	printf(_("Device: %s\n"), cxt->dev_path);
	if (fdisk_is_disklabel(cxt, SUN) ||
	    fdisk_is_disklabel(cxt, SGI) ||
	    fdisk_is_disklabel(cxt, GPT))
		print_buffer(cxt, cxt->firstsector);

	else if (fdisk_is_disklabel(cxt, DOS)) {
		for (i = 3; i < cxt->label->nparts_max; i++)
		     print_buffer(cxt, ptes[i].sectorbuffer);
	}
}

static void __attribute__ ((__noreturn__)) handle_quit(struct fdisk_context *cxt)
{
	fdisk_free_context(cxt);
	printf("\n");
	exit(EXIT_SUCCESS);
}

static void
expert_command_prompt(struct fdisk_context *cxt)
{
	char c;
	size_t n;

	assert(cxt);

	fdisk_context_enable_details(cxt, 1);

	while(1) {
		assert(cxt->label);

		c = process_fdisk_menu(cxt);
		if (c <= 0)
			continue;

		/* well, process_fdisk_menu() returns commands that
		 * are not yet implemented by menu callbacks. Let's
		 * perform the commands here */
		switch (c) {
		case 'b':
			if (fdisk_is_disklabel(cxt, DOS) &&
			    fdisk_ask_partnum(cxt, &n, FALSE) == 0)
				dos_move_begin(cxt, n);
			break;
		case 'd':
			print_raw(cxt);
			break;
		case 'e':
			if (fdisk_is_disklabel(cxt, DOS))
				dos_list_table_expert(cxt, 1);
			break;
		case 'f':
			if (fdisk_is_disklabel(cxt, DOS))
				dos_fix_partition_table_order(cxt);
			break;
		case 'g':
			/* Deprecated, use 'G' in main menu, just for backward
			 * compatibility only. */
			fdisk_create_disklabel(cxt, "sgi");
			break;
		case 'i':
			if (fdisk_is_disklabel(cxt, DOS))
				dos_set_mbr_id(cxt);
			break;
		case 'p':
			list_table(cxt, 1);
			break;
		case 'q':
			handle_quit(cxt);
		case 'r':
			fdisk_context_enable_details(cxt, 0);
			return;
		case 'v':
			verify(cxt);
			break;
		case 'w':
			write_table(cxt);
			break;
		}
	}
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

/* Print disk geometry and partition table of a specified device (-l option) */
static void print_partition_table_from_option(
			struct fdisk_context *cxt,
			char *device)
{
	if (fdisk_context_assign_device(cxt, device, 1) != 0)	/* read-only */
		err(EXIT_FAILURE, _("cannot open %s"), device);

	if (fdisk_dev_has_disklabel(cxt))
		list_table(cxt, 0);
	else
		list_disk_geometry(cxt);
}

/*
 * for fdisk -l:
 * try all things in /proc/partitions that look like a full disk
 */
static void
print_all_partition_table_from_option(struct fdisk_context *cxt)
{
	FILE *procpt;
	char line[128 + 1], ptname[128 + 1], devname[256];
	int ma, mi;
	unsigned long long sz;

	procpt = fopen(_PATH_PROC_PARTITIONS, "r");
	if (procpt == NULL) {
		fprintf(stderr, _("cannot open %s\n"), _PATH_PROC_PARTITIONS);
		return;
	}

	while (fgets(line, sizeof(line), procpt)) {
		if (sscanf (line, " %d %d %llu %128[^\n ]",
			    &ma, &mi, &sz, ptname) != 4)
			continue;
		snprintf(devname, sizeof(devname), "/dev/%s", ptname);
		if (is_whole_disk(devname)) {
			char *cn = canonicalize_path(devname);
			if (cn) {
				if (!is_ide_cdrom_or_tape(cn))
					print_partition_table_from_option(cxt, cn);
				free(cn);
			}
		}
	}
	fclose(procpt);
}

static void
unknown_command(int c) {
	printf(_("%c: unknown command\n"), c);
}

static void print_welcome(void)
{
	printf(_("Welcome to fdisk (%s).\n\n"
		 "Changes will remain in memory only, until you decide to write them.\n"
		 "Be careful before using the write command.\n\n"), PACKAGE_STRING);

	fflush(stdout);
}

static void command_prompt(struct fdisk_context *cxt)
{
	int c;
	size_t n;

	assert(cxt);

	if (fdisk_is_disklabel(cxt, OSF)) {
		putchar('\n');
		/* OSF label, and no DOS label */
		printf(_("Detected an OSF/1 disklabel on %s, entering "
			 "disklabel mode.\n"),
		       cxt->dev_path);
		bsd_command_prompt(cxt);

		/* If we return we may want to make an empty DOS label? */
		fdisk_context_switch_label(cxt, "dos");
	}

	while (1) {
		assert(cxt->label);

		c = process_fdisk_menu(cxt);
		if (c <= 0)
			continue;

		/* well, process_fdisk_menu() returns commands that
		 * are not yet implemented by menu callbacks. Let's
		 * perform the commands here */
		switch (c) {
		case 'a':
			if (fdisk_is_disklabel(cxt, DOS) &&
			    fdisk_ask_partnum(cxt, &n, FALSE) == 0)
				fdisk_partition_toggle_flag(cxt, n, DOS_FLAG_ACTIVE);

			else if (fdisk_is_disklabel(cxt, SGI) &&
				 fdisk_ask_partnum(cxt, &n, FALSE) == 0)
				fdisk_partition_toggle_flag(cxt, n, SGI_FLAG_BOOT);
			else
				unknown_command(c);
			break;
		case 'b':
			if (fdisk_is_disklabel(cxt, SGI))
				sgi_set_bootfile(cxt);
			else if (fdisk_is_disklabel(cxt, DOS)) {

				struct fdisk_context *bsd;

				bsd = fdisk_new_nested_context(cxt, "bsd");
				if (bsd)
					bsd_command_prompt(bsd);
				fdisk_free_context(bsd);
			} else
				unknown_command(c);
			break;
		case 'c':
			if (fdisk_is_disklabel(cxt, DOS))
				toggle_dos_compatibility_flag(cxt);

			else if (fdisk_is_disklabel(cxt, SGI) &&
				 fdisk_ask_partnum(cxt, &n, FALSE) == 0)
				fdisk_partition_toggle_flag(cxt, n, SGI_FLAG_SWAP);
			else
				unknown_command(c);
			break;
		case 'd':
			if (fdisk_ask_partnum(cxt, &n, FALSE) == 0)
				delete_partition(cxt, n);
			break;
		case 'g':
			fdisk_create_disklabel(cxt, "gpt");
			break;
		case 'G':
			fdisk_create_disklabel(cxt, "sgi");
			break;
		case 'i':
			if (fdisk_is_disklabel(cxt, SGI))
				sgi_create_info(cxt);
			else
				unknown_command(c);
			break;
		case 'l':
			list_partition_types(cxt);
			break;
		case 'n':
			new_partition(cxt);
			break;
		case 'o':
			fdisk_create_disklabel(cxt, "dos");
			break;
		case 'p':
			list_table(cxt, 0);
			break;
		case 'q':
			handle_quit(cxt);
		case 's':
			fdisk_create_disklabel(cxt, "sun");
			break;
		case 't':
			change_partition_type(cxt);
			break;
		case 'u':
			toggle_units(cxt);
			break;
		case 'v':
			verify(cxt);
			break;
		case 'w':
			write_table(cxt);
			break;
		case 'x':
			expert_command_prompt(cxt);
			break;
		default:
			unknown_command(c);
		}
	}
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

int main(int argc, char **argv)
{
	int c, optl = 0, opts = 0;
	unsigned long sector_size = 0;
	struct fdisk_context *cxt;
	struct fdisk_label *lb;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	fdisk_init_debug(0);
	cxt = fdisk_new_context();
	if (!cxt)
		err(EXIT_FAILURE, _("failed to allocate libfdisk context"));

	fdisk_context_set_ask(cxt, ask_callback, NULL);

	while ((c = getopt(argc, argv, "b:c::C:hH:lsS:u::vV")) != -1) {
		switch (c) {
		case 'b':
			/* Ugly: this sector size is really per device,
			   so cannot be combined with multiple disks,
			   and te same goes for the C/H/S options.
			*/
			sector_size = strtou32_or_err(optarg, _("invalid sector size argument"));
			if (sector_size != 512 && sector_size != 1024 &&
			    sector_size != 2048 && sector_size != 4096)
				usage(stderr);
			fdisk_save_user_sector_size(cxt, sector_size, sector_size);
			break;
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
				lb = fdisk_context_get_label(cxt, "dos");
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
			optl = 1;
			break;
		case 's':
			opts = 1;
			break;
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


	if (sector_size && argc-optind != 1)
		printf(_("Warning: the -b (set sector size) option should"
			 " be used with one specified device\n"));

	if (optl) {
		nowarn = 1;
		if (argc > optind) {
			int k;
			for (k = optind; k < argc; k++)
				print_partition_table_from_option(cxt, argv[k]);
		} else
			print_all_partition_table_from_option(cxt);
		exit(EXIT_SUCCESS);
	}

	if (opts) {
		/* print partition size for one or more devices */
		int i, ndevs = argc - optind;
		if (ndevs <= 0)
			usage(stderr);

		for (i = optind; i < argc; i++) {
			if (ndevs == 1)
				printf("%llu\n", get_dev_blocks(argv[i]));
			else
				printf("%s: %llu\n", argv[i], get_dev_blocks(argv[i]));
		}
		exit(EXIT_SUCCESS);
	}

	if (argc-optind != 1)
		usage(stderr);

	if (fdisk_context_assign_device(cxt, argv[optind], 0) != 0)
		err(EXIT_FAILURE, _("cannot open %s"), argv[optind]);

	print_welcome();

	if (!fdisk_dev_has_disklabel(cxt)) {
		fprintf(stderr,
			_("Device does not contain a recognized partition table\n"));
		fdisk_create_disklabel(cxt, NULL);
	}

	command_prompt(cxt);

	fdisk_free_context(cxt);
	return 0;
}
