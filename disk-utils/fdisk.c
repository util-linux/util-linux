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
 *
 */


#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <setjmp.h>
#include <errno.h>

#include <sys/ioctl.h>

#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/fs.h>

#include "fdisk.h"

#define hex_val(c)	({ \
				char _c = (c); \
				isdigit(_c) ? _c - '0' : \
				tolower(_c) + 10 - 'a'; \
			})


#define VERSION	"2.1 (>4GB)"

#define DEFAULT_DEVICE	"/dev/hda"
#define ALTERNATE_DEVICE "/dev/sda"
#define LINE_LENGTH	80
#define MAXIMUM_PARTS	60
#define PART_TABLE_FLAG	0xaa55
#define table_check(b)	((unsigned short *)((b) + 0x1fe))
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

#define ACTIVE_FLAG	0x80
#define EXTENDED	5

#define LINUX_PARTITION	0x81
#define LINUX_SWAP	0x82
#define LINUX_NATIVE	0x83

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

struct	partition *part_table[MAXIMUM_PARTS]	/* partitions */
		= {offset(buffer, 0), offset(buffer, 1),
		offset(buffer, 2), offset(buffer, 3)},
	*ext_pointers[MAXIMUM_PARTS]		/* link pointers */
		= {NULL, NULL, NULL, NULL};

struct systypes sys_types[] = { /* struct systypes in fdisk.h *//* bf */
                {0, "Empty"},
		{1, "DOS 12-bit FAT"},
		{2, "XENIX root"},
		{3, "XENIX usr"},
		{4, "DOS 16-bit <32M"},
		{EXTENDED, "Extended"},
		{6, "DOS 16-bit >=32M"},
		{7, "OS/2 HPFS"},		/* or QNX? */
		{8, "AIX"},
		{9, "AIX bootable"},
		{10, "OS/2 Boot Manager"},
		{0x40, "Venix 80286"},
		{0x51, "Novell?"},
		{0x52, "Microport"},		/* or CPM? */
		{0x63, "GNU HURD"},		/* or System V/386? */
		{0x64, "Novell Netware 286"},
		{0x65, "Novell Netware 386"},
		{0x75, "PC/IX"},
		{0x80, "Old MINIX"},		/* Minix 1.4a and earlier */

		{LINUX_PARTITION, "Linux/MINIX"}, /* Minix 1.4b and later */
		{LINUX_SWAP, "Linux swap"},
		{LINUX_NATIVE, "Linux native"},

		{0x93, "Amoeba"},
		{0x94, "Amoeba BBT"},		/* (bad block table) */
		{0xa5, "BSD/386"},
		{0xb7, "BSDI fs"},
		{0xb8, "BSDI swap"},
		{0xc7, "Syrinx"},
		{0xdb, "CP/M"},			/* or Concurrent DOS? */
		{0xe1, "DOS access"},
		{0xe3, "DOS R/O"},
		{0xf2, "DOS secondary"},
		{0xff, "BBT"}			/* (bad track table) */
	};

int nsys_types = sizeof (sys_types) / sizeof (struct systypes); /* bf */

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
			"Usage: fdisk [-l] [-v] [-s /dev/hdxn] [/dev/hdx]\n";
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
	puts("Command action\n"
		"   a   toggle a bootable flag\n"
                "   b   edit bsd disklabel\n" /* bf */
		"   c   toggle the dos compatibility flag\n"
		"   d   delete a partition\n"
		"   l   list known partition types\n"
		"   m   print this menu\n"
		"   n   add a new partition\n"
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
	puts("Command action\n"
		"   b   move beginning of data in a partition\n"
		"   c   change number of cylinders\n"
		"   d   print the raw data in the partition table\n"
		"   e   list extended partitions\n"
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
	int high = sizeof(sys_types) / sizeof(struct systypes),
		low = 0, mid;
	uint tmp;

	while (high >= low) {
		mid = (high + low) >> 1;
		if ((tmp = sys_types[mid].index) == type)
			return sys_types[mid].name;
		else if (tmp < type)
			low = mid + 1;
		else high = mid - 1;
	}
	return NULL;
}

/* added parameters to list_types() so fdisklabel
   can pass another list of types.
*//* bf */
void list_types(struct systypes *sys, int size)
{
	uint last[4], done = 0, next = 0;
	int i;

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
	p->start_sect = 0;
	p->nr_sects = 0;
}

void set_partition(int i, struct partition *p, uint start, uint stop,
	int sys, uint offset)
{
	p->boot_ind = 0;
	p->sys_ind = sys;
	p->start_sect = start - offset;
	p->nr_sects = stop - start + 1;
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
	update_units();
	if (cylinders > 1024)
		fprintf(stderr, "The number of cylinders for this disk is "
			"set to %d.\nThis is larger than 1024, and may cause "
			"problems with:\n"
			"1) software that runs at boot time (e.g., LILO)\n"
			"2) booting and partitioning software form other OSs\n"
			"   (e.g., DOS FDISK, OS/2 FDISK)\n",
			cylinders);
}

void read_extended(struct partition *p)
{
	int i;
	struct partition *q;

	ext_pointers[ext_index] = part_table[ext_index];
	if (!p->start_sect)
		fprintf(stderr, "Bad offset in primary extended partition\n");
	else while (p->sys_ind == EXTENDED) {
		if (partitions >= MAXIMUM_PARTS) {
			fprintf(stderr,
				"Warning: deleting partitions after %d\n",
				partitions);
			clear_partition(ext_pointers[partitions - 1]);
			changed[partitions - 1] = 1;
			return;
		}
		offsets[partitions] = extended_offset + p->start_sect;
		if (!extended_offset)
			extended_offset = p->start_sect;
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
			if (p->sys_ind == EXTENDED)
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

int get_boot(void)
{
	int i;
	struct hd_geometry geometry;

	partitions = 4;
	if ((fd = open(disk_device, type_open)) < 0)
	{
	    if ((fd = open(disk_device, O_RDONLY)) < 0)
		fatal(unable_to_open);
	    else
		printf("You will not be able to write the partition table.\n");
	}
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
		warn_cylinders();
	}
	else update_units();
	warn_geometry();

	if (*(unsigned short *)  (0x1fe + buffer) != 0xAA55) {
		return 0;	/* not a valid partition table */
	}

	for (i = 0; i < 4; i++)
		if(part_table[i]->sys_ind == EXTENDED)
			if (partitions != 4)
				fprintf(stderr, "Ignoring extra extended "
					"partition %d\n", i + 1);
			else read_extended(part_table[ext_index = i]);

	for (i = 3; i < partitions; i++)
		if (*table_check(buffers[i]) != PART_TABLE_FLAG) {
			fprintf(stderr, "Warning: invalid flag %04x of parti"
				"tion table %d will be corrected by w(rite)\n",
				*table_check(buffers[i]), i + 1);
			changed[i] = 1;
		}

	return 1;
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
	do
		fputs(mesg, stdout);
	while (!read_line());
	return *line_ptr;
}

/* new function *//* bf */
int read_hex(struct systypes *sys, int size)
{
        int hex;

        while (1)
        {
           read_char("Hex code (type L to list codes): ");
           if (tolower(*line_ptr) == 'l')
               list_types(sys, size);
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
		while (!isdigit(read_char(ms))
				&& (*line_ptr != '-' && *line_ptr != '+'))
			continue;
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

	if (warn && !part_table[i]->sys_ind)
		fprintf(stderr, "Warning: partition %d has empty type\n",
			i + 1);
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

	if (p->sys_ind == EXTENDED && !p->boot_ind)
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
	if (i < 4) {
		if (p->sys_ind == EXTENDED && i == ext_index) {
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
			p->start_sect = q->start_sect;
			p->nr_sects = q->nr_sects;
			changed[i - 1] = 1;
		}
		else {
                   if(part_table[5]) /* prevent SEGFAULT */
		        part_table[5]->start_sect +=
				offsets[5] - extended_offset;
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
	int i = get_partition(0, partitions), sys;
	struct partition *p = part_table[i];

	if ((sys = p->sys_ind) == EXTENDED)
		printf("Partition %d is extended.  Delete it\n", i + 1);
	else if (!sys)
		printf("Partition %d does not exist yet!\n", i + 1);
	else while (1) {
#if 0
		read_char("Hex code (type L to list codes): ");
		if (tolower(*line_ptr) == 'l')
			list_types();
		else if (isxdigit(*line_ptr)) {
			sys = 0;
			do
				sys = sys << 4 | hex_val(*line_ptr++);
			while (isxdigit(*line_ptr));
#endif
			/* The above code has been moved to read_hex ()
			   to avoid having the same code in fdisklabel.
			 *//* bf */
                        sys = read_hex (sys_types, nsys_types); /* bf */

			if (!sys) {
				delete_partition(i);
				break;
			}
			else if (sys == EXTENDED) {
				printf("You may not change a partition "
					"to be an extended partition\n");
				break;
			}
			else if (sys < 256) {
				if (sys == p->sys_ind)
					break;
				part_table[i]->sys_ind = sys;
				printf ("Changed system type of partition %d "
					"to %x (%s)\n", i + 1, sys,
					(temp = partition_type(sys)) ? temp :
					"Unknown");
				changed[i] = 1;
				break;
			}
#if 0 /* see above *//* bf */
		}
#endif
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
	long2chs(p->start_sect, &lbc, &lbh, &lbs);

/* compute logical ending (c, h, s) */
	long2chs(p->start_sect + p->nr_sects - 1, &lec, &leh, &les);

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

void list_table(void)
{
	struct partition *p;
	char *type;
	int i, w = strlen(disk_device);

	printf("\nDisk %s: %d heads, %d sectors, %d cylinders\nUnits = "
		"%ss of %d * 512 bytes\n\n", disk_device, heads, sectors,
		cylinders, str_units(), display_factor);
	if (w < 5)
		w = 5;
        printf("%*s Boot   Begin    Start      End   Blocks   Id  System\n",
		w + 1, "Device");
	for (i = 0 ; i < partitions; i++)
		if ((p = part_table[i])->sys_ind) {
                        printf("%*s%-2d  %c%9d%9d%9d%9d%c  %2x  %s\n", w,
/* device */		disk_device, i + 1,
/* boot flag */		!p->boot_ind ? ' ' : p->boot_ind == ACTIVE_FLAG
			? '*' : '?',
/* begin */		cround(rounded( calculate(p->head, p->sector, p->cyl),
				p->start_sect + offsets[i])),
/* start */		cround(p->start_sect + offsets[i]),
/* end */		cround(p->start_sect + offsets[i] + p->nr_sects
				- (p->nr_sects ? 1: 0)),
/* odd flag on end */	p->nr_sects / 2, p->nr_sects & 1 ? '+' : ' ',
/* type id */		p->sys_ind,
/* type name */		(type = partition_type(p->sys_ind)) ?
			type : "Unknown");
			check_consistency(p, i);
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
				p->start_sect, p->nr_sects, p->sys_ind);
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

	for (i = 0; i < partitions; p = part_table[++i])
		if (!p->sys_ind || p->sys_ind == EXTENDED) {
			first[i] = max;
			last[i] = 0;
		}
		else {
			first[i] = rounded(calculate(p->head, p->sector,
				p->cyl), p->start_sect + offsets[i]);
			last[i] = p->start_sect + offsets[i] + p->nr_sects - 1;
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

	check_bounds(first, last);
	for (i = 0; i < partitions; p = part_table[++i])
		if (p->sys_ind && (p->sys_ind != EXTENDED)) {
			check_consistency(p, i);
			if (p->start_sect + offsets[i] < first[i])
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
		uint e_last = part_table[ext_index]->start_sect +
			part_table[ext_index]->nr_sects - 1;

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
			last[ext_index] = q->start_sect + q->nr_sects - 1;
		}
	}
	else {
		start = extended_offset + sector_offset;
		limit = q->start_sect + q->nr_sects - 1;
	}
	if (unit_flag)
		for (i = 0; i < partitions; i++)
			first[i] = (cround(first[i]) - 1) * display_factor;

	sprintf(mesg, "First %s", str_units());
	do {
		temp = start;
		for (i = 0; i < partitions; i++) {
			if (start == offsets[i])
				start += sector_offset;
			if (start >= first[i] && start <= last[i])
				if (n < 4)
					start = last[i] + 1;
				else
					start = last[i] + sector_offset;
		}
		if (start > limit)
			break;
		if (start != temp && read) {
			printf("Sector %d is already allocated\n", temp);
			temp = start = stop;
			read = 0;
		}
		if (!read && start == temp) {
			uint i;
			i = (stop = start) + (n > 4);
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

	if (sys == EXTENDED) {
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
		if ((limit = p->nr_sects) & 1)
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
	for (i = 3; i < partitions; i++)
		if (changed[i]) {
			*table_check(buffers[i]) = PART_TABLE_FLAG;
			if (ext2_llseek(fd, (ext2_loff_t)offsets[i]
				       * SECTOR_SIZE, SEEK_SET) < 0)
				fatal(unable_to_seek);
			if (write(fd, buffers[i], SECTOR_SIZE) != SECTOR_SIZE)
				fatal(unable_to_write);
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

	printf( "\nWARNING: If you have created or modified any DOS 6.x\n"
		"partitions, please see the fdisk manual page for additional\n"
		"information.\n" );

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
	for (i = 3; i < partitions; i++)
		print_buffer(buffers[i]);
}

void move_begin(int i)
{
	struct partition *p = part_table[i];
	uint new, first;

	if (warn_geometry())
		return;
	if (!p->sys_ind || !p->nr_sects || p->sys_ind == EXTENDED) {
		printf("Partition %d has no data area\n", i + 1);
		return;
	}
	first = rounded(calculate(p->head, p->sector, p->cyl), p->start_sect +
		offsets[i]);
	new = read_int(first, first, 
		       p->start_sect + p->nr_sects + offsets[i] - 1,
		       lower, "New beginning of data") - offsets[i];

	if (new != p->nr_sects) {
		first = p->nr_sects + p->start_sect - new;
		p->nr_sects = first;
		p->start_sect = new;
		changed[i] = 1;
	}
}

void xselect(void)
{
	while(1) {
		putchar('\n');
		switch (tolower(read_char("Expert command (m for help): "))) {
			case 'b': move_begin(get_partition(0, partitions));
				break;
			case 'c': cylinders = read_int(1, cylinders, 65535,
					deflt, "Number of cylinders");
 				warn_cylinders();
				break;
			case 'd': print_raw();
				break;
			case 'e': x_list_table(1);
				break;
			case 'h': heads = read_int(1, heads, 256, deflt,
						   "Number of heads");
				update_units();
				break;
			case 'p': x_list_table(0);
				break;
			case 'q': close(fd);
				exit(0);
			case 'r': return;
			case 's': sectors = read_int(1, sectors, 63, deflt,
					"Number of sectors");
				if (dos_compatible_flag) {
					sector_offset = sectors;
					fprintf(stderr, "Warning: setting "
						"sector offset for DOS "
						"compatiblity\n");
				}
				update_units();
				break;
			case 'v': verify();
				break;
			case 'w': write_table();
			default: xmenu();
		}
	}
}

void try(char *device)
{
	disk_device = device;
	if (!setjmp(listingbuf)) {
		if ((fd = open(disk_device, type_open)) >= 0) {
			if (get_boot()) {
			    close(fd);
			    list_table();
			    if (partitions > 4)
				delete_partition(ext_index);
			} else {
			    btrydev(device);
			    close(fd);
			}
		} else {
				/* Ignore other errors, since we try IDE
				   and SCSI hard disks which may not be
				   installed on the system. */
			if(errno == EACCES) {
			    fprintf(stderr, "Cannot open %s\n", device);
			    exit(1);
			}
		}
	}
}

void main(int argc, char **argv)
{
	if (argc > 3)
		fatal(usage);
	if (argc > 1 && *argv[1] == '-') {
		switch (*(argv[1] + 1)) {
			case 'v':
				printf("fdisk v" VERSION "\n");
				exit(0);
			case 'l':
				listing = 1;
				type_open = O_RDONLY;
				try("/dev/hda");
				try("/dev/hdb");
				try("/dev/hdc");
				try("/dev/hdd");
				try("/dev/sda");
				try("/dev/sdb");
				try("/dev/sdc");
				try("/dev/sdd");
				try("/dev/sde");
				try("/dev/sdf");
				try("/dev/sdg");
				try("/dev/sdh");
				exit(0);
			case 's': {
				int i;
				if (argc < 3)
					fatal(usage);
				if (!(i = atoi(argv[2] + 8)))
					fatal(usage);
				disk_device = (char *) malloc(9);
				strncpy(disk_device, argv[2], 8);
				if ((fd = open(disk_device, O_RDWR)) >= 0) {
					close(fd);
					get_boot();
				if (i > partitions) exit(1);
				if (part_table[--i]->sys_ind > 10)
					printf("%d\n",
						part_table[i]->nr_sects / 2);
				else exit(1);
				exit(0);
			}
	}
			default:
				fatal(usage);
		}
	}
	if (argc > 1)
		disk_device = argv[argc - 1];
	else if ((fd = open(DEFAULT_DEVICE, O_RDWR)) < 0)
		disk_device = ALTERNATE_DEVICE;
	else close(fd);

	if (argc == 1)
		printf("Using %s as default device!\n", disk_device);
	get_boot();

	while (1) {
		putchar('\n');
		switch (tolower(read_char("Command (m for help): "))) {
			case 'a': toggle_active(get_partition(1, partitions));
				break;
/* There should be a define which allows to turn off disklabel support so as
   not to make fdisk larger than necessary on installation boot disks.
*/
#if 1 /* #ifndef NO_DISKLABEL_SUPPORT */
                        case 'b': bselect(); /* bf */
                                break;
#endif
			case 'c':
				toggle_dos();
				break;
			case 'd': delete_partition(
					get_partition(1, partitions));
				break;
                        case 'l': list_types(sys_types, nsys_types); /* bf */
				break;
			case 'n': new_partition();
				break;
			case 'p': list_table();
				break;
			case 'q': close(fd);
				exit(0);
			case 't': change_sysid();
				break;
			case 'u': change_units();
				break;
			case 'v': verify();
				break;
			case 'w': write_table();
			case 'x': xselect();
				break;
			default: menu();
		}
	}
}
