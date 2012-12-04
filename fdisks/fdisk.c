/* fdisk.c -- Partition table manipulator for Linux.
 *
 * Copyright (C) 1992  A. V. Le Blanc (LeBlanc@mcc.ac.uk)
 * Copyright (C) 2012  Davidlohr Bueso <dave@gnu.org>
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

#include "fdisksunlabel.h"
#include "fdisksgilabel.h"
#include "fdiskaixlabel.h"
#include "fdiskmaclabel.h"
#include "fdiskdoslabel.h"
#include "fdiskbsdlabel.h"

#ifdef HAVE_LINUX_COMPILER_H
#include <linux/compiler.h>
#endif
#ifdef HAVE_LINUX_BLKPG_H
#include <linux/blkpg.h>
#endif

#include "gpt.h"

int MBRbuffer_changed;

#define hex_val(c)	({ \
				char _c = (c); \
				isdigit(_c) ? _c - '0' : \
				tolower(_c) + 10 - 'a'; \
			})


#define sector(s)	((s) & 0x3f)
#define cylinder(s, c)	((c) | (((s) & 0xc0) << 2))

/* menu list description */

struct menulist_descr {
	char command;				/* command key */
	char *description;			/* command description */
	enum fdisk_labeltype label[2];		/* disklabel types associated with main and expert menu */
};

static const struct menulist_descr menulist[] = {
	{'a', N_("toggle a bootable flag"), {FDISK_DISKLABEL_DOS, 0}},
	{'a', N_("toggle a read only flag"), {FDISK_DISKLABEL_SUN, 0}},
	{'a', N_("select bootable partition"), {FDISK_DISKLABEL_SGI, 0}},
	{'a', N_("change number of alternate cylinders"), {0, FDISK_DISKLABEL_SUN}},
	{'b', N_("edit bsd disklabel"), {FDISK_DISKLABEL_DOS, 0}},
	{'b', N_("edit bootfile entry"), {FDISK_DISKLABEL_SGI, 0}},
	{'b', N_("move beginning of data in a partition"), {0, FDISK_DISKLABEL_DOS}},
	{'c', N_("toggle the dos compatibility flag"), {FDISK_DISKLABEL_DOS, 0}},
	{'c', N_("toggle the mountable flag"), {FDISK_DISKLABEL_SUN, 0}},
	{'c', N_("select sgi swap partition"), {FDISK_DISKLABEL_SGI, 0}},
	{'c', N_("change number of cylinders"), {0, FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN}},
	{'d', N_("delete a partition"), {FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN | FDISK_DISKLABEL_SGI | FDISK_DISKLABEL_OSF | FDISK_DISKLABEL_GPT, 0}},
	{'d', N_("print the raw data in the partition table"), {0, FDISK_DISKLABEL_ANY}},
	{'e', N_("change number of extra sectors per cylinder"), {0, FDISK_DISKLABEL_SUN}},
	{'e', N_("list extended partitions"), {0, FDISK_DISKLABEL_DOS}},
	{'e', N_("edit drive data"), {FDISK_DISKLABEL_OSF, 0}},
	{'f', N_("fix partition order"), {0, FDISK_DISKLABEL_DOS}},
	{'g', N_("create an IRIX (SGI) partition table"), {0, FDISK_DISKLABEL_ANY}},
	{'g', N_("create a new empty GPT partition table"), {~FDISK_DISKLABEL_OSF, 0}},
	{'h', N_("change number of heads"), {0, FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN}},
	{'i', N_("change interleave factor"), {0, FDISK_DISKLABEL_SUN}},
	{'i', N_("change the disk identifier"), {0, FDISK_DISKLABEL_DOS}},
	{'i', N_("install bootstrap"), {FDISK_DISKLABEL_OSF, 0}},
	{'l', N_("list known partition types"), {FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN | FDISK_DISKLABEL_SGI | FDISK_DISKLABEL_OSF  | FDISK_DISKLABEL_GPT, 0}},
	{'m', N_("print this menu"), {FDISK_DISKLABEL_ANY, FDISK_DISKLABEL_ANY}},
	{'n', N_("add a new partition"), {FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN | FDISK_DISKLABEL_SGI | FDISK_DISKLABEL_OSF | FDISK_DISKLABEL_GPT, 0}},
	{'o', N_("create a new empty DOS partition table"), {~FDISK_DISKLABEL_OSF, 0}},
	{'o', N_("change rotation speed (rpm)"), {0, FDISK_DISKLABEL_SUN}},
	{'p', N_("print the partition table"), {FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN | FDISK_DISKLABEL_SGI | FDISK_DISKLABEL_OSF, FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN}},
	{'q', N_("quit without saving changes"), {FDISK_DISKLABEL_ANY, FDISK_DISKLABEL_ANY}},
	{'r', N_("return to main menu"), {FDISK_DISKLABEL_OSF, FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN | FDISK_DISKLABEL_SGI | FDISK_DISKLABEL_OSF}},
	{'s', N_("create a new empty Sun disklabel"), {~FDISK_DISKLABEL_OSF, 0}},
	{'s', N_("change number of sectors/track"), {0, FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN}},
	{'s', N_("show complete disklabel"), {FDISK_DISKLABEL_OSF, 0}},
	{'t', N_("change a partition's system id"), {FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN | FDISK_DISKLABEL_SGI | FDISK_DISKLABEL_OSF, 0}},
	{'u', N_("change display/entry units"), {FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN | FDISK_DISKLABEL_SGI | FDISK_DISKLABEL_OSF, 0}},
	{'v', N_("verify the partition table"), {FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN | FDISK_DISKLABEL_SGI, FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN | FDISK_DISKLABEL_SGI}},
	{'w', N_("write table to disk and exit"), {FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN | FDISK_DISKLABEL_SGI  | FDISK_DISKLABEL_GPT, FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN | FDISK_DISKLABEL_SGI}},
	{'w', N_("write disklabel to disk"), {FDISK_DISKLABEL_OSF, 0}},
	{'x', N_("extra functionality (experts only)"), {FDISK_DISKLABEL_DOS | FDISK_DISKLABEL_SUN | FDISK_DISKLABEL_SGI, 0}},
#if !defined (__alpha__)
	{'x', N_("link BSD partition to non-BSD partition"), {FDISK_DISKLABEL_OSF, 0}},
#endif
	{'y', N_("change number of physical cylinders"), {0, FDISK_DISKLABEL_SUN}},
};



sector_t get_nr_sects(struct partition *p) {
	return read4_little_endian(p->size4);
}

char	*line_ptr,			/* interactive input */
	line_buffer[LINE_LENGTH];

int	nowarn = 0,			/* no warnings for fdisk -l/-s */
	dos_compatible_flag = 0,	/* disabled by default */
	partitions = 4;			/* maximum partition + 1 */

unsigned int	user_cylinders, user_heads, user_sectors;

static void __attribute__ ((__noreturn__)) usage(FILE *out)
{
	fprintf(out, _("Usage:\n"
		       " %1$s [options] <disk>    change partition table\n"
		       " %1$s [options] -l <disk> list partition table(s)\n"
		       " %1$s -s <partition>      give partition size(s) in blocks\n"
		       "\nOptions:\n"
		       " -b <size>             sector size (512, 1024, 2048 or 4096)\n"
		       " -c[=<mode>]           compatible mode: 'dos' or 'nondos' (default)\n"
		       " -h                    print this help text\n"
		       " -u[=<unit>]           display units: 'cylinders' or 'sectors' (default)\n"
		       " -v                    print program version\n"
		       " -C <number>           specify the number of cylinders\n"
		       " -H <number>           specify the number of heads\n"
		       " -S <number>           specify the number of sectors per track\n"
		       "\n"), program_invocation_short_name);
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

void __attribute__((__noreturn__))
fatal(struct fdisk_context *cxt, enum failure why)
{
	close(cxt->dev_fd);
	switch (why) {
		case unable_to_read:
			err(EXIT_FAILURE, _("unable to read %s"), cxt->dev_path);

		case unable_to_seek:
			err(EXIT_FAILURE, _("unable to seek on %s"), cxt->dev_path);

		case unable_to_write:
			err(EXIT_FAILURE, _("unable to write %s"), cxt->dev_path);

		case ioctl_error:
			err(EXIT_FAILURE, _("BLKGETSIZE ioctl failed on %s"), cxt->dev_path);

		default:
			err(EXIT_FAILURE, _("fatal error"));
	}
}

struct partition *
get_part_table(int i) {
	return ptes[i].part_table;
}

void
set_all_unchanged(void) {
	int i;

	for (i = 0; i < MAXIMUM_PARTS; i++)
		ptes[i].changed = 0;
}

void
set_changed(int i) {
	ptes[i].changed = 1;
}

static int
is_garbage_table(void) {
	int i;

	for (i = 0; i < 4; i++) {
		struct pte *pe = &ptes[i];
		struct partition *p = pe->part_table;

		if (p->boot_ind != 0 && p->boot_ind != 0x80)
			return 1;
	}
	return 0;
}

void print_menu(struct fdisk_context *cxt, enum menutype menu)
{
	size_t i;

	puts(_("Command action"));

	for (i = 0; i < ARRAY_SIZE(menulist); i++)
		if (menulist[i].label[menu] & cxt->disklabel)
			printf("   %c   %s\n", menulist[i].command, menulist[i].description);
}

void list_partition_types(struct fdisk_context *cxt)
{
	struct fdisk_parttype *types;
	int i;

	if (!cxt || !cxt->label || !cxt->label->parttypes)
		return;

	types = cxt->label->parttypes;

	if (types[0].typestr == NULL) {
		/*
		 * Prints in 4 columns in format <hex> <name>
		 */
		unsigned int last[4], done = 0, next = 0, size;

		for (i = 0; types[i].name; i++);
		size = i;

		for (i = 3; i >= 0; i--)
			last[3 - i] = done += (size + i - done) / (i + 1);
		i = done = 0;

		do {
			#define NAME_WIDTH 15
			char name[NAME_WIDTH * MB_LEN_MAX];
			size_t width = NAME_WIDTH;
			struct fdisk_parttype *t = &types[next];
			size_t ret;

			printf("%c%2x  ", i ? ' ' : '\n', t->type);
			ret = mbsalign(_(t->name), name, sizeof(name),
					      &width, MBS_ALIGN_LEFT, 0);

			if (ret == (size_t)-1 || ret >= sizeof(name))
				printf("%-15.15s", _(t->name));
			else
				fputs(name, stdout);

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

		for (i = 0, t = types; t->name; t++, i++)
			printf("%3d %-30s %s\n", i + 1, t->name, t->typestr);
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

static int
lba_is_aligned(struct fdisk_context *cxt, sector_t lba)
{
	unsigned int granularity = max(cxt->phy_sector_size, cxt->min_io_size);
	unsigned long long offset;

	if (cxt->grain > granularity)
		granularity = cxt->grain;
	offset = (lba * cxt->sector_size) & (granularity - 1);

	return !((granularity + cxt->alignment_offset - offset) & (granularity - 1));
}

static int
lba_is_phy_aligned(struct fdisk_context *cxt, unsigned long long lba)
{
	unsigned int granularity = max(cxt->phy_sector_size, cxt->min_io_size);
	unsigned long long offset = (lba * cxt->sector_size) & (granularity - 1);

	return !((granularity + cxt->alignment_offset - offset) & (granularity - 1));
}

sector_t align_lba(struct fdisk_context *cxt, sector_t lba, int direction)
{
	sector_t res;

	if (lba_is_aligned(cxt, lba))
		res = lba;
	else {
		sector_t sects_in_phy = cxt->grain / cxt->sector_size;

		if (lba < cxt->first_lba)
			res = cxt->first_lba;

		else if (direction == ALIGN_UP)
			res = ((lba + sects_in_phy) / sects_in_phy) * sects_in_phy;

		else if (direction == ALIGN_DOWN)
			res = (lba / sects_in_phy) * sects_in_phy;

		else /* ALIGN_NEAREST */
			res = ((lba + sects_in_phy / 2) / sects_in_phy) * sects_in_phy;

		if (cxt->alignment_offset && !lba_is_aligned(cxt, res) &&
		    res > cxt->alignment_offset / cxt->sector_size) {
			/*
			 * apply alignment_offset
			 *
			 * On disk with alignment compensation physical blocks starts
			 * at LBA < 0 (usually LBA -1). It means we have to move LBA
			 * according the offset to be on the physical boundary.
			 */
			/* fprintf(stderr, "LBA: %llu apply alignment_offset\n", res); */
			res -= (max(cxt->phy_sector_size, cxt->min_io_size) -
					cxt->alignment_offset) / cxt->sector_size;

			if (direction == ALIGN_UP && res < lba)
				res += sects_in_phy;
		}
	}

	return res;
}


sector_t align_lba_in_range(struct fdisk_context *cxt,
			    sector_t lba, sector_t start, sector_t stop)
{
	start = align_lba(cxt, start, ALIGN_UP);
	stop = align_lba(cxt, stop, ALIGN_DOWN);

	lba = align_lba(cxt, lba, ALIGN_NEAREST);

	if (lba < start)
		return start;
	else if (lba > stop)
		return stop;
	return lba;
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

static int is_partition_table_changed(void)
{
	int i;

	for (i = 0; i < partitions; i++)
		if (ptes[i].changed)
			return 1;
	return 0;
}

static void maybe_exit(int rc, int *asked)
{
	char line[LINE_LENGTH];

	putchar('\n');
	if (asked)
		*asked = 0;

	if (is_partition_table_changed() || MBRbuffer_changed) {
		fprintf(stderr, _("Do you really want to quit? "));

		if (!fgets(line, LINE_LENGTH, stdin) || rpmatch(line) == 1)
			exit(rc);
		if (asked)
			*asked = 1;
	} else
		exit(rc);
}

/* read line; return 0 or first char */
int
read_line(int *asked)
{
	line_ptr = line_buffer;
	if (!fgets(line_buffer, LINE_LENGTH, stdin)) {
		maybe_exit(1, asked);
		return 0;
	}
	if (asked)
		*asked = 0;
	while (*line_ptr && !isgraph(*line_ptr))
		line_ptr++;
	return *line_ptr;
}

char
read_char(char *mesg)
{
	do {
		fputs(mesg, stdout);
		fflush (stdout);	 /* requested by niles@scyld.com */
	} while (!read_line(NULL));
	return *line_ptr;
}

char
read_chars(char *mesg)
{
	int rc, asked = 0;

	do {
	        fputs(mesg, stdout);
		fflush (stdout);	/* niles@scyld.com */
		rc = read_line(&asked);
	} while (asked);

	if (!rc) {
		*line_ptr = '\n';
		line_ptr[1] = 0;
	}
	return *line_ptr;
}

struct fdisk_parttype *read_partition_type(struct fdisk_context *cxt)
{
	if (!cxt || !cxt->label || !cxt->label->nparttypes)
		return NULL;

        do {
		size_t sz;

		if (cxt->label->parttypes[0].typestr)
			read_chars(_("Partition type (type L to list all types): "));
		else
			read_chars(_("Hex code (type L to list all codes): "));

		sz = strlen(line_ptr);
		if (!sz || line_ptr[sz - 1] != '\n' || sz == 1)
			continue;
		line_ptr[sz - 1] = '\0';

		if (tolower(*line_ptr) == 'l')
			list_partition_types(cxt);
		else
			return fdisk_parse_parttype(cxt, line_ptr);
        } while (1);

	return NULL;
}


unsigned int
read_int_with_suffix(struct fdisk_context *cxt,
	unsigned int low, unsigned int dflt, unsigned int high,
	 unsigned int base, char *mesg, int *is_suffix_used)
{
	unsigned int res;
	int default_ok = 1;
	int absolute = 0;
	static char *ms = NULL;
	static size_t mslen = 0;

	if (!ms || strlen(mesg)+100 > mslen) {
		mslen = strlen(mesg)+200;
		ms = xrealloc(ms,mslen);
	}

	if (dflt < low || dflt > high)
		default_ok = 0;

	if (default_ok)
		snprintf(ms, mslen, _("%s (%u-%u, default %u): "),
			 mesg, low, high, dflt);
	else
		snprintf(ms, mslen, "%s (%u-%u): ",
			 mesg, low, high);

	while (1) {
		int use_default = default_ok;

		/* ask question and read answer */
		while (read_chars(ms) != '\n' && !isdigit(*line_ptr)
		       && *line_ptr != '-' && *line_ptr != '+')
			continue;

		if (*line_ptr == '+' || *line_ptr == '-') {
			int minus = (*line_ptr == '-');
			int suflen;

			absolute = 0;
			res = atoi(line_ptr + 1);

			while (isdigit(*++line_ptr))
				use_default = 0;

			while (isspace(*line_ptr))
				line_ptr++;

			suflen = strlen(line_ptr) - 1;

			while(isspace(*(line_ptr + suflen)))
				*(line_ptr + suflen--) = '\0';

			if ((*line_ptr == 'C' || *line_ptr == 'c') &&
			    *(line_ptr + 1) == '\0') {
				/*
				 * Cylinders
				 */
				if (!display_in_cyl_units)
					res *= cxt->geom.heads * cxt->geom.sectors;
			} else if (*line_ptr &&
				   *(line_ptr + 1) == 'B' &&
				   *(line_ptr + 2) == '\0') {
				/*
				 * 10^N
				 */
				if (*line_ptr == 'K')
					absolute = 1000;
				else if (*line_ptr == 'M')
					absolute = 1000000;
				else if (*line_ptr == 'G')
					absolute = 1000000000;
				else
					absolute = -1;
			} else if (*line_ptr &&
				   *(line_ptr + 1) == '\0') {
				/*
				 * 2^N
				 */
				if (*line_ptr == 'K')
					absolute = 1 << 10;
				else if (*line_ptr == 'M')
					absolute = 1 << 20;
				else if (*line_ptr == 'G')
					absolute = 1 << 30;
				else
					absolute = -1;
			} else if (*line_ptr != '\0')
				absolute = -1;

			if (absolute == -1)  {
				printf(_("Unsupported suffix: '%s'.\n"), line_ptr);
				printf(_("Supported: 10^N: KB (KiloByte), MB (MegaByte), GB (GigaByte)\n"
					 "            2^N: K  (KibiByte), M  (MebiByte), G  (GibiByte)\n"));
				continue;
			}

			if (absolute && res) {
				unsigned long long bytes;
				unsigned long unit;

				bytes = (unsigned long long) res * absolute;
				unit = cxt->sector_size * units_per_sector;
				bytes += unit/2;	/* round */
				bytes /= unit;
				res = bytes;
			}
			if (minus)
				res = -res;
			res += base;
		} else {
			res = atoi(line_ptr);
			while (isdigit(*line_ptr)) {
				line_ptr++;
				use_default = 0;
			}
		}
		if (use_default) {
			printf(_("Using default value %u\n"), dflt);
			return dflt;
		}
		if (res >= low && res <= high)
			break;
		else
			printf(_("Value out of range.\n"));
	}
	if (is_suffix_used)
			*is_suffix_used = absolute > 0;
	return res;
}

/*
 * Print the message MESG, then read an integer in LOW..HIGH.
 * If the user hits Enter, DFLT is returned, provided that is in LOW..HIGH.
 * Answers like +10 are interpreted as offsets from BASE.
 *
 * There is no default if DFLT is not between LOW and HIGH.
 */
unsigned int
read_int(struct fdisk_context *cxt,
	 unsigned int low, unsigned int dflt, unsigned int high,
	 unsigned int base, char *mesg)
{
	return read_int_with_suffix(cxt, low, dflt, high, base, mesg, NULL);
}

int
get_partition_dflt(struct fdisk_context *cxt, int warn, int max, int dflt) {
	struct pte *pe;
	int i;

	i = read_int(cxt, 1, dflt, max, 0, _("Partition number")) - 1;
	pe = &ptes[i];

	if (warn && !fdisk_is_disklabel(cxt, GPT)) {
		if ((!fdisk_is_disklabel(cxt, SUN) &&
		     !fdisk_is_disklabel(cxt, SGI) && !pe->part_table->sys_ind)
		    || (fdisk_is_disklabel(cxt, SUN) &&
			(!sunlabel->partitions[i].num_sectors ||
			 !sunlabel->part_tags[i].tag))
		    || (fdisk_is_disklabel(cxt, SGI) &&
			(!sgi_get_num_sectors(cxt, i))))
			fprintf(stderr,
				_("Warning: partition %d has empty type\n"),
				i+1);
	}
	return i;
}

int
get_partition(struct fdisk_context *cxt, int warn, int max) {
	return get_partition_dflt(cxt, warn, max, 0);
}

/* User partition selection unless one partition only is available */

static int
get_existing_partition(struct fdisk_context *cxt, int warn, int max) {
	int pno = -1;
	int i;

	if (!fdisk_is_disklabel(cxt, DOS))
		goto not_implemented;

	for (i = 0; i < max; i++) {
		struct pte *pe = &ptes[i];
		struct partition *p = pe->part_table;

		if (p && !is_cleared_partition(p)) {
			if (pno >= 0)
				goto not_unique;
			pno = i;
		}
	}

	if (pno >= 0) {
		printf(_("Selected partition %d\n"), pno+1);
		return pno;
	}
	printf(_("No partition is defined yet!\n"));
	return -1;

not_implemented:
not_unique:
	return get_partition(cxt, warn, max);
}

const char *
str_units(int n)
{
	if (display_in_cyl_units)
		return P_("cylinder", "cylinders", n);
	return P_("sector", "sectors", n);
}

static void
toggle_active(int i) {
	struct pte *pe = &ptes[i];
	struct partition *p = pe->part_table;

	if (IS_EXTENDED (p->sys_ind) && !p->boot_ind)
		fprintf(stderr,
			_("WARNING: Partition %d is an extended partition\n"),
			i + 1);
	p->boot_ind = (p->boot_ind ? 0 : ACTIVE_FLAG);
	pe->changed = 1;
}

static void
toggle_dos_compatibility_flag(struct fdisk_context *cxt) {
	dos_compatible_flag = ~dos_compatible_flag;
	if (dos_compatible_flag)
		printf(_("DOS Compatibility flag is set (DEPRECATED!)\n"));
	else
		printf(_("DOS Compatibility flag is not set\n"));

	fdisk_reset_alignment(cxt);
}

static void delete_partition(struct fdisk_context *cxt, int partnum)
{
	if (partnum < 0 || warn_geometry(cxt))
		return;

	ptes[partnum].changed = 1;
	if (fdisk_delete_partition(cxt, partnum) != 0)
		printf(_("Could not delete partition %d\n"), partnum + 1);
	else
		printf(_("Partition %d is deleted\n"), partnum + 1);
}

static void change_partition_type(struct fdisk_context *cxt)
{
	int i;
	struct fdisk_parttype *t, *org_t;

	i = get_existing_partition(cxt, 0, partitions);
	if (i == -1)
		return;

	org_t = t = fdisk_get_partition_type(cxt, i);
	if (!t)
                printf(_("Partition %d does not exist yet!\n"), i + 1);

        else do {
		t = read_partition_type(cxt);
		if (!t)
			continue;

		if (fdisk_set_partition_type(cxt, i, t) == 0) {
			ptes[i].changed = 1;
			printf (_("Changed type of partition '%s' to '%s'\n"),
				org_t ? org_t->name : _("Unknown"),
				    t ?     t->name : _("Unknown"));
		} else {
			printf (_("Type of partition %d is unchanged: %s\n"),
				i + 1,
				org_t ? org_t->name : _("Unknown"));
		}
		break;
        } while (1);

	fdisk_free_parttype(t);
	fdisk_free_parttype(org_t);
}

/* check_consistency() and long2chs() added Sat Mar 6 12:28:16 1993,
 * faith@cs.unc.edu, based on code fragments from pfdisk by Gordon W. Ross,
 * Jan.  1990 (version 1.2.1 by Gordon W. Ross Aug. 1990; Modified by S.
 * Lubkin Oct.  1991). */

static void
long2chs(struct fdisk_context *cxt, unsigned long ls,
	 unsigned int *c, unsigned int *h, unsigned int *s) {
	int spc = cxt->geom.heads * cxt->geom.sectors;

	*c = ls / spc;
	ls = ls % spc;
	*h = ls / cxt->geom.sectors;
	*s = ls % cxt->geom.sectors + 1;	/* sectors count from 1 */
}

void check_consistency(struct fdisk_context *cxt, struct partition *p, int partition)
{
	unsigned int pbc, pbh, pbs;	/* physical beginning c, h, s */
	unsigned int pec, peh, pes;	/* physical ending c, h, s */
	unsigned int lbc, lbh, lbs;	/* logical beginning c, h, s */
	unsigned int lec, leh, les;	/* logical ending c, h, s */

	if (!dos_compatible_flag)
		return;

	if (!cxt->geom.heads || !cxt->geom.sectors || (partition >= 4))
		return;		/* do not check extended partitions */

/* physical beginning c, h, s */
	pbc = (p->cyl & 0xff) | ((p->sector << 2) & 0x300);
	pbh = p->head;
	pbs = p->sector & 0x3f;

/* physical ending c, h, s */
	pec = (p->end_cyl & 0xff) | ((p->end_sector << 2) & 0x300);
	peh = p->end_head;
	pes = p->end_sector & 0x3f;

/* compute logical beginning (c, h, s) */
	long2chs(cxt, get_start_sect(p), &lbc, &lbh, &lbs);

/* compute logical ending (c, h, s) */
	long2chs(cxt, get_start_sect(p) + get_nr_sects(p) - 1, &lec, &leh, &les);

/* Same physical / logical beginning? */
	if (cxt->geom.cylinders <= 1024 && (pbc != lbc || pbh != lbh || pbs != lbs)) {
		printf(_("Partition %d has different physical/logical "
			"beginnings (non-Linux?):\n"), partition + 1);
		printf(_("     phys=(%d, %d, %d) "), pbc, pbh, pbs);
		printf(_("logical=(%d, %d, %d)\n"),lbc, lbh, lbs);
	}

/* Same physical / logical ending? */
	if (cxt->geom.cylinders <= 1024 && (pec != lec || peh != leh || pes != les)) {
		printf(_("Partition %d has different physical/logical "
			"endings:\n"), partition + 1);
		printf(_("     phys=(%d, %d, %d) "), pec, peh, pes);
		printf(_("logical=(%d, %d, %d)\n"),lec, leh, les);
	}

/* Ending on cylinder boundary? */
	if (peh != (cxt->geom.heads - 1) || pes != cxt->geom.sectors) {
		printf(_("Partition %i does not end on cylinder boundary.\n"),
			partition + 1);
	}
}

void check_alignment(struct fdisk_context *cxt, sector_t lba, int partition)
{
	if (!lba_is_phy_aligned(cxt, lba))
		printf(_("Partition %i does not start on physical sector boundary.\n"),
			partition + 1);
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
	if (dos_compatible_flag)
		printf(_("%d heads, %llu sectors/track, %llu cylinders\n"),
		       cxt->geom.heads, cxt->geom.sectors, cxt->geom.cylinders);
	printf(_("Units = %s of %d * %ld = %ld bytes\n"),
	       str_units(PLURAL),
	       units_per_sector, cxt->sector_size, units_per_sector * cxt->sector_size);

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

/*
 * Check whether partition entries are ordered by their starting positions.
 * Return 0 if OK. Return i if partition i should have been earlier.
 * Two separate checks: primary and logical partitions.
 */
static int
wrong_p_order(int *prev) {
	struct pte *pe;
	struct partition *p;
	unsigned int last_p_start_pos = 0, p_start_pos;
	int i, last_i = 0;

	for (i = 0 ; i < partitions; i++) {
		if (i == 4) {
			last_i = 4;
			last_p_start_pos = 0;
		}
		pe = &ptes[i];
		if ((p = pe->part_table)->sys_ind) {
			p_start_pos = get_partition_start(pe);

			if (last_p_start_pos > p_start_pos) {
				if (prev)
					*prev = last_i;
				return i;
			}

			last_p_start_pos = p_start_pos;
			last_i = i;
		}
	}
	return 0;
}

/*
 * Fix the chain of logicals.
 * extended_offset is unchanged, the set of sectors used is unchanged
 * The chain is sorted so that sectors increase, and so that
 * starting sectors increase.
 *
 * After this it may still be that cfdisk doesn't like the table.
 * (This is because cfdisk considers expanded parts, from link to
 * end of partition, and these may still overlap.)
 * Now
 *   sfdisk /dev/hda > ohda; sfdisk /dev/hda < ohda
 * may help.
 */
static void
fix_chain_of_logicals(void) {
	int j, oj, ojj, sj, sjj;
	struct partition *pj,*pjj,tmp;

	/* Stage 1: sort sectors but leave sector of part 4 */
	/* (Its sector is the global extended_offset.) */
 stage1:
	for (j = 5; j < partitions-1; j++) {
		oj = ptes[j].offset;
		ojj = ptes[j+1].offset;
		if (oj > ojj) {
			ptes[j].offset = ojj;
			ptes[j+1].offset = oj;
			pj = ptes[j].part_table;
			set_start_sect(pj, get_start_sect(pj)+oj-ojj);
			pjj = ptes[j+1].part_table;
			set_start_sect(pjj, get_start_sect(pjj)+ojj-oj);
			set_start_sect(ptes[j-1].ext_pointer,
				       ojj-extended_offset);
			set_start_sect(ptes[j].ext_pointer,
				       oj-extended_offset);
			goto stage1;
		}
	}

	/* Stage 2: sort starting sectors */
 stage2:
	for (j = 4; j < partitions-1; j++) {
		pj = ptes[j].part_table;
		pjj = ptes[j+1].part_table;
		sj = get_start_sect(pj);
		sjj = get_start_sect(pjj);
		oj = ptes[j].offset;
		ojj = ptes[j+1].offset;
		if (oj+sj > ojj+sjj) {
			tmp = *pj;
			*pj = *pjj;
			*pjj = tmp;
			set_start_sect(pj, ojj+sjj-oj);
			set_start_sect(pjj, oj+sj-ojj);
			goto stage2;
		}
	}

	/* Probably something was changed */
	for (j = 4; j < partitions; j++)
		ptes[j].changed = 1;
}

static void
fix_partition_table_order(void) {
	struct pte *pei, *pek;
	int i,k;

	if (!wrong_p_order(NULL)) {
		printf(_("Nothing to do. Ordering is correct already.\n\n"));
		return;
	}

	while ((i = wrong_p_order(&k)) != 0 && i < 4) {
		/* partition i should have come earlier, move it */
		/* We have to move data in the MBR */
		struct partition *pi, *pk, *pe, pbuf;
		pei = &ptes[i];
		pek = &ptes[k];

		pe = pei->ext_pointer;
		pei->ext_pointer = pek->ext_pointer;
		pek->ext_pointer = pe;

		pi = pei->part_table;
		pk = pek->part_table;

		memmove(&pbuf, pi, sizeof(struct partition));
		memmove(pi, pk, sizeof(struct partition));
		memmove(pk, &pbuf, sizeof(struct partition));

		pei->changed = pek->changed = 1;
	}

	if (i)
		fix_chain_of_logicals();

	printf(_("Done.\n"));

}

static void list_table(struct fdisk_context *cxt, int xtra)
{
	struct partition *p;
	int i, w;

	if (fdisk_is_disklabel(cxt, SUN)) {
		sun_list_table(cxt, xtra);
		return;
	}

	if (fdisk_is_disklabel(cxt, SGI)) {
		sgi_list_table(cxt, xtra);
		return;
	}

	list_disk_geometry(cxt);

	if (fdisk_is_disklabel(cxt, GPT)) {
		gpt_list_table(cxt, xtra);
		return;
	}

	if (fdisk_is_disklabel(cxt, OSF)) {
		xbsd_print_disklabel(cxt, xtra);
		return;
	}

	if (is_garbage_table()) {
		printf(_("This doesn't look like a partition table\n"
			 "Probably you selected the wrong device.\n\n"));
	}

	/* Heuristic: we list partition 3 of /dev/foo as /dev/foo3,
	   but if the device name ends in a digit, say /dev/foo1,
	   then the partition is called /dev/foo1p3. */
	w = strlen(cxt->dev_path);
	if (w && isdigit(cxt->dev_path[w-1]))
		w++;
	if (w < 5)
		w = 5;

	printf(_("%*s Boot      Start         End      Blocks   Id  System\n"),
	       w+1, _("Device"));

	for (i = 0; i < partitions; i++) {
		struct pte *pe = &ptes[i];

		p = pe->part_table;
		if (p && !is_cleared_partition(p)) {
			unsigned int psects = get_nr_sects(p);
			unsigned int pblocks = psects;
			unsigned int podd = 0;
			struct fdisk_parttype *type =
					fdisk_get_parttype_from_code(cxt, p->sys_ind);

			if (cxt->sector_size < 1024) {
				pblocks /= (1024 / cxt->sector_size);
				podd = psects % (1024 / cxt->sector_size);
			}
			if (cxt->sector_size > 1024)
				pblocks *= (cxt->sector_size / 1024);
                        printf(
			    "%s  %c %11lu %11lu %11lu%c  %2x  %s\n",
			partname(cxt->dev_path, i+1, w+2),
/* boot flag */		!p->boot_ind ? ' ' : p->boot_ind == ACTIVE_FLAG
			? '*' : '?',
/* start */		(unsigned long) cround(get_partition_start(pe)),
/* end */		(unsigned long) cround(get_partition_start(pe) + psects
				- (psects ? 1 : 0)),
/* odd flag on end */	(unsigned long) pblocks, podd ? '+' : ' ',
/* type id */		p->sys_ind,
/* type name */		type ? type->name : _("Unknown"));
			check_consistency(cxt, p, i);
			check_alignment(cxt, get_partition_start(pe), i);
		}
	}

	/* Is partition table in disk order? It need not be, but... */
	/* partition table entries are not checked for correct order if this
	   is a sgi, sun or aix labeled disk... */
	if (fdisk_is_disklabel(cxt, DOS) && wrong_p_order(NULL)) {
		printf(_("\nPartition table entries are not in disk order\n"));
	}
}

static void
x_list_table(struct fdisk_context *cxt, int extend) {
	struct pte *pe;
	struct partition *p;
	int i;

	printf(_("\nDisk %s: %d heads, %llu sectors, %llu cylinders\n\n"),
		cxt->dev_path, cxt->geom.heads, cxt->geom.sectors, cxt->geom.cylinders);
        printf(_("Nr AF  Hd Sec  Cyl  Hd Sec  Cyl     Start      Size ID\n"));
	for (i = 0 ; i < partitions; i++) {
		pe = &ptes[i];
		p = (extend ? pe->ext_pointer : pe->part_table);
		if (p != NULL) {
                        printf("%2d %02x%4d%4d%5d%4d%4d%5d%11lu%11lu %02x\n",
				i + 1, p->boot_ind, p->head,
				sector(p->sector),
				cylinder(p->sector, p->cyl), p->end_head,
				sector(p->end_sector),
				cylinder(p->end_sector, p->end_cyl),
				(unsigned long) get_start_sect(p),
				(unsigned long) get_nr_sects(p), p->sys_ind);
			if (p->sys_ind) {
				check_consistency(cxt, p, i);
				check_alignment(cxt, get_partition_start(pe), i);
			}
		}
	}
}

void fill_bounds(sector_t *first, sector_t *last)
{
	int i;
	struct pte *pe = &ptes[0];
	struct partition *p;

	for (i = 0; i < partitions; pe++,i++) {
		p = pe->part_table;
		if (!p->sys_ind || IS_EXTENDED (p->sys_ind)) {
			first[i] = 0xffffffff;
			last[i] = 0;
		} else {
			first[i] = get_partition_start(pe);
			last[i] = first[i] + get_nr_sects(p) - 1;
		}
	}
}

void check(struct fdisk_context *cxt, int n, 
	   unsigned int h, unsigned int s, unsigned int c,
	   unsigned int start)
{
	unsigned int total, real_s, real_c;

	real_s = sector(s) - 1;
	real_c = cylinder(s, c);
	total = (real_c * cxt->geom.sectors + real_s) * cxt->geom.heads + h;
	if (!total)
		fprintf(stderr, _("Warning: partition %d contains sector 0\n"), n);
	if (h >= cxt->geom.heads)
		fprintf(stderr,
			_("Partition %d: head %d greater than maximum %d\n"),
			n, h + 1, cxt->geom.heads);
	if (real_s >= cxt->geom.sectors)
		fprintf(stderr, _("Partition %d: sector %d greater than "
			"maximum %llu\n"), n, s, cxt->geom.sectors);
	if (real_c >= cxt->geom.cylinders)
		fprintf(stderr, _("Partitions %d: cylinder %d greater than "
			"maximum %llu\n"), n, real_c + 1, cxt->geom.cylinders);
	if (cxt->geom.cylinders <= 1024 && start != total)
		fprintf(stderr,
			_("Partition %d: previous sectors %d disagrees with "
			"total %d\n"), n, start, total);
}

static void verify(struct fdisk_context *cxt)
{
	if (warn_geometry(cxt))
		return;

	fdisk_verify_disklabel(cxt);
}

void print_partition_size(struct fdisk_context *cxt,
			  int num, sector_t start, sector_t stop, int sysid)
{
	char *str = size_to_human_string(SIZE_SUFFIX_3LETTER | SIZE_SUFFIX_SPACE,
				     (uint64_t)(stop - start + 1) * cxt->sector_size);
	struct fdisk_parttype *t = fdisk_get_parttype_from_code(cxt, sysid);

	printf(_("Partition %d of type %s and of size %s is set\n"),
			num, t ? t->name : _("Unknown"), str);
	free(str);
}

static void new_partition(struct fdisk_context *cxt)
{
	int partnum = 0;

	if (warn_geometry(cxt))
		return;

	if (fdisk_is_disklabel(cxt, SUN) ||
	    fdisk_is_disklabel(cxt, SGI) ||
	    fdisk_is_disklabel(cxt, GPT))
		partnum = get_partition(cxt, 0, partitions);

	fdisk_add_partition(cxt, partnum, NULL);
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
	int i;

	printf(_("Device: %s\n"), cxt->dev_path);
	if (fdisk_is_disklabel(cxt, SUN) ||
	    fdisk_is_disklabel(cxt, SGI) ||
	    fdisk_is_disklabel(cxt, GPT))
		print_buffer(cxt, cxt->firstsector);
	else for (i = 3; i < partitions; i++)
		     print_buffer(cxt, ptes[i].sectorbuffer);
}

static void
move_begin(struct fdisk_context *cxt, int i) {
	struct pte *pe = &ptes[i];
	struct partition *p = pe->part_table;
	unsigned int new, free_start, curr_start, last;
	int x;

	if (warn_geometry(cxt))
		return;
	if (!p->sys_ind || !get_nr_sects(p) || IS_EXTENDED (p->sys_ind)) {
		printf(_("Partition %d has no data area\n"), i + 1);
		return;
	}

	/* the default start is at the second sector of the disk or at the
	 * second sector of the extended partition
	 */
	free_start = pe->offset ? pe->offset + 1 : 1;

	curr_start = get_partition_start(pe);

	/* look for a free space before the current start of the partition */
	for (x = 0; x < partitions; x++) {
		unsigned int end;
		struct pte *prev_pe = &ptes[x];
		struct partition *prev_p = prev_pe->part_table;

		if (!prev_p)
			continue;
		end = get_partition_start(prev_pe) + get_nr_sects(prev_p);

		if (!is_cleared_partition(prev_p) &&
		    end > free_start && end <= curr_start)
			free_start = end;
	}

	last = get_partition_start(pe) + get_nr_sects(p) - 1;

	new = read_int(cxt, free_start, curr_start, last, free_start,
		       _("New beginning of data")) - pe->offset;

	if (new != get_nr_sects(p)) {
		unsigned int sects = get_nr_sects(p) + get_start_sect(p) - new;
		set_nr_sects(p, sects);
		set_start_sect(p, new);
		pe->changed = 1;
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

	while(1) {
		putchar('\n');
		c = tolower(read_char(_("Expert command (m for help): ")));
		switch (c) {
		case 'a':
			if (fdisk_is_disklabel(cxt, SUN))
				sun_set_alt_cyl(cxt);
			break;
		case 'b':
			if (fdisk_is_disklabel(cxt, DOS))
				move_begin(cxt, get_partition(cxt, 0, partitions));
			break;
		case 'c':
			user_cylinders = read_int(cxt, 1, cxt->geom.cylinders, 1048576, 0,
					 _("Number of cylinders"));
			fdisk_context_set_user_geometry(cxt, user_cylinders, user_heads, user_sectors);
			if (fdisk_is_disklabel(cxt, SUN))
				sun_set_ncyl(cxt, cxt->geom.cylinders);
			break;
		case 'd':
			print_raw(cxt);
			break;
		case 'e':
			if (fdisk_is_disklabel(cxt, SGI))
				sgi_set_xcyl();
			else if (fdisk_is_disklabel(cxt, SUN))
				sun_set_xcyl(cxt);
			else
				if (fdisk_is_disklabel(cxt, DOS))
					x_list_table(cxt, 1);
			break;
		case 'f':
			if (fdisk_is_disklabel(cxt, DOS))
				fix_partition_table_order();
			break;
		case 'g':
			fdisk_create_disklabel(cxt, "sgi");
			break;
		case 'h':
			user_heads = read_int(cxt, 1, cxt->geom.heads, 256, 0,
					 _("Number of heads"));
			fdisk_context_set_user_geometry(cxt, user_cylinders, user_heads, user_sectors);
			break;
		case 'i':
			if (fdisk_is_disklabel(cxt, SUN))
				sun_set_ilfact(cxt);
			else if (fdisk_is_disklabel(cxt, DOS))
				dos_set_mbr_id(cxt);
			break;
		case 'o':
			if (fdisk_is_disklabel(cxt, SUN))
				sun_set_rspeed(cxt);
			break;
		case 'p':
			if (fdisk_is_disklabel(cxt, SUN))
				list_table(cxt, 1);
			else
				x_list_table(cxt, 0);
			break;
		case 'q':
			handle_quit(cxt);
		case 'r':
			return;
		case 's':
			user_sectors = read_int(cxt, 1, cxt->geom.sectors, 63, 0,
					   _("Number of sectors"));
			if (dos_compatible_flag)
				fprintf(stderr, _("Warning: setting "
					"sector offset for DOS "
					"compatibility\n"));
			fdisk_context_set_user_geometry(cxt, user_cylinders, user_heads, user_sectors);
			break;
		case 'v':
			verify(cxt);
			break;
		case 'w':
			write_table(cxt);
			break;
		case 'y':
			if (fdisk_is_disklabel(cxt, SUN))
				sun_set_pcylcount(cxt);
			break;
		default:
			print_menu(cxt, EXPERT_MENU);
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
static void print_partition_table_from_option(char *device, unsigned long sector_size)
{
	struct fdisk_context *cxt;

	cxt = fdisk_new_context_from_filename(device, 1);	/* read-only */
	if (!cxt)
		err(EXIT_FAILURE, _("cannot open %s"), device);

	if (sector_size) /* passed -b option, override autodiscovery */
		fdisk_context_force_sector_size(cxt, sector_size);

	if (user_cylinders || user_heads || user_sectors)
		fdisk_context_set_user_geometry(cxt, user_cylinders,
					user_heads, user_sectors);

	if (!fdisk_dev_has_disklabel(cxt)) {
		/*
		 * Try BSD -- TODO: move to list_table() too
		 */
		update_units(cxt);
		list_disk_geometry(cxt);
		if (!fdisk_is_disklabel(cxt, AIX) &&
		    !fdisk_is_disklabel(cxt, MAC))
			btrydev(cxt);
	} else
		list_table(cxt, 0);

	fdisk_free_context(cxt);
	cxt = NULL;
}

/*
 * for fdisk -l:
 * try all things in /proc/partitions that look like a full disk
 */
static void
print_all_partition_table_from_option(unsigned long sector_size)
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
					print_partition_table_from_option(cn, sector_size);
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

	if (fdisk_is_disklabel(cxt, OSF)) {
		putchar('\n');
		/* OSF label, and no DOS label */
		printf(_("Detected an OSF/1 disklabel on %s, entering "
			 "disklabel mode.\n"),
		       cxt->dev_path);
		bsd_command_prompt(cxt);
		/* If we return we may want to make an empty DOS label? */
		cxt->disklabel = FDISK_DISKLABEL_DOS;
	}

	while (1) {
		putchar('\n');
		c = tolower(read_char(_("Command (m for help): ")));
		switch (c) {
		case 'a':
			if (fdisk_is_disklabel(cxt, DOS))
				toggle_active(get_partition(cxt, 1, partitions));
			else if (fdisk_is_disklabel(cxt, SUN))
				toggle_sunflags(cxt, get_partition(cxt, 1, partitions),
						SUN_FLAG_UNMNT);
			else if (fdisk_is_disklabel(cxt, SGI))
				sgi_set_bootpartition(cxt,
					get_partition(cxt, 1, partitions));
			else
				unknown_command(c);
			break;
		case 'b':
			if (fdisk_is_disklabel(cxt, SGI))
				sgi_set_bootfile(cxt);
			else if (fdisk_is_disklabel(cxt, DOS)) {
				cxt->disklabel = FDISK_DISKLABEL_OSF;
				bsd_command_prompt(cxt);
				cxt->disklabel = FDISK_DISKLABEL_DOS;
			} else
				unknown_command(c);
			break;
		case 'c':
			if (fdisk_is_disklabel(cxt, DOS))
				toggle_dos_compatibility_flag(cxt);
			else if (fdisk_is_disklabel(cxt, SUN))
				toggle_sunflags(cxt, get_partition(cxt, 1, partitions),
						SUN_FLAG_RONLY);
			else if (fdisk_is_disklabel(cxt, SGI))
				sgi_set_swappartition(cxt,
					get_partition(cxt, 1, partitions));
			else
				unknown_command(c);
			break;
		case 'd':
			delete_partition(cxt, get_existing_partition(cxt, 1, partitions));
			break;
		case 'g':
			fdisk_create_disklabel(cxt, "gpt");
			break;
		case 'i':
			if (fdisk_is_disklabel(cxt, SGI))
				create_sgiinfo(cxt);
			else
				unknown_command(c);
			break;
		case 'l':
			list_partition_types(cxt);
			break;
		case 'm':
			print_menu(cxt, MAIN_MENU);
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
			change_units(cxt);
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
			print_menu(cxt, MAIN_MENU);
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
	struct fdisk_context *cxt = NULL;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

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
			break;
		case 'C':
			user_cylinders =  strtou32_or_err(optarg, _("invalid cylinders argument"));
			break;
		case 'c':
			dos_compatible_flag = 0;	/* default */

			if (optarg && !strcmp(optarg, "=dos"))
				dos_compatible_flag = ~0;
			else if (optarg && strcmp(optarg, "=nondos"))
				usage(stderr);
			break;
		case 'H':
			user_heads = strtou32_or_err(optarg, _("invalid heads argument"));
			if (user_heads > 256)
				user_heads = 0;
			break;
		case 'S':
			user_sectors =  strtou32_or_err(optarg, _("invalid sectors argument"));
			if (user_sectors >= 64)
				user_sectors = 0;
			break;
		case 'l':
			optl = 1;
			break;
		case 's':
			opts = 1;
			break;
		case 'u':
			display_in_cyl_units = 0;		/* default */
			if (optarg && strcmp(optarg, "=cylinders") == 0)
				display_in_cyl_units = !display_in_cyl_units;
			else if (optarg && strcmp(optarg, "=sectors"))
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

	fdisk_init_debug(0);

	if (sector_size && argc-optind != 1)
		printf(_("Warning: the -b (set sector size) option should"
			 " be used with one specified device\n"));

	if (optl) {
		nowarn = 1;
		if (argc > optind) {
			int k;
			for (k = optind; k < argc; k++)
				print_partition_table_from_option(argv[k], sector_size);
		} else
			print_all_partition_table_from_option(sector_size);
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

	cxt = fdisk_new_context_from_filename(argv[optind], 0);
	if (!cxt)
		err(EXIT_FAILURE, _("cannot open %s"), argv[optind]);

	if (sector_size)	/* passed -b option, override autodiscovery */
		fdisk_context_force_sector_size(cxt, sector_size);

	if (user_cylinders || user_heads || user_sectors)
		fdisk_context_set_user_geometry(cxt, user_cylinders,
						user_heads, user_sectors);

	print_welcome();

	if (!fdisk_dev_sectsz_is_default(cxt))
		printf(_("Note: sector size is %ld (not %d)\n"),
		       cxt->sector_size, DEFAULT_SECTOR_SIZE);

	if (!fdisk_dev_has_disklabel(cxt)) {
		update_units(cxt);	/* to provide compatible 'p'rint output */
		fprintf(stderr,
			_("Device does not contain a recognized partition table\n"));
		fdisk_create_disklabel(cxt, NULL);
	}

	command_prompt(cxt);

	return 0;
}
