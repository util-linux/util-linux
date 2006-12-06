/* fdisk.c -- Partition table manipulator for Linux.
 *
 * Copyright (C) 1992  A. V. Le Blanc (LeBlanc@mcc.ac.uk)
 *
 * This program is free software.  You can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation: either version 1 or
 * (at your option) any later version.
 *
 * Before Linux version 0.95c, this program requires a kernel patch.
 *
 * Modified, Tue Feb  2 18:46:49 1993, faith@cs.unc.edu to better support SCSI.
 * Modified, Sat Feb 27 18:11:27 1993, faith@cs.unc.edu: added extfs support.
 * Modified, Sat Mar  6 10:14:12 1993, faith@cs.unc.edu: added more comments.
 * Modified, Sat Mar  6 12:25:45 1993, faith@cs.unc.edu:
 *	Added patches from Michael Bischoff (i1041905@ws.rz.tu-bs.de
 *	or mbi@mo.math.nat.tu-bs.de) to fix the following problems:
 *	1) Incorrect mapping of head/sector/cylinder to absolute sector
 *	2) Odd sector count causes one sector to be lost
 * Modified, Sat Mar  6 12:25:52 1993, faith@cs.unc.edu: improved verification.
 * Modified, Sat Apr 17 15:00:00 1993, LeBlanc@mcc.ac.uk: add -s, fix -l.
 * Modified, Sat Apr 24 10:00:00 1993, LeBlanc@mcc.ac.uk: fix overlap bug.
 * Modified, Wed May  5 21:30:00 1993, LeBlanc@mcc.ac.uk: errors to stderr.
 * Modified, Mon Mar 21 20:00:00 1994, LeBlanc@mcc.ac.uk:
 *	more stderr for messages, avoid division by 0, and
 *	give reboot message only if ioctl(fd, BLKRRPART) fails.
 * Modified, Mon Apr 25 01:01:05 1994, martin@cs.unc.edu:
 *    1) Added support for DOS, OS/2, ... compatibility.  We should be able
 *       use this fdisk to partition our drives for other operating systems.
 *    2) Added a print the raw data in the partition table command.
 * Modified, Wed Jun 22 21:05:30 1994, faith@cs.unc.edu:
 *    Added/changed a few partition type names to conform to cfdisk.
 *    (suggested by Sujal, smpatel@wam.umd.edu)
 * Modified 3/5/95 leisner@sdsp.mc.xerox.com -- on -l only open
 *    devices RDONLY (instead of RDWR).  This allows you to
 *    have the disks as rw-r----- with group disk (and if you
 *    want is safe to setguid fdisk to disk).
 * Modified Sat Mar 11 10:02 1995 with more partition types, faith@cs.unc.edu
 * Modified, Thu May  4 01:11:45 1995, esr@snark.thyrsus.com:
 *	It's user-interface cleanup time.
 *	Actual error messages for out-of-bounds values (what a concept!).
 *	Enable read-only access to partition table for learners.
 *	Smart defaults for most numeric prompts.
 * Fixed a bug preventing a partition from crossing cylinder 8064, aeb, 950801.
 * Read partition table twice to avoid kernel bug
 *     (from Daniel Quinlan <quinlan@yggdrasil.com>), Tue Sep 26 10:25:28 1995
 * Modified, Sat Jul  1 23:43:16 MET DST 1995, fasten@cs.bonn.edu:
 *      editor for NetBSD/i386 (and Linux/Alpha?) disklabels.
 * Tue Sep 26 17:07:54 1995: More patches from aeb.  Fix segfaults, all >4GB.
 *   Don't destroy random data if extd partition starts past 4GB, aeb, 950818.
 *   Don't segfault on bad partition created by previous fdisk.
 * Modified, Fri Jul 14 11:13:35 MET DST 1996, jj@sunsite.mff.cuni.cz:
 *      editor for Sun disklabels.
 * Modified, Wed Jul  3 10:14:17 MET DST 1996, jj@sunsite.mff.cuni.cz:
 *      support for Sun floppies 
 * Modified, Thu Jul 24 16:42:33 MET DST 1997, fasten@shw.com:
 *   LINUX_EXTENDED support
 * Added Windows 95 partition types, aeb.
 * Fixed a bug described by johnf@whitsunday.net.au, aeb, 980408.
 *   [There are lots of other bugs - nobody should use this program]
 *   [cfdisk on the other hand is nice and correct]
 * Try to avoid reading a CD-ROM.
 * Do not print Begin column -- it confuses too many people -- aeb, 980610.
 */


#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <setjmp.h>
#include <errno.h>
#include <getopt.h>

#include <sys/stat.h>
#include <sys/ioctl.h>

#include <linux/hdreg.h>       /* for HDIO_GETGEO */
#include <linux/fs.h>          /* for BLKRRPART, BLKGETSIZE */

#include "fdisk.h"
#if defined(sparc)
#include "fdisksunlabel.h"
#endif

#define hex_val(c)	({ \
				char _c = (c); \
				isdigit(_c) ? _c - '0' : \
				tolower(_c) + 10 - 'a'; \
			})


#define VERSION	"2.8"		/* util-linux version */

#define DEFAULT_DEVICE	"/dev/hda"
#define ALTERNATE_DEVICE "/dev/sda"
#define LINE_LENGTH	80
#define offset(b, n)	((struct partition *)((b) + 0x1be + \
				(n) * sizeof(struct partition)))
#define sector(s)	((s) & 0x3f)
#define cylinder(s, c)	((c) | (((s) & 0xc0) << 2))

#define calculate(h,s,c) (sector(s) - 1 + sectors * \
				((h) + heads * cylinder(s,c)))
#define set_hsc(h,s,c,sector) { \
				s = sector % sectors + 1;	\
				sector /= sectors;	\
				h = sector % heads;	\
				sector /= heads;	\
				c = sector & 0xff;	\
				s |= (sector >> 2) & 0xc0;	\
			}

/* A valid partition table sector ends in 0x55 0xaa */
unsigned int
part_table_flag(char *b) {
	return ((uint) b[510]) + (((uint) b[511]) << 8);
}

int
valid_part_table_flag(unsigned char *b) {
	return (b[510] == 0x55 && b[511] == 0xaa);
}

void
write_part_table_flag(char *b) {
	b[510] = 0x55;
	b[511] = 0xaa;
}

/* start_sect and nr_sects are stored little endian on all machines */
/* moreover, they are not aligned correctly */
void
store4_little_endian(unsigned char *cp, unsigned int val) {
	cp[0] = (val & 0xff);
	cp[1] = ((val >> 8) & 0xff);
	cp[2] = ((val >> 16) & 0xff);
	cp[3] = ((val >> 24) & 0xff);
}

unsigned int
read4_little_endian(unsigned char *cp) {
	return (uint)(cp[0]) + ((uint)(cp[1]) << 8)
		+ ((uint)(cp[2]) << 16) + ((uint)(cp[3]) << 24);
}

void
set_start_sect(struct partition *p, unsigned int start_sect) {
	store4_little_endian(p->start4, start_sect);
}

unsigned int
get_start_sect(struct partition *p) {
	return read4_little_endian(p->start4);
}

void
set_nr_sects(struct partition *p, unsigned int nr_sects) {
	store4_little_endian(p->size4, nr_sects);
}

unsigned int
get_nr_sects(struct partition *p) {
	return read4_little_endian(p->size4);
}

#define ACTIVE_FLAG	0x80

#define EXTENDED	0x05
#define WIN98_EXTENDED  0x0f
#define LINUX_PARTITION	0x81
#define LINUX_SWAP	0x82
#define LINUX_NATIVE	0x83
#define LINUX_EXTENDED	0x85

#define IS_EXTENDED(i) \
	((i) == EXTENDED || (i) == WIN98_EXTENDED || (i) == LINUX_EXTENDED)

/* normally O_RDWR, -l option gives O_RDONLY */
static int type_open = O_RDWR;

char	*disk_device = DEFAULT_DEVICE,	/* hda, unless specified */
	*line_ptr,			/* interactive input */
	line_buffer[LINE_LENGTH],
	changed[MAXIMUM_PARTS],		/* marks changed buffers */
	buffer[SECTOR_SIZE],		/* first four partitions */
	*buffers[MAXIMUM_PARTS]		/* pointers to buffers */
		= {buffer, buffer, buffer, buffer};

int	fd,				/* the disk */
	ext_index,			/* the prime extended partition */
	listing = 0,			/* no aborts for fdisk -l */
	nowarn = 0,			/* no warnings for fdisk -l/-s */
	show_begin = 0,
	dos_compatible_flag = ~0,
	partitions = 4;			/* maximum partition + 1 */

uint	heads,
	sectors,
	cylinders,
	sector_offset = 1,
	display_factor = 1,		/* in units/sector */
	unit_flag = 1,
	full_bits = 0,			/* 1024 cylinders in sectors */
	extended_offset = 0,		/* offset of link pointers */
	offsets[MAXIMUM_PARTS] = {0, 0, 0, 0};

int     sun_label = 0;                  /* looking at sun disklabel */

struct	partition *part_table[MAXIMUM_PARTS]	/* partitions */
		= {offset(buffer, 0), offset(buffer, 1),
		offset(buffer, 2), offset(buffer, 3)},
	*ext_pointers[MAXIMUM_PARTS]		/* link pointers */
		= {NULL, NULL, NULL, NULL};

struct systypes sys_types[] = {
	{0x00, "Empty"},
	{0x01, "DOS 12-bit FAT"},
	{0x02, "XENIX root"},
	{0x03, "XENIX usr"},
	{0x04, "DOS 16-bit <32M"},
	{0x05, "Extended"},
	{0x06, "DOS 16-bit >=32M"},
	{0x07, "OS/2 HPFS"},		/* or QNX? */
	{0x08, "AIX"},
	{0x09, "AIX bootable"},
	{0x0a, "OS/2 Boot Manager"},
	{0x0b, "Win95 FAT32"},
	{0x0c, "Win95 FAT32 (LBA)"},
	{0x0e, "Win95 FAT16 (LBA)"},
	{0x0f, "Win95 Extended (LBA)"},
	{0x40, "Venix 80286"},
	{0x51, "Novell?"},
	{0x52, "Microport"},		/* or CPM? */
	{0x63, "GNU HURD"},		/* or System V/386? */
	{0x64, "Novell Netware 286"},
	{0x65, "Novell Netware 386"},
	{0x75, "PC/IX"},
	{0x80, "Old MINIX"},		/* Minix 1.4a and earlier */

	{LINUX_PARTITION, "Linux/MINIX"}, /* Minix 1.4b and later */
	{LINUX_SWAP,      "Linux swap"},
	{LINUX_NATIVE,    "Linux native"},
	{LINUX_EXTENDED,  "Linux extended"},

	{0x93, "Amoeba"},
	{0x94, "Amoeba BBT"},		/* (bad block table) */
	{0xa5, "BSD/386"},
	{0xa6, "OpenBSD"},
	{0xa7, "NEXTSTEP"},
	{0xb7, "BSDI fs"},
	{0xb8, "BSDI swap"},
	{0xc7, "Syrinx"},
	{0xdb, "CP/M"},			/* or Concurrent DOS? */
	{0xe1, "DOS access"},
	{0xe3, "DOS R/O"},
	{0xf2, "DOS secondary"},
	{0xff, "BBT"},			/* (bad track table) */
	{ 0, NULL }
};

jmp_buf listingbuf;

void fatal(enum failure why)
{
	char	error[LINE_LENGTH],
		*message = error;

	if (listing) {
		close(fd);
		longjmp(listingbuf, 1);
	}

	switch (why) {
		case usage: message =
"Usage: fdisk [-b] [-u] [/dev/hdx]     Change partition table\n"
"       fdisk -l [-b] [-u] [/dev/hdx]  List partition table(s)\n"
"       fdisk -s /dev/hdxn             Give partition size(s)\n"
"       fdisk -v                       Give fdisk version\n";
			break;
		case unable_to_open:
			sprintf(error, "Unable to open %s\n", disk_device);
			break;
		case unable_to_read:
			sprintf(error, "Unable to read %s\n", disk_device);
			break;
		case unable_to_seek:
			sprintf(error, "Unable to seek on %s\n", disk_device);
			break;
		case unable_to_write:
			sprintf(error, "Unable to write %s\n", disk_device);
			break;
		case out_of_memory:
			message = "Unable to allocate any more memory\n";
			break;
		default: message = "Fatal error\n";
	}

	fputc('\n', stderr);
	fputs(message, stderr);
	exit(1);
}

void menu(void)
{
	if (sun_label)
	   puts("Command action\n"
		"   a   toggle a read only flag\n" 		/* sun */
		"   b   edit bsd disklabel\n"
		"   c   toggle the mountable flag\n" 		/* sun */
		"   d   delete a partition\n"
		"   l   list known partition types\n"
		"   m   print this menu\n"
		"   n   add a new partition\n"
		"   o   create a new empty DOS partition table\n"
		"   p   print the partition table\n"
		"   q   quit without saving changes\n"
		"   s   create a new empty Sun disklabel\n"	/* sun */
		"   t   change a partition's system id\n"
		"   u   change display/entry units\n"
		"   v   verify the partition table\n"
		"   w   write table to disk and exit\n"
		"   x   extra functionality (experts only)"
	   );
	else
	   puts("Command action\n"
		"   a   toggle a bootable flag\n"
		"   b   edit bsd disklabel\n"
		"   c   toggle the dos compatibility flag\n"
		"   d   delete a partition\n"
		"   l   list known partition types\n"
		"   m   print this menu\n"
		"   n   add a new partition\n"
		"   o   create a new empty DOS partition table\n"
		"   p   print the partition table\n"
		"   q   quit without saving changes\n"
		"   t   change a partition's system id\n"
		"   u   change display/entry units\n"
		"   v   verify the partition table\n"
		"   w   write table to disk and exit\n"
		"   x   extra functionality (experts only)"
	   );
}

void xmenu(void)
{
	if (sun_label)
	   puts("Command action\n"
		"   a   change number of alternate cylinders\n"	/* sun */
		"   c   change number of cylinders\n"
		"   d   print the raw data in the partition table\n"
		"   e   change number of extra sectors per cylinder\n" /*sun*/
		"   h   change number of heads\n"
		"   i   change interleave factor\n"		/* sun */
		"   o   change rotation speed (rpm)\n"		/* sun */
		"   m   print this menu\n"
		"   p   print the partition table\n"
		"   q   quit without saving changes\n"
		"   r   return to main menu\n"
		"   s   change number of sectors\n"
		"   v   verify the partition table\n"
		"   w   write table to disk and exit\n"
		"   y   change number of physical cylinders"	/* sun */
	   );
	else
	   puts("Command action\n"
		"   b   move beginning of data in a partition\n" /* !sun */
		"   c   change number of cylinders\n"
		"   d   print the raw data in the partition table\n"
		"   e   list extended partitions\n"		/* sun */
		"   h   change number of heads\n"
		"   m   print this menu\n"
		"   p   print the partition table\n"
		"   q   quit without saving changes\n"
		"   r   return to main menu\n"
		"   s   change number of sectors\n"
		"   v   verify the partition table\n"
		"   w   write table to disk and exit"
	   );
}

char *partition_type(unsigned char type)
{
	int i;
	struct systypes *types;

#if defined(sparc)
	if (sun_label)
		types = sun_sys_types;
	else
#endif
		types = sys_types;

	for (i=0; types[i].name; i++)
		if (types[i].index == type)
			return types[i].name;

	return NULL;
}

void list_types(struct systypes *sys)
{
	uint last[4], done = 0, next = 0, size;
	int i;

	for (i = 0; sys[i].name; i++);
	size = i;

	for (i = 3; i >= 0; i--)
		last[3 - i] = done += (size + i - done) / (i + 1);
	i = done = 0;

	do {
		printf("%c%2x  %-15.15s", i ? ' ' : '\n',
		        sys[next].index, sys[next].name);
 		next = last[i++] + done;
		if (i > 3 || next >= last[i]) {
			i = 0;
			next = ++done;
		}
	} while (done < last[0]);
	putchar('\n');
}

void clear_partition(struct partition *p)
{
	p->boot_ind = 0;
	p->head = 0;
	p->sector = 0;
	p->cyl = 0;
	p->sys_ind = 0;
	p->end_head = 0;
	p->end_sector = 0;
	p->end_cyl = 0;
	set_start_sect(p,0);
	set_nr_sects(p,0);
}

void set_partition(int i, struct partition *p, uint start, uint stop,
	int sysid, uint offset)
{
	p->boot_ind = 0;
	p->sys_ind = sysid;
	set_start_sect(p, start - offset);
	set_nr_sects(p, stop - start + 1);
	if (dos_compatible_flag && (start/(sectors*heads) > 1023))
		start = heads*sectors*1024 - 1;
	set_hsc(p->head, p->sector, p->cyl, start);
	if (dos_compatible_flag && (stop/(sectors*heads) > 1023))
		stop = heads*sectors*1024 - 1;
	set_hsc(p->end_head, p->end_sector, p->end_cyl, stop);
	changed[i] = 1;
}

int test_c(char **m, char *mesg)
{
	int val = 0;
	if (!*m)
		fprintf(stderr, "You must set");
	else {
		fprintf(stderr, " %s", *m);
		val = 1;
	}
	*m = mesg;
	return val;
}

int warn_geometry(void)
{
	char *m = NULL;
	int prev = 0;
	if (!heads)
		prev = test_c(&m, "heads");
	if (!sectors)
		prev = test_c(&m, "sectors");
	if (!cylinders)
		prev = test_c(&m, "cylinders");
	if (!m)
		return 0;
	fprintf(stderr,
		"%s%s.\nYou can do this from the extra functions menu.\n",
		prev ? " and " : " ", m);
	return 1;
}

uint rounded(uint calcul, uint start)
{
	uint i;
	if (!full_bits)
		return calcul;
	while ((i = calcul + full_bits) <= start)
		calcul = i;
	return calcul;
}

void update_units(void)
{
	full_bits = 1024 * heads * sectors;
	if (unit_flag && full_bits)
		display_factor = full_bits >> 10;
	else display_factor = 1;
}

void warn_cylinders(void)
{
	if (!sun_label && cylinders > 1024 && !nowarn)
		fprintf(stderr,
			"The number of cylinders for this disk is set to %d.\n"
			"This is larger than 1024, and may cause "
			"problems with:\n"
			"1) software that runs at boot time (e.g., LILO)\n"
			"2) booting and partitioning software from other OSs\n"
			"   (e.g., DOS FDISK, OS/2 FDISK)\n",
			cylinders);
}

void read_extended(struct partition *p)
{
	int i;
	struct partition *q;

	ext_pointers[ext_index] = part_table[ext_index];
	if (!get_start_sect(p))
		fprintf(stderr, "Bad offset in primary extended partition\n");
	else while (IS_EXTENDED (p->sys_ind)) {
		if (partitions >= MAXIMUM_PARTS) {
			fprintf(stderr,
				"Warning: deleting partitions after %d\n",
				partitions);
			clear_partition(ext_pointers[partitions - 1]);
			changed[partitions - 1] = 1;
			return;
		}
		offsets[partitions] = extended_offset + get_start_sect(p);
		if (!extended_offset)
			extended_offset = get_start_sect(p);
		if (ext2_llseek(fd, (ext2_loff_t)offsets[partitions]
			       * SECTOR_SIZE, SEEK_SET) < 0)
			fatal(unable_to_seek);
		if (!(buffers[partitions] = (char *) malloc(SECTOR_SIZE)))
			fatal(out_of_memory);
		if (SECTOR_SIZE != read(fd, buffers[partitions], SECTOR_SIZE))
			fatal(unable_to_read);
		part_table[partitions] = ext_pointers[partitions] = NULL;
		q = p = offset(buffers[partitions], 0);
		for (i = 0; i < 4; i++, p++) {
			if (IS_EXTENDED (p->sys_ind))
				if (ext_pointers[partitions])
					fprintf(stderr, "Warning: extra link "
						"pointer in partition table "
						"%d\n", partitions + 1);
				else
					ext_pointers[partitions] = p;
			else if (p->sys_ind)
				if (part_table[partitions])
					fprintf(stderr,
						"Warning: ignoring extra data "
						"in partition table %d\n",
						partitions + 1);
				else
					part_table[partitions] = p;
		}
		if (!part_table[partitions])
			if (q != ext_pointers[partitions])
				part_table[partitions] = q;
			else part_table[partitions] = q + 1;
		if (!ext_pointers[partitions])
			if (q != part_table[partitions])
				ext_pointers[partitions] = q;
			else ext_pointers[partitions] = q + 1;
		p = ext_pointers[partitions++];
	}
}

void create_doslabel(void)
{
	int i;

	fprintf(stderr,
	"Building a new DOS disklabel. Changes will remain in memory only,\n"
	"until you decide to write them. After that, of course, the previous\n"
	"content won't be recoverable.\n\n");

	write_part_table_flag(buffer);
	for (i = 0; i < 4; i++)
	    clear_partition(part_table[i]);
	for (i = 1; i < MAXIMUM_PARTS; i++)
	    changed[i] = 0;
	changed[0] = 1;
	get_boot(create_empty);
}

/*
 * Read MBR.  Returns:
 *   -1: no 0xaa55 flag present (possibly entire disk BSD)
 *    0: found or created label
 */
int get_boot(enum action what)
{
	int i;
	struct hd_geometry geometry;

	partitions = 4;

	if (what == create_empty)
		goto got_table;		/* skip reading disk */

	if ((fd = open(disk_device, type_open)) < 0) {
	    if ((fd = open(disk_device, O_RDONLY)) < 0)
		fatal(unable_to_open);
	    else
		printf("You will not be able to write the partition table.\n");
	}

#if defined(sparc)
	guess_device_type(fd);
#endif

	if (SECTOR_SIZE != read(fd, buffer, SECTOR_SIZE))
		fatal(unable_to_read);

#ifdef HDIO_REQ
	if (!ioctl(fd, HDIO_REQ, &geometry)) {
#else
	if (!ioctl(fd, HDIO_GETGEO, &geometry)) {
#endif
		heads = geometry.heads;
		sectors = geometry.sectors;
		cylinders = geometry.cylinders;
		if (dos_compatible_flag)
			sector_offset = sectors;
	} else {
		if (!ioctl(fd, BLKGETSIZE, &sectors)) {
			heads = 1;
			cylinders = 1;
		} else {
			heads = cylinders = sectors = 0;
		}
	}
	update_units();

got_table:

#if defined(sparc)
	if (check_sun_label())
		return 0;
#endif

	if (!valid_part_table_flag(buffer)) {
		switch(what) {
		case fdisk:
			fprintf(stderr,
				"Device contains neither a valid DOS partition"
				" table, nor Sun disklabel\n");
#ifdef __sparc__
			create_sunlabel();
#else
			create_doslabel();
#endif
			return 0;
		case require:
			return -1;
		case try_only:
		        return -1;
		case create_empty:
			break;
		}

		fprintf(stderr, "Internal error\n");
		exit(1);
	}

	warn_cylinders();
	warn_geometry();

	for (i = 0; i < 4; i++)
		if(IS_EXTENDED (part_table[i]->sys_ind))
			if (partitions != 4)
				fprintf(stderr, "Ignoring extra extended "
					"partition %d\n", i + 1);
			else read_extended(part_table[ext_index = i]);

	for (i = 3; i < partitions; i++)
		if (!valid_part_table_flag(buffers[i])) {
			fprintf(stderr,
				"Warning: invalid flag 0x%04x of partition "
				"table %d will be corrected by w(rite)\n",
				part_table_flag(buffers[i]), i + 1);
			changed[i] = 1;
		}

	return 0;
}

int read_line(void)
{
	if (!fgets(line_buffer, LINE_LENGTH, stdin))
		return 0;
	line_ptr = line_buffer;
	while (*line_ptr && !isgraph(*line_ptr))
		line_ptr++;
	return *line_ptr;
}

char read_char(char *mesg)
{
	do {
		fputs(mesg, stdout);
	} while (!read_line());
	return *line_ptr;
}

char read_chars(char *mesg)
{
        fputs(mesg, stdout);
        if (!read_line()) {
		*line_ptr = '\n';
		return '\n';
        } else
		return *line_ptr;
}

int read_hex(struct systypes *sys)
{
        int hex;

        while (1)
        {
           read_char("Hex code (type L to list codes): ");
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


uint read_int(uint low, uint dflt, uint high, enum offset base, char *mesg)
{
	uint i, use_default = 1;
	char ms[70];

	switch(base) {
	case lower:
	    sprintf(ms, "%s ([%d]-%d): ", mesg, low, high);
	    break;
	case upper:
	    sprintf(ms, "%s (%d-[%d]): ", mesg, low, high);
	    break;
	case deflt:
	    sprintf(ms, "%s (%d-[%d]-%d): ", mesg, low, dflt, high);
	    break;
	default:
	    sprintf(ms, "%s (%d-%d): ", mesg, low, high);
	    break;
	}

	while (1) {
		if (base == deflt) {
			while (read_chars(ms) != '\n' && !isdigit(*line_ptr)
		       		    && *line_ptr != '-' && *line_ptr != '+')
				continue;
			if (*line_ptr == '\n')
				return dflt;
		} else {
			while (!isdigit(read_char(ms))
				    && *line_ptr != '-' && *line_ptr != '+')
				continue;
		}
		if (*line_ptr == '+' || *line_ptr == '-') {
			if (*line_ptr == '+')
			    ++line_ptr;
			i = atoi(line_ptr);
 			while (isdigit(*line_ptr))
			{
 				line_ptr++;
				use_default = 0;
			}
			switch (*line_ptr) {
				case 'c':
				case 'C': if (!unit_flag)
						i *= heads * sectors;
					break;
				case 'k':
				case 'K': i *= 2;
					i /= display_factor;
					break;
				case 'm':
				case 'M': i *= 2048;
					i /= display_factor;
					break;
				default: break;
			}
			switch(base) {
			case lower: i += low; break;
			case upper: i += high; break;
			case deflt: i += dflt; break;
			case ignore:
			}
		}
		else
		{
			i = atoi(line_ptr);
			while (isdigit(*line_ptr))
			{
				line_ptr++;
				use_default = 0;
			}
		}
		if (use_default)
		    printf("Using default value %d\n", i = dflt);
		if (i >= low && i <= high)
			break;
		else
			printf("Value out of range.\n");
	}
	return i;
}

int get_partition(int warn, int max)
{
	/*
	 * try to pick a default least likely to do damage,
	 * in case luser just types a newline
	 */
	int i = read_int(1, max, max, ignore, "Partition number") - 1;

	if (warn && (
	     (!sun_label && !part_table[i]->sys_ind)
#if defined(sparc)
	     || (sun_label &&
		(!sunlabel->partitions[i].num_sectors ||
		 !sunlabel->infos[i].id))
#endif
	)) fprintf(stderr, "Warning: partition %d has empty type\n", i+1);
	return i;
}

char *const str_units(void)
{
	return unit_flag ? "cylinder" : "sector";
}

void change_units(void)
{
	if ((unit_flag = !unit_flag))
		display_factor = 1;
	else display_factor = heads * sectors;
	update_units();
	printf("Changing display/entry units to %ss\n",
		str_units());
}

void toggle_active(int i)
{
	struct partition *p = part_table[i];

	if (IS_EXTENDED (p->sys_ind) && !p->boot_ind)
		fprintf(stderr,
			"WARNING: Partition %d is an extended partition\n",
			i + 1);
	if (p->boot_ind)
		p->boot_ind = 0;
	else p->boot_ind = ACTIVE_FLAG;
	changed[i] = 1;
}

void toggle_dos(void)
{
	dos_compatible_flag = ~dos_compatible_flag;
	printf("DOS Compatibility flag is ");
	if (dos_compatible_flag)
		sector_offset = sectors;
	else {
		sector_offset = 1;
		printf("not ");
	}
	printf("set\n");
}

void delete_partition(int i)
{
	struct partition *p = part_table[i], *q = ext_pointers[i];

/* Note that for the fifth partition (i == 4) we don't actually
 * decrement partitions.
 */

	if (warn_geometry())
		return;
	changed[i] = 1;
#if defined(sparc)
	if (sun_label) {
		sun_delete_partition(i);
		return;
	}
#endif
	if (i < 4) {
		if (IS_EXTENDED (p->sys_ind) && i == ext_index) {
			while (partitions > 4)
				free(buffers[--partitions]);
			ext_pointers[ext_index] = NULL;
			extended_offset = 0;
		}
		clear_partition(p);
	}
	else if (!q->sys_ind && i > 4) {
		free(buffers[--partitions]);
		clear_partition(ext_pointers[--i]);
	}
	else if (i > 3) {
		if (i > 4) {
			p = ext_pointers[i - 1];
			p->boot_ind = 0;
			p->head = q->head;
			p->sector = q->sector;
			p->cyl = q->cyl;
			p->sys_ind = EXTENDED;
			p->end_head = q->end_head;
			p->end_sector = q->end_sector;
			p->end_cyl = q->end_cyl;
			set_start_sect(p, get_start_sect(q));
			set_nr_sects(p, get_nr_sects(q));
			changed[i - 1] = 1;
		} else {
			if(part_table[5]) /* prevent SEGFAULT */
				set_start_sect(part_table[5],
					       get_start_sect(part_table[5]) +
					       offsets[5] - extended_offset);
			offsets[5] = extended_offset;
			changed[5] = 1;
		}
		if (partitions > 5) {
			partitions--;
			free(buffers[i]);
			while (i < partitions) {
				changed[i] = changed[i + 1];
				buffers[i] = buffers[i + 1];
				offsets[i] = offsets[i + 1];
				part_table[i] = part_table[i + 1];
				ext_pointers[i] = ext_pointers[i + 1];
				i++;
			}
		}
		else
			clear_partition(part_table[i]);
	}
}

void change_sysid(void)
{
	char *temp;
	int i = get_partition(0, partitions), sys, origsys;
	struct partition *p = part_table[i];

#if defined(sparc)
	if (sun_label)
		sys = sunlabel->infos[i].id;
	else
#endif
		sys = p->sys_ind;
	origsys = sys;

	if (!sys)
                printf("Partition %d does not exist yet!\n", i + 1);
        else while (1) {
		if (!sun_label)
			sys = read_hex (sys_types);
#if defined(sparc)
		else
			sys = read_hex (sun_sys_types);
#endif

		if (!sys) {
			delete_partition(i);
			break;
		}

		if (!sun_label) {
			if (IS_EXTENDED (sys) != IS_EXTENDED (p->sys_ind)) {
				printf("You cannot change a partition into"
				       " an extended one or vice versa\n"
				       "Delete it first.\n");
				break;
			}
		}

                if (sys < 256) {
#if defined(sparc)
			if (sun_label && i == 2 && sys != WHOLE_DISK)
				printf("Consider leaving partition 3 "
				       "as Whole disk (5),\n"
				       "as SunOS/Solaris expects it and "
				       "even Linux likes it.\n");
#endif
                        if (sys == origsys)
                            break;
#if defined(sparc)
			if (sunlabel) {
				sun_change_sysid(i, sys);
			} else
#endif
				part_table[i]->sys_ind = sys;
                        printf ("Changed system type of partition %d "
                                "to %x (%s)\n", i + 1, sys,
                                (temp = partition_type(sys)) ? temp :
                                "Unknown");
                        changed[i] = 1;
                        break;
                }
        }
}

/* check_consistency() and long2chs() added Sat Mar 6 12:28:16 1993,
 * faith@cs.unc.edu, based on code fragments from pfdisk by Gordon W. Ross,
 * Jan.  1990 (version 1.2.1 by Gordon W. Ross Aug. 1990; Modified by S.
 * Lubkin Oct.  1991). */

static void long2chs(ulong ls, uint *c, uint *h, uint *s)
{
	int	spc = heads * sectors;

	*c = ls / spc;
	ls = ls % spc;
	*h = ls / sectors;
	*s = ls % sectors + 1;	/* sectors count from 1 */
}

static void check_consistency(struct partition *p, int partition)
{
	uint	pbc, pbh, pbs;		/* physical beginning c, h, s */
	uint	pec, peh, pes;		/* physical ending c, h, s */
	uint	lbc, lbh, lbs;		/* logical beginning c, h, s */
	uint	lec, leh, les;		/* logical ending c, h, s */

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
		printf("Partition %d has different physical/logical "
			"beginnings (non-Linux?):\n", partition + 1);
		printf("     phys=(%d, %d, %d) ", pbc, pbh, pbs);
		printf("logical=(%d, %d, %d)\n",lbc, lbh, lbs);
	}

/* Same physical / logical ending? */
	if (cylinders <= 1024 && (pec != lec || peh != leh || pes != les)) {
		printf("Partition %d has different physical/logical "
			"endings:\n", partition + 1);
		printf("     phys=(%d, %d, %d) ", pec, peh, pes);
		printf("logical=(%d, %d, %d)\n",lec, leh, les);
	}

#if 0
/* Beginning on cylinder boundary? */
	if (pbh != !pbc || pbs != 1) {
		printf("Partition %i does not start on cylinder "
			"boundary:\n", partition + 1);
		printf("     phys=(%d, %d, %d) ", pbc, pbh, pbs);
		printf("should be (%d, %d, 1)\n", pbc, !pbc);
	}
#endif

/* Ending on cylinder boundary? */
	if (peh != (heads - 1) || pes != sectors) {
		printf("Partition %i does not end on cylinder boundary:\n",
			partition + 1);
		printf("     phys=(%d, %d, %d) ", pec, peh, pes);
		printf("should be (%d, %d, %d)\n",
		pec, heads - 1, sectors);
	}
}

void list_table(int xtra)
{
	struct partition *p;
	char *type;
	int i, w;

#if defined(sparc)
	if (sun_label)
		sun_list_table(xtra);
	return;
#endif

	w = strlen(disk_device);

	printf("\nDisk %s: %d heads, %d sectors, %d cylinders\nUnits = "
		"%ss of %d * 512 bytes\n\n", disk_device, heads, sectors,
		cylinders, str_units(), display_factor);
	if (w < 5)
		w = 5;
	printf("%*s Boot %s  Start      End   Blocks   Id  System\n",
	       w + 1, "Device", show_begin ? "  Begin  " : " ");

	for (i = 0 ; i < partitions; i++) {
		if ((p = part_table[i])->sys_ind) {
			unsigned int psects = get_nr_sects(p);
                        printf(
			  show_begin
			    ? "%*s%-2d  %c%9d%9d%9d%9d%c  %2x  %s\n"
			    : "%*s%-2d  %c%c%9d%9d%9d%c  %2x  %s\n",
/* device */		w, disk_device, i+1,
/* boot flag */		!p->boot_ind ? ' ' : p->boot_ind == ACTIVE_FLAG
			? '*' : '?',
			  show_begin ?
/* begin */		cround(rounded( calculate(p->head, p->sector, p->cyl),
				get_start_sect(p) + offsets[i]))
			  : ' ',
/* start */		cround(get_start_sect(p) + offsets[i]),
/* end */		cround(get_start_sect(p) + offsets[i] + psects
				- (psects ? 1 : 0)),
/* odd flag on end */	psects / 2, (psects & 1) ? '+' : ' ',
/* type id */		p->sys_ind,
/* type name */		(type = partition_type(p->sys_ind)) ?
			type : "Unknown");
			check_consistency(p, i);
		}
	}
}

void x_list_table(int extend)
{
	struct partition *p, **q;
	int i;

	if (extend)
		q = ext_pointers;
	else
		q = part_table;
	printf("\nDisk %s: %d heads, %d sectors, %d cylinders\n\n",
		disk_device, heads, sectors, cylinders);
        printf("Nr AF  Hd Sec  Cyl  Hd Sec  Cyl   Start    Size ID\n");
	for (i = 0 ; i < partitions; i++)
		if ((p = q[i]) != NULL) {
                        printf("%2d %02x%4d%4d%5d%4d%4d%5d%8d%8d %02x\n",
				i + 1, p->boot_ind, p->head,
				sector(p->sector),
				cylinder(p->sector, p->cyl), p->end_head,
				sector(p->end_sector),
				cylinder(p->end_sector, p->end_cyl),
				get_start_sect(p), get_nr_sects(p), p->sys_ind);
			if (p->sys_ind)
				check_consistency(p, i);
		}
}

void check_bounds(uint *first, uint *last)
{
	int i;
	uint max = 0xffffffff;          /* used to be 256 * 63 * 1024
					   but that made it impossible to add a
					   partition crossing cylinder 8064 */
	struct partition *p = part_table[0];

	for (i = 0; i < partitions; p = part_table[++i]) {
		if (!p->sys_ind || IS_EXTENDED (p->sys_ind)) {
			first[i] = max;
			last[i] = 0;
		} else {
			first[i] = rounded(calculate(p->head, p->sector,
				p->cyl), get_start_sect(p) + offsets[i]);
			last[i] = get_start_sect(p) + offsets[i] +
				get_nr_sects(p) - 1;
		}
	}
}

void check(int n, uint h, uint s, uint c, uint start)
{
	uint total, real_s, real_c, i;

	real_s = sector(s) - 1;
	real_c = cylinder(s, c);
	total = (real_c * sectors + real_s) * heads + h;
	if (full_bits)
		while ((i = total + full_bits) <= start) {
			real_c += 1024;
			total = i;
		}
	if (!total)
		fprintf(stderr, "Warning: partition %d contains sector 0\n", n);
	if (h >= heads)
		fprintf(stderr,
			"Partition %d: head %d greater than maximum %d\n",
			n, h + 1, heads);
	if (real_s >= sectors)
		fprintf(stderr, "Partition %d: sector %d greater than "
			"maximum %d\n", n, s, sectors);
	if (real_c >= cylinders)
		fprintf(stderr, "Partitions %d: cylinder %d greater than "
			"maximum %d\n", n, real_c + 1, cylinders);
	if (cylinders <= 1024 && start != total)
		fprintf(stderr,
			"Partition %d: previous sectors %d disagrees with "
			"total %d\n", n, start, total);
}


void verify(void)
{
	int i, j;
	uint total = 1,
		first[partitions], last[partitions];
	struct partition *p = part_table[0];

	if (warn_geometry())
		return;

#if defined(sparc)
	if (sun_label)
		verify_sun();
	return;
#endif

	check_bounds(first, last);
	for (i = 0; i < partitions; p = part_table[++i])
		if (p->sys_ind && !IS_EXTENDED (p->sys_ind)) {
			check_consistency(p, i);
			if (get_start_sect(p) + offsets[i] < first[i])
				printf("Warning: bad start-of-data in "
					"partition %d\n", i + 1);
			check(i + 1, p->end_head, p->end_sector, p->end_cyl,
				last[i]);
			total += last[i] + 1 - first[i];
			for (j = 0; j < i; j++)
			if ((first[i] >= first[j] && first[i] <= last[j])
			 || ((last[i] <= last[j] && last[i] >= first[j]))) {
				printf("Warning: partition %d overlaps "
					"partition %d.\n", j + 1, i + 1);
				total += first[i] >= first[j] ?
					first[i] : first[j];
				total -= last[i] <= last[j] ?
					last[i] : last[j];
			}
		}

	if (extended_offset) {
		uint e_last = get_start_sect(part_table[ext_index]) +
			get_nr_sects(part_table[ext_index]) - 1;

		for (p = part_table[i = 4]; i < partitions;
				p = part_table[++i]) {
			total++;
			if (!p->sys_ind) {
				if (i != 4 || i + 1 < partitions)
					printf("Warning: partition %d "
						"is empty\n", i + 1);
			}
			else if (first[i] < extended_offset ||
					last[i] > e_last)
				printf("Logical partition %d not entirely in "
					"partition %d\n", i + 1, ext_index + 1);
		}
	}

	if (total > heads * sectors * cylinders)
		printf("Total allocated sectors %d greater than the maximum "
			"%d\n", total, heads * sectors * cylinders);
	else if ((total = heads * sectors * cylinders - total) != 0)
		printf("%d unallocated sectors\n", total);
}

void add_partition(int n, int sys)
{
	char mesg[48];
	int i, read = 0;
	struct partition *p = part_table[n], *q = part_table[ext_index];
	uint start, stop = 0, limit, temp,
		first[partitions], last[partitions];

	if (p->sys_ind) {
		printf("Partition %d is already defined.  Delete "
			"it before re-adding it.\n", n + 1);
		return;
	}
	check_bounds(first, last);
	if (n < 4) {
		start = sector_offset;
		limit = heads * sectors * cylinders - 1;
		if (extended_offset) {
			first[ext_index] = extended_offset;
			last[ext_index] = get_start_sect(q) +
				get_nr_sects(q) - 1;
		}
	} else {
		start = extended_offset + sector_offset;
		limit = get_start_sect(q) + get_nr_sects(q) - 1;
	}
	if (unit_flag)
		for (i = 0; i < partitions; i++)
			first[i] = (cround(first[i]) - 1) * display_factor;

	sprintf(mesg, "First %s", str_units());
	do {
		temp = start;
		for (i = 0; i < partitions; i++) {
			int lastplusoff;

			if (start == offsets[i])
				start += sector_offset;
			lastplusoff = last[i] + ((n<4) ? 0 : sector_offset);
			if (start >= first[i] && start <= lastplusoff)
				start = lastplusoff + 1;
		}
		if (start > limit)
			break;
		if (start >= temp+display_factor && read) {
			printf("Sector %d is already allocated\n", temp);
			temp = start;
			read = 0;
		}
		if (!read && start == temp) {
			uint i;
			i = start;
			start = read_int(cround(i), cround(i), cround(limit),
					 ignore, mesg);
			if (unit_flag) {
				start = (start - 1) * display_factor;
				if (start < i) start = i;
			}
			read = 1;
		}
	} while (start != temp || !read);
	if (n > 4)			/* NOT for fifth partition */
		offsets[n] = start - sector_offset;

	for (i = 0; i < partitions; i++) {
		if (start < offsets[i] && limit >= offsets[i])
			limit = offsets[i] - 1;
		if (start < first[i] && limit >= first[i])
			limit = first[i] - 1;
	}
	if (start > limit) {
		printf("No free sectors available\n");
		if (n > 4) {
			free(buffers[n]);
			partitions--;
		}
		return;
	}
	if (cround(start) == cround(limit))
		stop = start;
	else {
		sprintf(mesg, "Last %s or +size or +sizeM or +sizeK",
			str_units());
		stop = read_int(cround(start), cround(limit), cround(limit),
				lower, mesg);
		if (unit_flag) {
			stop = stop * display_factor - 1;
			if (stop >limit)
				stop = limit;
		}
	}

	set_partition(n, p, start, stop, sys, offsets[n]);

	if (IS_EXTENDED (sys)) {
		ext_index = n;
		offsets[4] = extended_offset = start;
		ext_pointers[n] = p;
		if (!(buffers[4] = calloc(1, SECTOR_SIZE)))
			fatal(out_of_memory);
		part_table[4] = offset(buffers[4], 0);
		ext_pointers[4] = part_table[4] + 1;
		changed[4] = 1;
		partitions = 5;
	}
	else {
		if (n > 4)
			set_partition(n - 1, ext_pointers[n - 1],
				start - sector_offset, stop, EXTENDED,
				extended_offset);
#if 0
		if ((limit = get_nr_sects(p)) & 1)
			printf("Warning: partition %d has an odd "
				"number of sectors.\n", n + 1);
#endif
	}
}

void add_logical(void)
{
	if (partitions > 5 || part_table[4]->sys_ind) {
		if (!(buffers[partitions] = calloc(1, SECTOR_SIZE)))
			fatal(out_of_memory);
		part_table[partitions] = offset(buffers[partitions], 0);
		ext_pointers[partitions] = part_table[partitions] + 1;
		offsets[partitions] = 0;
		partitions++;
	}
	add_partition(partitions - 1, LINUX_NATIVE);
}

void new_partition(void)
{
	int i, free_primary = 0;

	if (warn_geometry())
		return;

#if defined(sparc)
	if (sun_label)
		add_sun_partition(get_partition(0, partitions), LINUX_NATIVE);
	return;
#endif

	if (partitions >= MAXIMUM_PARTS) {
		printf("The maximum number of partitions has been created\n");
		return;
	}

	for (i = 0; i < 4; i++)
		free_primary += !part_table[i]->sys_ind;
	if (!free_primary)
		if (extended_offset)
			add_logical();
		else
			printf("You must delete some partition and add "
				"an extended partition first\n");
	else {
		char c, line[LINE_LENGTH];
		sprintf(line, "Command action\n   %s\n   p   primary "
			"partition (1-4)\n", extended_offset ?
			"l   logical (5 or over)" : "e   extended");
		while (1)
			if ((c = tolower(read_char(line))) == 'p') {
				add_partition(get_partition(0, 4),
					LINUX_NATIVE);
				return;
			}
			else if (c == 'l' && extended_offset) {
				add_logical();
				return;
			}
			else if (c == 'e' && !extended_offset) {
				add_partition(get_partition(0, 4),
					EXTENDED);
				return;
			}
			else
				printf("Invalid partition number "
				       "for type `%c'\n", c);
		
	}
}

void write_table(void)
{
	int i, error = 0;

	changed[3] = changed[0] || changed[1] || changed[2] || changed[3];
	if (!sun_label) {
	    for (i = 3; i < partitions; i++) {
		if (changed[i]) {
			write_part_table_flag(buffers[i]);
			if (ext2_llseek(fd, (ext2_loff_t)offsets[i]
				       * SECTOR_SIZE, SEEK_SET) < 0)
				fatal(unable_to_seek);
			if (write(fd, buffers[i], SECTOR_SIZE) != SECTOR_SIZE)
				fatal(unable_to_write);
		}
	    }
	} else {
#if defined(sparc)
	    if (changed[3] || changed[4] || changed[5] ||
		changed[6] || changed[7]) {
		sun_write_table();
	    }
#endif
	}

	printf("The partition table has been altered!\n\n");

	printf("Calling ioctl() to re-read partition table.\n");
	sync();
	sleep(2);
	if ((i = ioctl(fd, BLKRRPART)) != 0) {
                error = errno;
        } else {
                /* some kernel versions (1.2.x) seem to have trouble
                   rereading the partition table, but if asked to do it
		   twice, the second time works. - biro@yggdrasil.com */
                sync();
                sleep(2);
                if((i = ioctl(fd, BLKRRPART)) != 0)
                        error = errno;
        }

	close(fd);

	printf("Syncing disks.\n");
	sync();
	sleep(4);		/* for sync() */

	if (i < 0)
		printf("Re-read table failed with error %d: %s.\nReboot your "
			"system to ensure the partition table is updated.\n",
			error, strerror(error));

	if (!sun_label)
	    printf(
		"\nWARNING: If you have created or modified any DOS 6.x\n"
		"partitions, please see the fdisk manual page for additional\n"
		"information.\n");

	exit(0);
}

#define MAX_PER_LINE	16
void print_buffer(char buffer[])
{
	int	i,
		l;

	for (i = 0, l = 0; i < SECTOR_SIZE; i++, l++) {
		if (l == 0)
			printf("0x%03X:", i);
		printf(" %02X", (unsigned char) buffer[i]);
		if (l == MAX_PER_LINE - 1) {
			printf("\n");
			l = -1;
		}
	}
	if (l > 0)
		printf("\n");
	printf("\n");
}

void print_raw(void)
{
	int i;

	printf("Device: %s\n", disk_device);
	if (sun_label)
		print_buffer(buffer);
	else for (i = 3; i < partitions; i++)
		print_buffer(buffers[i]);
}

void move_begin(int i)
{
	struct partition *p = part_table[i];
	uint new, first;

	if (warn_geometry())
		return;
	if (!p->sys_ind || !get_nr_sects(p) || IS_EXTENDED (p->sys_ind)) {
		printf("Partition %d has no data area\n", i + 1);
		return;
	}
	first = rounded(calculate(p->head, p->sector, p->cyl),
			get_start_sect(p) + offsets[i]);
	new = read_int(first, first, 
		       get_start_sect(p) + get_nr_sects(p) + offsets[i] - 1,
		       lower, "New beginning of data") - offsets[i];

	if (new != get_nr_sects(p)) {
		first = get_nr_sects(p) + get_start_sect(p) - new;
		set_nr_sects(p, first);
		set_start_sect(p, new);
		changed[i] = 1;
	}
}

void xselect(void)
{
	while(1) {
		putchar('\n');
		switch (tolower(read_char("Expert command (m for help): "))) {
#if defined(sparc)
		case 'a':
			if (sun_label)
				sun_set_alt_cyl();
			break;
#endif
		case 'b':
			if (!sun_label)
				move_begin(get_partition(0, partitions));
			break;
		case 'c':
			cylinders = read_int(1, cylinders, 65535,
					     deflt, "Number of cylinders");
#if defined(sparc)
			if (sun_label)
				sun_set_ncyl(cylinders);
#endif
			warn_cylinders();
			break;
		case 'd':
			print_raw();
			break;
		case 'e':
			if (!sun_label)
				x_list_table(1);
#if defined(sparc)
			else
				sun_set_xcyl();
#endif
			break;
		case 'h':
			heads = read_int(1, heads, 256, deflt,
					 "Number of heads");
			update_units();
			break;
#if defined(sparc)
		case 'i':
			if (sun_label)
				sun_set_ilfact();
			break;
		case 'o':
			if (sun_label)
				sun_set_rspeed();
			break;
#endif
		case 'p':
#if defined(sparc)
			if (sun_label)
				list_table(1);
			else
#endif
				x_list_table(0);
			break;
		case 'q':
			close(fd);
			exit(0);
		case 'r':
			return;
		case 's':
			sectors = read_int(1, sectors, 63, deflt,
					   "Number of sectors");
			if (dos_compatible_flag) {
				sector_offset = sectors;
				fprintf(stderr, "Warning: setting "
					"sector offset for DOS "
					"compatiblity\n");
			}
			update_units();
			break;
		case 'v':
			verify();
			break;
		case 'w':
			write_table(); 	/* does not return */
			break;
#if defined(sparc)
		case 'y':
			if (sun_label)
				sun_set_pcylcount();
			break;
#endif
		default:
			xmenu();
		}
	}
}

int
is_ide_cdrom(char *device) {
	/* No device was given explicitly, and we are trying some
       likely things.  But opening /dev/hdc may produce errors like
           "hdc: tray open or drive not ready"
       if it happens to be a CD-ROM drive. It even happens that
       the process hangs on the attempt to read a music CD.
       So try to be careful. This only works since 2.1.73. */

    FILE *procf;
    char buf[100];
    struct stat statbuf;

    sprintf(buf, "/proc/ide/%s/media", device+5);
    procf = fopen(buf, "r");
    if (procf != NULL && fgets(buf, sizeof(buf), procf))
        return  !strncmp(buf, "cdrom", 5);

    /* Now when this proc file does not exist, skip the
       device when it is read-only. */
    if (stat(device, &statbuf) == 0)
        return (statbuf.st_mode & 0222) == 0;

    return 0;
}

void try(char *device, int user_specified)
{
	disk_device = device;
	if (!setjmp(listingbuf)) {
		if (!user_specified)
			if (is_ide_cdrom(device))
				return;
		if ((fd = open(disk_device, type_open)) >= 0) {
			if (get_boot(try_only) < 0) {
				if (btrydev(device) < 0)
					fprintf(stderr,
						"Disk doesn't contain a valid "
						"partition table\n");
				close(fd);
			} else {
				close(fd);
				list_table(0);
				if (!sun_label && partitions > 4)
					delete_partition(ext_index);
			}
		} else {
				/* Ignore other errors, since we try IDE
				   and SCSI hard disks which may not be
				   installed on the system. */
			if(errno == EACCES) {
			    fprintf(stderr, "Cannot open %s\n", device);
			    return;
			}
		}
	}
}

void
dummy(int *kk) {}

void main(int argc, char **argv)
{
	int i, j, s, c;
	int optl = 0, opts = 0;
	char *part;


	/*
	 * Calls:
	 *  fdisk -v
	 *  fdisk -l [-b] [-u] [device] ...
	 *  fdisk -s [partition] ...
	 *  fdisk [-b] [-u] [device]
	 */
	while ((c = getopt(argc, argv, "blsuv")) != EOF) {
		switch (c) {
		case 'b':
			show_begin = 1;
			break;
		case 'l':
			optl = 1;
			break;
		case 's':
			opts = 1;
			break;
		case 'u':
			unit_flag = 0;
			break;
		case 'v':
			printf("fdisk v" VERSION "\n");
			exit(0);
		default:
			fatal(usage);
		}
	}

	if (optl) {
		listing = 1;
		nowarn = 1;
		type_open = O_RDONLY;
		if (argc > optind) {
			int k;
			/* avoid gcc warning:
			   variable `k' might be clobbered by `longjmp' */
			dummy(&k);
			for(k=optind; k<argc; k++)
				try(argv[k], 1);
		} else {
			try("/dev/hda", 0);
			try("/dev/hdb", 0);
			try("/dev/hdc", 0); /* often a CDROM */
			try("/dev/hdd", 0);
			try("/dev/sda", 0);
			try("/dev/sdb", 0);
			try("/dev/sdc", 0);
			try("/dev/sdd", 0);
			try("/dev/sde", 0);
			try("/dev/sdf", 0);
			try("/dev/sdg", 0);
			try("/dev/sdh", 0);
			try("/dev/eda", 0);
			try("/dev/edb", 0);
			try("/dev/edc", 0);
			try("/dev/edd", 0);
		}
		exit(0);
	}

	if (opts) {
		/* Silly assumptions here about device naming */
		nowarn = 1;
		disk_device = (char *) malloc(9);
		type_open = O_RDONLY;

		opts = argc - optind;
		if (opts <= 0)
			fatal(usage);

		for (j = optind; j < argc; j++) {
			part = argv[j];
			if (strlen(part) < 9)
				fatal(usage);
			if (!(i = atoi(part + 8)))
				fatal(usage);
			i--; 	/* count from 0 */
			strncpy(disk_device, part, 8);
			disk_device[8] = 0;
			if ((fd = open(disk_device, type_open)) < 0)
				fatal(unable_to_open);
			close(fd);
			if (get_boot(require) < 0)
				exit(1);
			if (i >= partitions)
				exit(1);
#if defined(sparc)
			if (!sun_label) {
				int id = sunlabel->infos[i].id;

				if (!(id > 1 && id != WHOLE_DISK))
					exit(1);
				s = get_num_sectors(sunlabel->partitions[i]);
			} else
#endif
				s = get_nr_sects(part_table[i]);
			if (opts == 1)
				printf("%d\n", s/2);
			else
				printf("%s: %d\n", argv[j], s/2);
		}
		exit(0);
	}

	if (argc-optind == 1)
		disk_device = argv[optind];
	else if (argc-optind != 0)
		fatal(usage);
	else {
		if ((fd = open(DEFAULT_DEVICE, O_RDWR)) < 0)
			disk_device = ALTERNATE_DEVICE;
		else close(fd);
		printf("Using %s as default device!\n", disk_device);
	}
	get_boot(fdisk);

	while (1) {
		putchar('\n');
		switch (tolower(read_char("Command (m for help): "))) {
		case 'a':
#if defined(sparc)
			if (sun_label)
				toggle_sunflags(get_partition(1, partitions),
						0x01);
			else
#endif
				toggle_active(get_partition(1, partitions));
			break;
		case 'b':
			bselect();
			break;
		case 'c':
#if defined(sparc)
			if (sun_label)
				toggle_sunflags(get_partition(1, partitions),
						0x10);
			else
#endif
				toggle_dos();
			break;
		case 'd':
			delete_partition(
				get_partition(1, partitions));
			break;
		case 'l':
#if defined(sparc)
			if (sun_label)
				list_types(sun_sys_types);
			else
#endif
				list_types(sys_types);
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
			close(fd);
			exit(0);
#if defined(sparc)
		case 's':
			create_sunlabel();
			break;
#endif
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
			xselect();
			break;
		default: menu();
		}
	}
}

