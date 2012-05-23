/* fdisk.c -- Partition table manipulator for Linux.
 *
 * Copyright (C) 1992  A. V. Le Blanc (LeBlanc@mcc.ac.uk)
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
#ifdef HAVE_LIBBLKID
#include <blkid.h>
#endif

#include "gpt.h"

unsigned char *MBRbuffer;
int MBRbuffer_changed;
struct fdisk_context *cxt = NULL;

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
	enum labeltype label[2];		/* disklabel types associated with main and expert menu */
};

static const struct menulist_descr menulist[] = {
	{'a', N_("toggle a bootable flag"), {DOS_LABEL, 0}},
	{'a', N_("toggle a read only flag"), {SUN_LABEL, 0}},
	{'a', N_("select bootable partition"), {SGI_LABEL, 0}},
	{'a', N_("change number of alternate cylinders"), {0, SUN_LABEL}},
	{'b', N_("edit bsd disklabel"), {DOS_LABEL, 0}},
	{'b', N_("edit bootfile entry"), {SGI_LABEL, 0}},
	{'b', N_("move beginning of data in a partition"), {0, DOS_LABEL}},
	{'c', N_("toggle the dos compatibility flag"), {DOS_LABEL, 0}},
	{'c', N_("toggle the mountable flag"), {SUN_LABEL, 0}},
	{'c', N_("select sgi swap partition"), {SGI_LABEL, 0}},
	{'c', N_("change number of cylinders"), {0, DOS_LABEL | SUN_LABEL}},
	{'d', N_("delete a partition"), {DOS_LABEL | SUN_LABEL | SGI_LABEL | OSF_LABEL, 0}},
	{'d', N_("print the raw data in the partition table"), {0, ANY_LABEL}},
	{'e', N_("change number of extra sectors per cylinder"), {0, SUN_LABEL}},
	{'e', N_("list extended partitions"), {0, DOS_LABEL}},
	{'e', N_("edit drive data"), {OSF_LABEL, 0}},
	{'f', N_("fix partition order"), {0, DOS_LABEL}},
	{'g', N_("create an IRIX (SGI) partition table"), {0, ANY_LABEL}},
	{'h', N_("change number of heads"), {0, DOS_LABEL | SUN_LABEL}},
	{'i', N_("change interleave factor"), {0, SUN_LABEL}},
	{'i', N_("change the disk identifier"), {0, DOS_LABEL}},
	{'i', N_("install bootstrap"), {OSF_LABEL, 0}},
	{'l', N_("list known partition types"), {DOS_LABEL | SUN_LABEL | SGI_LABEL | OSF_LABEL, 0}},
	{'m', N_("print this menu"), {ANY_LABEL, ANY_LABEL}},
	{'n', N_("add a new partition"), {DOS_LABEL | SUN_LABEL | SGI_LABEL | OSF_LABEL, 0}},
	{'o', N_("create a new empty DOS partition table"), {~OSF_LABEL, 0}},
	{'o', N_("change rotation speed (rpm)"), {0, SUN_LABEL}},
	{'p', N_("print the partition table"), {DOS_LABEL | SUN_LABEL | SGI_LABEL | OSF_LABEL, DOS_LABEL | SUN_LABEL}},
	{'q', N_("quit without saving changes"), {ANY_LABEL, ANY_LABEL}},
	{'r', N_("return to main menu"), {OSF_LABEL, DOS_LABEL | SUN_LABEL | SGI_LABEL | OSF_LABEL}},
	{'s', N_("create a new empty Sun disklabel"), {~OSF_LABEL, 0}},
	{'s', N_("change number of sectors/track"), {0, DOS_LABEL | SUN_LABEL}},
	{'s', N_("show complete disklabel"), {OSF_LABEL, 0}},
	{'t', N_("change a partition's system id"), {DOS_LABEL | SUN_LABEL | SGI_LABEL | OSF_LABEL, 0}},
	{'u', N_("change display/entry units"), {DOS_LABEL | SUN_LABEL | SGI_LABEL | OSF_LABEL, 0}},
	{'v', N_("verify the partition table"), {DOS_LABEL | SUN_LABEL | SGI_LABEL, DOS_LABEL | SUN_LABEL | SGI_LABEL}},
	{'w', N_("write table to disk and exit"), {DOS_LABEL | SUN_LABEL | SGI_LABEL, DOS_LABEL | SUN_LABEL | SGI_LABEL}},
	{'w', N_("write disklabel to disk"), {OSF_LABEL, 0}},
	{'x', N_("extra functionality (experts only)"), {DOS_LABEL | SUN_LABEL | SGI_LABEL, 0}},
#if !defined (__alpha__)
	{'x', N_("link BSD partition to non-BSD partition"), {OSF_LABEL, 0}},
#endif
	{'y', N_("change number of physical cylinders"), {0, SUN_LABEL}},
};

int
valid_part_table_flag(unsigned char *b) {
	return (b[510] == 0x55 && b[511] == 0xaa);
}

unsigned long long
get_nr_sects(struct partition *p) {
	return read4_little_endian(p->size4);
}

char	*line_ptr,			/* interactive input */
	line_buffer[LINE_LENGTH];

int	nowarn = 0,			/* no warnings for fdisk -l/-s */
	dos_compatible_flag = 0,	/* disabled by default */
	dos_changed = 0,
	partitions = 4;			/* maximum partition + 1 */

unsigned int	user_cylinders, user_heads, user_sectors;
unsigned int   pt_heads, pt_sectors;

unsigned long long sector_offset = 1, sectors;

unsigned int	heads,
	cylinders,
	sector_size = DEFAULT_SECTOR_SIZE,
	user_set_sector_size = 0,
	units_per_sector = 1,
	display_in_cyl_units = 0;

unsigned long long total_number_of_sectors;	/* in logical sectors */
unsigned long grain = DEFAULT_SECTOR_SIZE,
	      io_size = DEFAULT_SECTOR_SIZE,
	      min_io_size = DEFAULT_SECTOR_SIZE,
	      phy_sector_size = DEFAULT_SECTOR_SIZE,
	      alignment_offset;
int has_topology;

enum labeltype disklabel;	/* Current disklabel */

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

void fatal(enum failure why)
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

void print_menu(enum menutype menu)
{
	size_t i;

	puts(_("Command action"));

	for (i = 0; i < ARRAY_SIZE(menulist); i++)
		if (menulist[i].label[menu] & disklabel)
			printf("   %c   %s\n", menulist[i].command, menulist[i].description);
}

static int
get_sysid(int i) {
	return (
		disklabel == SUN_LABEL ? sun_get_sysid(i) :
		disklabel == SGI_LABEL ? sgi_get_sysid(i) :
		ptes[i].part_table->sys_ind);
}

static struct systypes *
get_sys_types(void) {
	return (
		disklabel == SUN_LABEL ? sun_sys_types :
		disklabel == SGI_LABEL ? sgi_sys_types :
		i386_sys_types);
}

char *partition_type(unsigned char type)
{
	int i;
	struct systypes *types = get_sys_types();

	for (i=0; types[i].name; i++)
		if (types[i].type == type)
			return _(types[i].name);

	return NULL;
}

void list_types(struct systypes *sys)
{
	unsigned int last[4], done = 0, next = 0, size;
	int i;

	for (i = 0; sys[i].name; i++);
	size = i;

	for (i = 3; i >= 0; i--)
		last[3 - i] = done += (size + i - done) / (i + 1);
	i = done = 0;

	do {
		#define NAME_WIDTH 15
		char name[NAME_WIDTH * MB_LEN_MAX];
		size_t width = NAME_WIDTH;

		printf("%c%2x  ", i ? ' ' : '\n', sys[next].type);
		size_t ret = mbsalign(_(sys[next].name), name, sizeof(name),
				      &width, MBS_ALIGN_LEFT, 0);
		if (ret == (size_t)-1 || ret >= sizeof(name))
			printf("%-15.15s", _(sys[next].name));
		else
			fputs(name, stdout);

		next = last[i++] + done;
		if (i > 3 || next >= last[i]) {
			i = 0;
			next = ++done;
		}
	} while (done < last[0]);
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
lba_is_aligned(unsigned long long lba)
{
	unsigned int granularity = max(phy_sector_size, min_io_size);
	unsigned long long offset = (lba * sector_size) & (granularity - 1);

	return !((granularity + alignment_offset - offset) & (granularity - 1));
}

unsigned long long align_lba(unsigned long long lba, int direction)
{
	unsigned long long res;

	if (lba_is_aligned(lba))
		res = lba;
	else {
		unsigned long long sects_in_phy = grain / sector_size;

		if (lba < sector_offset)
			res = sector_offset;

		else if (direction == ALIGN_UP)
			res = ((lba + sects_in_phy) / sects_in_phy) * sects_in_phy;

		else if (direction == ALIGN_DOWN)
			res = (lba / sects_in_phy) * sects_in_phy;

		else /* ALIGN_NEAREST */
			res = ((lba + sects_in_phy / 2) / sects_in_phy) * sects_in_phy;

		if (alignment_offset && !lba_is_aligned(res) &&
		    res > alignment_offset / sector_size) {
			/*
			 * apply alignment_offset
			 *
			 * On disk with alignment compensation physical blocks starts
			 * at LBA < 0 (usually LBA -1). It means we have to move LBA
			 * according the offset to be on the physical boundary.
			 */
			/* fprintf(stderr, "LBA: %llu apply alignment_offset\n", res); */
			res -= (max(phy_sector_size, min_io_size) -
					alignment_offset) / sector_size;

			if (direction == ALIGN_UP && res < lba)
				res += sects_in_phy;
		}
	}

	/***
	 fprintf(stderr, "LBA %llu (%s) --align-(%s)--> %llu (%s)\n",
				lba,
				lba_is_aligned(lba) ? "OK" : "FALSE",
				direction == ALIGN_UP ?   "UP     " :
				direction == ALIGN_DOWN ? "DOWN   " : "NEAREST",
				res,
				lba_is_aligned(res) ? "OK" : "FALSE");
	***/
	return res;
}

int warn_geometry(void)
{
	char *m = NULL;
	int prev = 0;

	if (disklabel == SGI_LABEL)	/* cannot set cylinders etc anyway */
		return 0;
	if (!heads)
		prev = test_c(&m, _("heads"));
	if (!sectors)
		prev = test_c(&m, _("sectors"));
	if (!cylinders)
		prev = test_c(&m, _("cylinders"));
	if (!m)
		return 0;
	fprintf(stderr,
		_("%s%s.\nYou can do this from the extra functions menu.\n"),
		prev ? _(" and ") : " ", m);
	return 1;
}

void update_units(void)
{
	int cyl_units = heads * sectors;

	if (display_in_cyl_units && cyl_units)
		units_per_sector = cyl_units;
	else
		units_per_sector = 1;	/* in sectors */
}

void warn_limits(void)
{
	if (total_number_of_sectors > UINT_MAX && !nowarn) {
		unsigned long long bytes = total_number_of_sectors * sector_size;
		int giga = bytes / 1000000000;
		int hectogiga = (giga + 50) / 100;

		fprintf(stderr, _("\n"
"WARNING: The size of this disk is %d.%d TB (%llu bytes).\n"
"DOS partition table format can not be used on drives for volumes\n"
"larger than (%llu bytes) for %d-byte sectors. Use parted(1) and GUID \n"
"partition table format (GPT).\n\n"),
			hectogiga / 10, hectogiga % 10,
			bytes,
			(unsigned long long ) UINT_MAX * sector_size,
			sector_size);
	}
}

void warn_alignment(void)
{
	if (nowarn)
		return;

	if (sector_size != phy_sector_size)
		fprintf(stderr, _("\n"
"The device presents a logical sector size that is smaller than\n"
"the physical sector size. Aligning to a physical sector (or optimal\n"
"I/O) size boundary is recommended, or performance may be impacted.\n"));

	if (dos_compatible_flag)
		fprintf(stderr, _("\n"
"WARNING: DOS-compatible mode is deprecated. It's strongly recommended to\n"
"         switch off the mode (with command 'c')."));

	if (display_in_cyl_units)
		fprintf(stderr, _("\n"
"WARNING: cylinders as display units are deprecated. Use command 'u' to\n"
"         change units to sectors.\n"));

}

static void
get_topology(int fd) {
	int arg;
#ifdef HAVE_LIBBLKID
	blkid_probe pr;

	pr = blkid_new_probe();
	if (pr && blkid_probe_set_device(pr, cxt->dev_fd, 0, 0) == 0) {
		blkid_topology tp = blkid_probe_get_topology(pr);

		if (tp) {
			min_io_size = blkid_topology_get_minimum_io_size(tp);
			io_size = blkid_topology_get_optimal_io_size(tp);
			phy_sector_size = blkid_topology_get_physical_sector_size(tp);
			alignment_offset = blkid_topology_get_alignment_offset(tp);

			/* We assume that the device provides topology info if
			 * optimal_io_size is set or alignment_offset is set or
			 * minimum_io_size is not power of 2.
			 *
			 * See also update_sector_offset().
			 */
			if (io_size || alignment_offset ||
			    (min_io_size & (min_io_size - 1)))
				has_topology = 1;
			if (!io_size)
				/* optimal IO is optional, default to minimum IO */
				io_size = min_io_size;
		}
	}
	blkid_free_probe(pr);
#endif

	if (user_set_sector_size)
		/* fdisk since 2.17 differentiate between logical and physical
		 * sectors size. For backward compatibility the
		 *    fdisk -b <sectorsize>
		 * changes both, logical and physical sector size.
		 */
		phy_sector_size = sector_size;

	else if (blkdev_get_sector_size(cxt->dev_fd, &arg) == 0) {
		sector_size = arg;

		if (!phy_sector_size)
			phy_sector_size = sector_size;
	}

	if (!min_io_size)
		min_io_size = phy_sector_size;
	if (!io_size)
		io_size = min_io_size;

	if (sector_size != DEFAULT_SECTOR_SIZE)
		printf(_("Note: sector size is %d (not %d)\n"),
		       sector_size, DEFAULT_SECTOR_SIZE);
}

static void
get_partition_table_geometry(void) {
	unsigned char *bufp = MBRbuffer;
	struct partition *p;
	int i, h, s, hh, ss;
	int first = 1;
	int bad = 0;

	if (!(valid_part_table_flag(bufp)))
		return;

	hh = ss = 0;
	for (i=0; i<4; i++) {
		p = pt_offset(bufp, i);
		if (p->sys_ind != 0) {
			h = p->end_head + 1;
			s = (p->end_sector & 077);
			if (first) {
				hh = h;
				ss = s;
				first = 0;
			} else if (hh != h || ss != s)
				bad = 1;
		}
	}

	if (!first && !bad) {
		pt_heads = hh;
		pt_sectors = ss;
	}
}

/*
 * Sets LBA of the first partition
 */
void
update_sector_offset(void)
{
	grain = io_size;

	if (dos_compatible_flag)
		sector_offset = sectors;	/* usually 63 sectors */
	else {
		/*
		 * Align the begin of partitions to:
		 *
		 * a) topology
		 *  a2) alignment offset
		 *  a1) or physical sector (minimal_io_size, aka "grain")
		 *
		 * b) or default to 1MiB (2048 sectrors, Windows Vista default)
		 *
		 * c) or for very small devices use 1 phy.sector
		 */
		unsigned long long x = 0;

		if (has_topology) {
			if (alignment_offset)
				x = alignment_offset;
			else if (io_size > 2048 * 512)
				x = io_size;
		}
		/* default to 1MiB */
		if (!x)
			x = 2048 * 512;

		sector_offset = x / sector_size;

		/* don't use huge offset on small devices */
		if (total_number_of_sectors <= sector_offset * 4)
			sector_offset = phy_sector_size / sector_size;

		/* use 1MiB grain always when possible */
		if (grain < 2048 * 512)
			grain = 2048 * 512;

		/* don't use huge grain on small devices */
		if (total_number_of_sectors <= (grain * 4 / sector_size))
			grain = phy_sector_size;
	}
}

void
get_geometry(int fd, struct geom *g) {
	unsigned long long llcyls, nsects = 0;
	unsigned int kern_heads = 0, kern_sectors = 0;

	get_topology(fd);
	heads = cylinders = sectors = 0;
	pt_heads = pt_sectors = 0;

	blkdev_get_geometry(fd, &kern_heads, &kern_sectors);
	get_partition_table_geometry();

	heads = user_heads ? user_heads :
		pt_heads ? pt_heads :
		kern_heads ? kern_heads : 255;
	sectors = user_sectors ? user_sectors :
		pt_sectors ? pt_sectors :
		kern_sectors ? kern_sectors : 63;

	/* get number of 512-byte sectors, and convert it the real sectors */
	if (blkdev_get_sectors(fd, &nsects) == 0)
		total_number_of_sectors = (nsects / (sector_size >> 9));

	update_sector_offset();

	llcyls = total_number_of_sectors / (heads * sectors);
	cylinders = llcyls;
	if (cylinders != llcyls)	/* truncated? */
		cylinders = ~0;
	if (!cylinders)
		cylinders = user_cylinders;

	if (g) {
		g->heads = heads;
		g->sectors = sectors;
		g->cylinders = cylinders;
	}
}

/*
 * Please, always use allocated buffer if you want to cast the buffer to
 * any struct -- cast non-allocated buffer to any struct is against
 * strict-aliasing rules.  --kzak 16-Oct-2009
 */
static void init_mbr_buffer(void)
{
	if (MBRbuffer)
		return;

	MBRbuffer = xcalloc(1, MAX_SECTOR_SIZE);
}

void zeroize_mbr_buffer(void)
{
	if (MBRbuffer)
		memset(MBRbuffer, 0, MAX_SECTOR_SIZE);
}



/*
 * Read MBR.  Returns:
 *   -1: no 0xaa55 flag present (possibly entire disk BSD)
 *    0: found or created label
 *    1: I/O error
 */
static int get_boot(int try_only) {

	disklabel = ANY_LABEL;
	memset(MBRbuffer, 0, 512);

	if (512 != read(cxt->dev_fd, MBRbuffer, 512)) {
		if (try_only)
			return 1;
		fatal(unable_to_read);
	}

	get_geometry(cxt->dev_fd, NULL);
	update_units();

	if (!check_dos_label())
		if (check_sun_label() || check_sgi_label() || check_aix_label() || check_mac_label())
			return 0;

	if (check_osf_label()) {
		/* intialize partitions for BSD as well */
		dos_init();
		if (!valid_part_table_flag(MBRbuffer)) {
			disklabel = OSF_LABEL;
			return 0;
		}
		printf(_("This disk has both DOS and BSD magic.\n"
			 "Give the 'b' command to go to BSD mode.\n"));
		return 0;
	}

	if (disklabel == ANY_LABEL) {
		if (try_only)
			return -1;

		fprintf(stderr,
			_("Device does not contain a recognized partition table\n"));
#ifdef __sparc__
		create_sunlabel();
#else
		create_doslabel();
#endif
	}
	return 0;
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

int
read_hex(struct systypes *sys)
{
        int hex;

        while (1)
        {
           read_char(_("Hex code (type L to list codes): "));
           if (tolower(*line_ptr) == 'l')
               list_types(sys);
	   else if (isxdigit (*line_ptr))
	   {
	      hex = 0;
	      do
		 hex = hex << 4 | hex_val(*line_ptr++);
	      while (isxdigit(*line_ptr));
	      return hex;
	   }
        }
}

unsigned int
read_int_with_suffix(unsigned int low, unsigned int dflt, unsigned int high,
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
					res *= heads * sectors;
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
				unit = sector_size * units_per_sector;
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
read_int(unsigned int low, unsigned int dflt, unsigned int high,
	 unsigned int base, char *mesg)
{
	return read_int_with_suffix(low, dflt, high, base, mesg, NULL);
}

int
get_partition_dflt(int warn, int max, int dflt) {
	struct pte *pe;
	int i;

	i = read_int(1, dflt, max, 0, _("Partition number")) - 1;
	pe = &ptes[i];

	if (warn) {
		if ((disklabel != SUN_LABEL && disklabel != SGI_LABEL && !pe->part_table->sys_ind)
		    || (disklabel == SUN_LABEL &&
			(!sunlabel->partitions[i].num_sectors ||
			 !sunlabel->part_tags[i].tag))
		    || (disklabel == SGI_LABEL && (!sgi_get_num_sectors(i)))
		   )
			fprintf(stderr,
				_("Warning: partition %d has empty type\n"),
				i+1);
	}
	return i;
}

int
get_partition(int warn, int max) {
	return get_partition_dflt(warn, max, 0);
}

/* User partition selection unless one partition only is available */

static int
get_existing_partition(int warn, int max) {
	int pno = -1;
	int i;

	if (disklabel != DOS_LABEL)
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
	return get_partition(warn, max);
}

const char *
str_units(int n)
{
	if (display_in_cyl_units)
		return P_("cylinder", "cylinders", n);
	return P_("sector", "sectors", n);
}

void change_units(void)
{
	display_in_cyl_units = !display_in_cyl_units;
	update_units();

	if (display_in_cyl_units)
		printf(_("Changing display/entry units to cylinders (DEPRECATED!)\n"));
	else
		printf(_("Changing display/entry units to sectors\n"));
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
toggle_dos_compatibility_flag(void) {
	dos_compatible_flag = ~dos_compatible_flag;
	if (dos_compatible_flag)
		printf(_("DOS Compatibility flag is set (DEPRECATED!)\n"));
	else
		printf(_("DOS Compatibility flag is not set\n"));

	update_sector_offset();
}

static void
delete_partition(int i)
{
	if (i < 0)
		return;

	if (warn_geometry())
		return;		/* C/H/S not set */

	ptes[i].changed = 1;

	if (disklabel == DOS_LABEL)
		dos_delete_partition(i);
	else if (disklabel == SUN_LABEL)
		sun_delete_partition(i);
	else if (disklabel == SGI_LABEL)
		sgi_delete_partition(i);

	printf(_("Partition %d is deleted\n"), i + 1);
}

static void
change_sysid(void) {
	char *temp;
	int i, sys, origsys;
	struct partition *p;

	i = get_existing_partition(0, partitions);

	if (i == -1)
		return;
	p = ptes[i].part_table;
	origsys = sys = get_sysid(i);

	/* if changing types T to 0 is allowed, then
	   the reverse change must be allowed, too */
	if (!sys && disklabel != SGI_LABEL && disklabel != SUN_LABEL && !get_nr_sects(p))
                printf(_("Partition %d does not exist yet!\n"), i + 1);
        else while (1) {
		sys = read_hex (get_sys_types());

		if (!sys && disklabel != SGI_LABEL && disklabel != SUN_LABEL) {
			printf(_("Type 0 means free space to many systems\n"
			       "(but not to Linux). Having partitions of\n"
			       "type 0 is probably unwise. You can delete\n"
			       "a partition using the `d' command.\n"));
			/* break; */
		}

		if (disklabel != SGI_LABEL && disklabel != SUN_LABEL) {
			if (IS_EXTENDED (sys) != IS_EXTENDED (p->sys_ind)) {
				printf(_("You cannot change a partition into"
				       " an extended one or vice versa\n"
				       "Delete it first.\n"));
				break;
			}
		}

                if (sys < 256) {
			if (disklabel == SUN_LABEL && i == 2 && sys != SUN_TAG_BACKUP)
				printf(_("Consider leaving partition 3 "
				       "as Whole disk (5),\n"
				       "as SunOS/Solaris expects it and "
				       "even Linux likes it.\n\n"));
			if (disklabel == SGI_LABEL && ((i == 10 && sys != ENTIRE_DISK)
					  || (i == 8 && sys != 0)))
				printf(_("Consider leaving partition 9 "
				       "as volume header (0),\nand "
				       "partition 11 as entire volume (6), "
				       "as IRIX expects it.\n\n"));
                        if (sys == origsys)
				break;
			if (disklabel == SUN_LABEL) {
				ptes[i].changed = sun_change_sysid(i, sys);
			} else
			if (disklabel == SGI_LABEL) {
				ptes[i].changed = sgi_change_sysid(i, sys);
			} else {
				p->sys_ind = sys;
				ptes[i].changed = 1;
			}
			temp = partition_type(sys) ? : _("Unknown");
			if (ptes[i].changed)
				printf (_("Changed system type of partition %d "
				        "to %x (%s)\n"), i + 1, sys, temp);
			else
				printf (_("System type of partition %d is unchanged: "
				        "%x (%s)\n"), i + 1, sys, temp);
			if (is_dos_partition(origsys) ||
			    is_dos_partition(sys))
				dos_changed = 1;
                        break;
                }
        }
}

/* check_consistency() and long2chs() added Sat Mar 6 12:28:16 1993,
 * faith@cs.unc.edu, based on code fragments from pfdisk by Gordon W. Ross,
 * Jan.  1990 (version 1.2.1 by Gordon W. Ross Aug. 1990; Modified by S.
 * Lubkin Oct.  1991). */

static void
long2chs(unsigned long ls, unsigned int *c, unsigned int *h, unsigned int *s) {
	int spc = heads * sectors;

	*c = ls / spc;
	ls = ls % spc;
	*h = ls / sectors;
	*s = ls % sectors + 1;	/* sectors count from 1 */
}

static void check_consistency(struct partition *p, int partition) {
	unsigned int pbc, pbh, pbs;	/* physical beginning c, h, s */
	unsigned int pec, peh, pes;	/* physical ending c, h, s */
	unsigned int lbc, lbh, lbs;	/* logical beginning c, h, s */
	unsigned int lec, leh, les;	/* logical ending c, h, s */

	if (!dos_compatible_flag)
		return;

	if (!heads || !sectors || (partition >= 4))
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
	long2chs(get_start_sect(p), &lbc, &lbh, &lbs);

/* compute logical ending (c, h, s) */
	long2chs(get_start_sect(p) + get_nr_sects(p) - 1, &lec, &leh, &les);

/* Same physical / logical beginning? */
	if (cylinders <= 1024 && (pbc != lbc || pbh != lbh || pbs != lbs)) {
		printf(_("Partition %d has different physical/logical "
			"beginnings (non-Linux?):\n"), partition + 1);
		printf(_("     phys=(%d, %d, %d) "), pbc, pbh, pbs);
		printf(_("logical=(%d, %d, %d)\n"),lbc, lbh, lbs);
	}

/* Same physical / logical ending? */
	if (cylinders <= 1024 && (pec != lec || peh != leh || pes != les)) {
		printf(_("Partition %d has different physical/logical "
			"endings:\n"), partition + 1);
		printf(_("     phys=(%d, %d, %d) "), pec, peh, pes);
		printf(_("logical=(%d, %d, %d)\n"),lec, leh, les);
	}

/* Ending on cylinder boundary? */
	if (peh != (heads - 1) || pes != sectors) {
		printf(_("Partition %i does not end on cylinder boundary.\n"),
			partition + 1);
	}
}

static void
check_alignment(unsigned long long lba, int partition)
{
	if (!lba_is_aligned(lba))
		printf(_("Partition %i does not start on physical sector boundary.\n"),
			partition + 1);
}

static void
list_disk_geometry(void) {
	unsigned long long bytes = total_number_of_sectors * sector_size;
	long megabytes = bytes/1000000;

	if (megabytes < 10000)
		printf(_("\nDisk %s: %ld MB, %lld bytes\n"),
		       cxt->dev_path, megabytes, bytes);
	else {
		long hectomega = (megabytes + 50) / 100;
		printf(_("\nDisk %s: %ld.%ld GB, %llu bytes\n"),
		       cxt->dev_path, hectomega / 10, hectomega % 10, bytes);
	}
	printf(_("%d heads, %llu sectors/track, %d cylinders"),
	       heads, sectors, cylinders);
	if (units_per_sector == 1)
		printf(_(", total %llu sectors"), total_number_of_sectors);
	printf("\n");
	printf(_("Units = %s of %d * %d = %d bytes\n"),
	       str_units(PLURAL),
	       units_per_sector, sector_size, units_per_sector * sector_size);

	printf(_("Sector size (logical/physical): %u bytes / %lu bytes\n"),
				sector_size, phy_sector_size);
	printf(_("I/O size (minimum/optimal): %lu bytes / %lu bytes\n"),
				min_io_size, io_size);
	if (alignment_offset)
		printf(_("Alignment offset: %lu bytes\n"), alignment_offset);
	if (disklabel == DOS_LABEL)
		dos_print_mbr_id();
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

static void
list_table(int xtra) {
	struct partition *p;
	char *type;
	int i, w;

	if (disklabel == SUN_LABEL) {
		sun_list_table(xtra);
		return;
	}

	if (disklabel == SGI_LABEL) {
		sgi_list_table(xtra);
		return;
	}

	list_disk_geometry();

	if (disklabel == OSF_LABEL) {
		xbsd_print_disklabel(xtra);
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

			if (sector_size < 1024) {
				pblocks /= (1024 / sector_size);
				podd = psects % (1024 / sector_size);
			}
			if (sector_size > 1024)
				pblocks *= (sector_size / 1024);
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
/* type name */		(type = partition_type(p->sys_ind)) ?
			type : _("Unknown"));
			check_consistency(p, i);
			check_alignment(get_partition_start(pe), i);
		}
	}

	/* Is partition table in disk order? It need not be, but... */
	/* partition table entries are not checked for correct order if this
	   is a sgi, sun or aix labeled disk... */
	if (disklabel == DOS_LABEL && wrong_p_order(NULL)) {
		printf(_("\nPartition table entries are not in disk order\n"));
	}
}

static void
x_list_table(int extend) {
	struct pte *pe;
	struct partition *p;
	int i;

	printf(_("\nDisk %s: %d heads, %llu sectors, %d cylinders\n\n"),
		cxt->dev_path, heads, sectors, cylinders);
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
				check_consistency(p, i);
				check_alignment(get_partition_start(pe), i);
			}
		}
	}
}

void fill_bounds(unsigned long long *first, unsigned long long *last)
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

static void
check(int n, unsigned int h, unsigned int s, unsigned int c,
      unsigned int start) {
	unsigned int total, real_s, real_c;

	real_s = sector(s) - 1;
	real_c = cylinder(s, c);
	total = (real_c * sectors + real_s) * heads + h;
	if (!total)
		fprintf(stderr, _("Warning: partition %d contains sector 0\n"), n);
	if (h >= heads)
		fprintf(stderr,
			_("Partition %d: head %d greater than maximum %d\n"),
			n, h + 1, heads);
	if (real_s >= sectors)
		fprintf(stderr, _("Partition %d: sector %d greater than "
			"maximum %llu\n"), n, s, sectors);
	if (real_c >= cylinders)
		fprintf(stderr, _("Partitions %d: cylinder %d greater than "
			"maximum %d\n"), n, real_c + 1, cylinders);
	if (cylinders <= 1024 && start != total)
		fprintf(stderr,
			_("Partition %d: previous sectors %d disagrees with "
			"total %d\n"), n, start, total);
}

static void
verify(void) {
	int i, j;
	unsigned long long total = 1;
	unsigned long long n_sectors = total_number_of_sectors;
	unsigned long long first[partitions], last[partitions];
	struct partition *p;

	if (warn_geometry())
		return;

	if (disklabel == SUN_LABEL) {
		verify_sun();
		return;
	}

	if (disklabel == SGI_LABEL) {
		verify_sgi(1);
		return;
	}

	fill_bounds(first, last);
	for (i = 0; i < partitions; i++) {
		struct pte *pe = &ptes[i];

		p = pe->part_table;
		if (p->sys_ind && !IS_EXTENDED (p->sys_ind)) {
			check_consistency(p, i);
			check_alignment(get_partition_start(pe), i);
			if (get_partition_start(pe) < first[i])
				printf(_("Warning: bad start-of-data in "
					"partition %d\n"), i + 1);
			check(i + 1, p->end_head, p->end_sector, p->end_cyl,
				last[i]);
			total += last[i] + 1 - first[i];
			for (j = 0; j < i; j++)
			if ((first[i] >= first[j] && first[i] <= last[j])
			 || ((last[i] <= last[j] && last[i] >= first[j]))) {
				printf(_("Warning: partition %d overlaps "
					"partition %d.\n"), j + 1, i + 1);
				total += first[i] >= first[j] ?
					first[i] : first[j];
				total -= last[i] <= last[j] ?
					last[i] : last[j];
			}
		}
	}

	if (extended_offset) {
		struct pte *pex = &ptes[ext_index];
		unsigned long long e_last = get_start_sect(pex->part_table) +
			get_nr_sects(pex->part_table) - 1;

		for (i = 4; i < partitions; i++) {
			total++;
			p = ptes[i].part_table;
			if (!p->sys_ind) {
				if (i != 4 || i + 1 < partitions)
					printf(_("Warning: partition %d "
						"is empty\n"), i + 1);
			}
			else if (first[i] < extended_offset ||
					last[i] > e_last)
				printf(_("Logical partition %d not entirely in "
					"partition %d\n"), i + 1, ext_index + 1);
		}
	}

	if (total > n_sectors)
		printf(_("Total allocated sectors %llu greater than the maximum"
			" %llu\n"), total, n_sectors);
	else if (total < n_sectors)
		printf(_("Remaining %lld unallocated %d-byte sectors\n"),
		       n_sectors - total, sector_size);
}

void print_partition_size(int num, unsigned long long start, unsigned long long stop, int sysid)
{
	char *str = size_to_human_string(SIZE_SUFFIX_3LETTER | SIZE_SUFFIX_SPACE,
				     (stop - start + 1) * sector_size);
	printf(_("Partition %d of type %s and of size %s is set\n"), num, partition_type(sysid), str);
	free(str);
}

static void new_partition(void)
{
	if (warn_geometry())
		return;

	if (disklabel == SUN_LABEL) {
		add_sun_partition(get_partition(0, partitions), LINUX_NATIVE);
		return;
	}

	if (disklabel == SGI_LABEL) {
		sgi_add_partition(get_partition(0, partitions), LINUX_NATIVE);
		return;
	}

	if (disklabel == AIX_LABEL) {
		printf(_("\tSorry - this fdisk cannot handle AIX disk labels."
			 "\n\tIf you want to add DOS-type partitions, create"
			 "\n\ta new empty DOS partition table first. (Use o.)"
			 "\n\tWARNING: "
			 "This will destroy the present disk contents.\n"));
		return;
	}

	if (disklabel == MAC_LABEL) {
		printf(_("\tSorry - this fdisk cannot handle Mac disk labels."
		         "\n\tIf you want to add DOS-type partitions, create"
		         "\n\ta new empty DOS partition table first. (Use o.)"
		         "\n\tWARNING: "
		         "This will destroy the present disk contents.\n"));
		 return;
	}

	/* default to DOS/BSD */
	dos_new_partition();
}

static void
write_table(void) {
	int i;

	if (disklabel == DOS_LABEL)
		dos_write_table();
	else if (disklabel == SGI_LABEL)
		/* no test on change? the printf below might be mistaken */
		sgi_write_table();
	else if (disklabel == SUN_LABEL) {
		int needw = 0;

		for (i=0; i<8; i++)
			if (ptes[i].changed)
				needw = 1;
		if (needw)
			sun_write_table();
	}

	printf(_("The partition table has been altered!\n\n"));
	reread_partition_table(1);
}

void
reread_partition_table(int leave) {
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

	if (dos_changed)
	    printf(
		_("\nWARNING: If you have created or modified any DOS 6.x\n"
		"partitions, please see the fdisk manual page for additional\n"
		"information.\n"));

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
print_buffer(unsigned char pbuffer[]) {
	unsigned int i, l;

	for (i = 0, l = 0; i < sector_size; i++, l++) {
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

static void
print_raw(void) {
	int i;

	printf(_("Device: %s\n"), cxt->dev_path);
	if (disklabel == SUN_LABEL || disklabel == SGI_LABEL)
		print_buffer(MBRbuffer);
	else for (i = 3; i < partitions; i++)
		print_buffer(ptes[i].sectorbuffer);
}

static void
move_begin(int i) {
	struct pte *pe = &ptes[i];
	struct partition *p = pe->part_table;
	unsigned int new, free_start, curr_start, last;
	int x;

	if (warn_geometry())
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

	new = read_int(free_start, curr_start, last, free_start,
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
expert_command_prompt(void)
{
	char c;

	while(1) {
		putchar('\n');
		c = tolower(read_char(_("Expert command (m for help): ")));
		switch (c) {
		case 'a':
			if (disklabel == SUN_LABEL)
				sun_set_alt_cyl();
			break;
		case 'b':
			if (disklabel == DOS_LABEL)
				move_begin(get_partition(0, partitions));
			break;
		case 'c':
			user_cylinders = cylinders =
				read_int(1, cylinders, 1048576, 0,
					 _("Number of cylinders"));
			if (disklabel == SUN_LABEL)
				sun_set_ncyl(cylinders);
			break;
		case 'd':
			print_raw();
			break;
		case 'e':
			if (disklabel == SGI_LABEL)
				sgi_set_xcyl();
			else if (disklabel == SUN_LABEL)
				sun_set_xcyl();
			else
			if (disklabel == DOS_LABEL)
				x_list_table(1);
			break;
		case 'f':
			if (disklabel == DOS_LABEL)
				fix_partition_table_order();
			break;
		case 'g':
			create_sgilabel();
			break;
		case 'h':
			user_heads = heads = read_int(1, heads, 256, 0,
					 _("Number of heads"));
			update_units();
			break;
		case 'i':
			if (disklabel == SUN_LABEL)
				sun_set_ilfact();
			else if (disklabel == DOS_LABEL)
				dos_set_mbr_id();
			break;
		case 'o':
			if (disklabel == SUN_LABEL)
				sun_set_rspeed();
			break;
		case 'p':
			if (disklabel == SUN_LABEL)
				list_table(1);
			else
				x_list_table(0);
			break;
		case 'q':
			handle_quit(cxt);
		case 'r':
			return;
		case 's':
			user_sectors = sectors = read_int(1, sectors, 63, 0,
					   _("Number of sectors"));
			if (dos_compatible_flag)
				fprintf(stderr, _("Warning: setting "
					"sector offset for DOS "
					"compatiblity\n"));
			update_sector_offset();
			update_units();
			break;
		case 'v':
			verify();
			break;
		case 'w':
			write_table(); 	/* does not return */
			break;
		case 'y':
			if (disklabel == SUN_LABEL)
				sun_set_pcylcount();
			break;
		default:
			print_menu(EXPERT_MENU);
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

static void
gpt_warning(char *dev)
{
	if (dev && gpt_probe_signature_devname(dev))
		fprintf(stderr, _("\nWARNING: GPT (GUID Partition Table) detected on '%s'! "
			"The util fdisk doesn't support GPT. Use GNU Parted.\n\n"), dev);
}

/* Print disk geometry and partition table of a specified device (-l option) */

static void
print_partition_table_from_option(char *device)
{
	int gb;

	cxt = fdisk_new_context_from_filename(device, 1);	/* read-only */
	if (!cxt)
		err(EXIT_FAILURE, _("unable to open %s"), device);

	gpt_warning(device);
	gb = get_boot(1);
	if (gb < 0) { /* no DOS signature */
		list_disk_geometry();
		if (disklabel != AIX_LABEL && disklabel != MAC_LABEL)
			btrydev(device);
	}
	else if (!gb)
		list_table(0);
	fdisk_free_context(cxt);
	cxt = NULL;
}

/*
 * for fdisk -l:
 * try all things in /proc/partitions that look like a full disk
 */
static void
print_all_partition_table_from_option(void)
{
	FILE *procpt;
	char line[128], ptname[128], devname[256];
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
					print_partition_table_from_option(cn);
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

static void command_prompt(void)
{
	int c;

	if (disklabel == OSF_LABEL) {
		putchar('\n');
		/* OSF label, and no DOS label */
		printf(_("Detected an OSF/1 disklabel on %s, entering "
			 "disklabel mode.\n"),
		       cxt->dev_path);
		bsd_command_prompt();
		/* If we return we may want to make an empty DOS label? */
		disklabel = DOS_LABEL;
	}

	while (1) {
		putchar('\n');
		c = tolower(read_char(_("Command (m for help): ")));
		switch (c) {
		case 'a':
			if (disklabel == DOS_LABEL)
				toggle_active(get_partition(1, partitions));
			else if (disklabel == SUN_LABEL)
				toggle_sunflags(get_partition(1, partitions),
						SUN_FLAG_UNMNT);
			else if (disklabel == SGI_LABEL)
				sgi_set_bootpartition(
					get_partition(1, partitions));
			else
				unknown_command(c);
			break;
		case 'b':
			if (disklabel == SGI_LABEL)
				sgi_set_bootfile();
			else if (disklabel == DOS_LABEL) {
				disklabel = OSF_LABEL;
				bsd_command_prompt();
				disklabel = DOS_LABEL;
			} else
				unknown_command(c);
			break;
		case 'c':
			if (disklabel == DOS_LABEL)
				toggle_dos_compatibility_flag();
			else if (disklabel == SUN_LABEL)
				toggle_sunflags(get_partition(1, partitions),
						SUN_FLAG_RONLY);
			else if (disklabel == SGI_LABEL)
				sgi_set_swappartition(
						get_partition(1, partitions));
			else
				unknown_command(c);
			break;
		case 'd':
			delete_partition(get_existing_partition(1, partitions));
			break;
		case 'i':
			if (disklabel == SGI_LABEL)
				create_sgiinfo();
			else
				unknown_command(c);
			break;
		case 'l':
			list_types(get_sys_types());
			break;
		case 'm':
			print_menu(MAIN_MENU);
			break;
		case 'n':
			new_partition();
			break;
		case 'o':
			create_doslabel();
			break;
		case 'p':
			list_table(0);
			break;
		case 'q':
			handle_quit(cxt);
		case 's':
			create_sunlabel();
			break;
		case 't':
			change_sysid();
			break;
		case 'u':
			change_units();
			break;
		case 'v':
			verify();
			break;
		case 'w':
			write_table(); 		/* does not return */
			break;
		case 'x':
			expert_command_prompt();
			break;
		default:
			unknown_command(c);
			print_menu(MAIN_MENU);
		}
	}
}

static unsigned long long get_dev_blocks(char *dev)
{
	int fd;
	unsigned long long size;

	if ((fd = open(dev, O_RDONLY)) < 0)
		err(EXIT_FAILURE, _("unable to open %s"), dev);
	if (blkdev_get_sectors(fd, &size) == -1)
		fatal(ioctl_error);
	close(fd);
	return size/2;
}

int main(int argc, char **argv)
{
	int c, optl = 0, opts = 0;

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
			sector_offset = 2;
			user_set_sector_size = 1;
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

	if (user_set_sector_size && argc-optind != 1)
		printf(_("Warning: the -b (set sector size) option should"
			 " be used with one specified device\n"));

	init_mbr_buffer();

	if (optl) {
		nowarn = 1;
		if (argc > optind) {
			int k;
			for (k = optind; k < argc; k++)
				print_partition_table_from_option(argv[k]);
		} else
			print_all_partition_table_from_option();
		exit(0);
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

	if (argc-optind == 1) {
		cxt = fdisk_new_context_from_filename(argv[optind], 0);
		if (!cxt)
			err(EXIT_FAILURE, _("unable to open %s"), argv[optind]);
	}
	else
		usage(stderr);

	printf(_("Welcome to fdisk (%s).\n\n"
		"Changes will remain in memory only, until you decide to write them.\n"
		"Be careful before using the write command.\n\n"), PACKAGE_STRING);

	gpt_warning(cxt->dev_path);
	get_boot(0);

	command_prompt();

	return 0;
}
