/****************************************************************************
 *
 *     CFDISK
 *
 * cfdisk is a curses based disk drive partitioning program that can
 * create partitions for a wide variety of operating systems including
 * Linux, MS-DOS and OS/2.
 *
 * cfdisk was inspired by the fdisk program, by A. V. Le Blanc
 * (LeBlanc@mcc.ac.uk).
 *
 *     Copyright (C) 1994 Kevin E. Martin (martin@cs.unc.edu)
 *
 * cfdisk is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * cfdisk is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with cfdisk; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Created:	Fri Jan 28 22:46:58 1994, martin@cs.unc.edu
 * >2GB patches: Sat Feb 11 09:08:10 1995, faith@cs.unc.edu
 * Prettier menus: Sat Feb 11 09:08:25 1995, Janne Kukonlehto
 *                                           <jtklehto@stekt.oulu.fi>
 * Versions 0.8e-n: aeb@cwi.nl
 *  Recognition of NTFS / HPFS difference inspired by patches
 *  from Marty Leisner <leisner@sdsp.mc.xerox.com>
 *  Exit codes by Enrique Zanardi <ezanardi@ull.es>:
 *     0: all went well
 *     1: command line error
 *     2: hardware problems [BAD_SEEK, BAD_READ, BAD_WRITE or BAD_OPEN].
 *     3: ioctl(fd, HDIO_GETGEO,...) failed [BAD_GEOMETRY].
 *     4: bad partition table on disk. [BAD_PRIMARY or BAD_LOGICAL].
 *
 ****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#ifdef SLCURSES
  #include <slcurses.h>
#else
#if NCH
  #include <ncurses.h>
#else
  #include <curses.h>
#endif
#endif
#include <signal.h>
#include <math.h>
#include <locale.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/hdreg.h>
#include <linux/fs.h>		/* for BLKRRPART */

#if defined(__GNUC__) || defined(HAS_LONG_LONG)
typedef long long ext2_loff_t;
#else
typedef long      ext2_loff_t;
#endif

extern ext2_loff_t ext2_llseek(unsigned int fd, ext2_loff_t offset,
			       unsigned int origin);

#define VERSION "0.8n"

#define DEFAULT_DEVICE "/dev/hda"
#define ALTERNATE_DEVICE "/dev/sda"

#define LINE_LENGTH 80
#define MAXIMUM_PARTS 60

#define SECTOR_SIZE 512

#define MAX_CYLINDERS 65535
#define MAX_HEADS 255
#define MAX_SECTORS 63

#define ACTIVE_FLAG 0x80
#define PART_TABLE_FLAG0 0x55
#define PART_TABLE_FLAG1 0xAA

#define UNUSABLE -1
#define FREE_SPACE     0x00
#define DOS_EXTENDED   0x05
#define OS2_OR_NTFS    0x07
#define WIN98_EXTENDED 0x0f
#define LINUX_EXTENDED 0x85
#define LINUX_MINIX    0x81
#define LINUX_SWAP     0x82
#define LINUX          0x83

#define ADD_EXISTS "This partition is already in use"
#define ADD_UNUSABLE "This partition is unusable"
#define DEL_EMPTY "Cannot delete an empty partition"
#define ID_EMPTY "Cannot change FS Type to empty"
#define ID_EXT "Cannot change FS Type to extended"
#define NEED_EXT "No room to create the extended partition"
#define NO_FLAGS "Cannot make this partition bootable"
#define NO_MORE_PARTS "No more partitions"
#define PRINT_OPEN_ERR "Cannot open file '%s'"
#define TWO_EXTENDEDS "Cannot create logical drive here -- would create two extended partitions"
#define TYPE_EMPTY "Cannot change the type of an empty partition"
#define BAD_COMMAND "Illegal command"
#define MAX_UNMAXABLE "Cannot maximize this partition"
#define BAD_OPEN "Cannot open disk drive"
#define BAD_SEEK "Cannot seek on disk drive"
#define BAD_READ "Cannot read disk drive"
#define BAD_WRITE "Cannot write disk drive"
#define BAD_GEOMETRY "Cannot read disk drive geometry"
#define BAD_PRIMARY "Bad primary partition"
#define BAD_LOGICAL "Bad logical partition"
#define BAD_CYLINDERS "Illegal cylinders value"
#define BAD_HEADS "Illegal heads value"
#define BAD_SECTORS "Illegal sectors value"
#define READONLY_WARN "Opened disk read-only - you have no permission to write"
#define WRITE_WARN "Warning!!  This may destroy data on your disk!"
#define YES_NO "Please enter `yes' or `no'"
#define WRITING_PART "Writing partition table to disk..."
#define YES_WRITE "Wrote partition table to disk"
#define NO_WRITE "Did not write partition table to disk"
#define RRPART_FAILED "Wrote partition table, but re-read table failed.  Reboot to update table."
#define NOT_DOS_MBR_BOOTABLE "Not precisely one primary partition is bootable. DOS MBR cannot boot this."

#define PRI_OR_LOG -1
#define PRIMARY -2
#define LOGICAL -3

#define COL_ID_WIDTH 20

#define CR '\015'
#define ESC '\033'
#define DEL '\177'
#define BELL '\007'
/* '\014' == ^L */
#define REDRAWKEY '\014'

/* Display units */
#define MEGABYTES 1
#define SECTORS 2
#define CYLINDERS 3

#define GS_DEFAULT -1
#define GS_ESCAPE -2

#define PRINT_RAW_TABLE 1
#define PRINT_SECTOR_TABLE 2
#define PRINT_PARTITION_TABLE 4

#define IS_PRIMARY(p) ((p) >= 0 && (p) < 4)
#define IS_LOGICAL(p) ((p) > 3)

#define round_int(d) ((double)((int)(d+0.5)))
#define ceiling(d) ((double)(((d) != (int)(d)) ? (int)(d+1.0) : (int)(d)))

#define set_hsc(h,s,c,sector) \
{ \
      s = sector % sectors + 1;	\
      sector /= sectors;	\
      h = sector % heads;	\
      sector /= heads;		\
      c = sector & 0xFF;	\
      s |= (sector >> 2) & 0xC0;\
}

#define is_extended(x)	((x) == DOS_EXTENDED || (x) == WIN98_EXTENDED || \
			 (x) == LINUX_EXTENDED)

#define is_dos_partition(x) ((x) == 1 || (x) == 4 || (x) == 6)
#define may_have_dos_label(x) (is_dos_partition(x) \
   || (x) == 7 || (x) == 0xb || (x) == 0xc || (x) == 0xe \
   || (x) == 0x11 || (x) == 0x14 || (x) == 0x16 || (x) == 0x17)

struct partition {
        unsigned char boot_ind;         /* 0x80 - active */
        unsigned char head;             /* starting head */
        unsigned char sector;           /* starting sector */
        unsigned char cyl;              /* starting cylinder */
        unsigned char sys_ind;          /* What partition type */
        unsigned char end_head;         /* end head */
        unsigned char end_sector;       /* end sector */
        unsigned char end_cyl;          /* end cylinder */
        unsigned char start4[4];        /* starting sector counting from 0 */
        unsigned char size4[4];         /* nr of sectors in partition */
};

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

#define ALIGNMENT 2
typedef union {
    struct {
	unsigned char align[ALIGNMENT];
	unsigned char b[SECTOR_SIZE];
    } c;
    struct {
	unsigned char align[ALIGNMENT];
	unsigned char buffer[0x1BE];
	struct partition part[4];
	unsigned char magicflag[2];
    } p;
} partition_table;

typedef struct {
    int first_sector;	/* first sector in partition */
    int last_sector;	/* last sector in partition */
    int offset;		/* offset from first sector to start of data */
    int flags;		/* active == 0x80 */
    int id;		/* filesystem type */
    int num;		/* number of partition -- primary vs. logical */
#define LABELSZ 16
    char volume_label[LABELSZ+1];
#define OSTYPESZ 8
    char ostype[OSTYPESZ+1];
#define FSTYPESZ 8
    char fstype[FSTYPESZ+1];
} partition_info;

char *disk_device = DEFAULT_DEVICE;
int fd;
int heads = 0;
int sectors = 0;
int cylinders = 0;
int changed = FALSE;
int opened = FALSE;
int opentype;
int curses_started = 0;

partition_info p_info[MAXIMUM_PARTS];
partition_info ext_info;
int num_parts = 0;

int logical = 0;
int logical_sectors[MAXIMUM_PARTS];

__sighandler_t old_SIGINT, old_SIGTERM;

int arrow_cursor = FALSE;
int display_units = MEGABYTES;
int zero_table = FALSE;
int print_only = 0;

/* Curses screen information */
int cur_part = 0;
int warning_last_time = FALSE;
int defined = FALSE;
int COLUMNS = 80;
int NUM_ON_SCREEN = 1;

/* Y coordinates */
int HEADER_START = 0;
int DISK_TABLE_START = 5;
int WARNING_START = 23;
int COMMAND_LINE_Y = 21;

/* X coordinates */
int NAME_START = 4;
int FLAGS_START = 16;
int PTYPE_START = 28;
int FSTYPE_START = 38;
int LABEL_START = 54;
int SIZE_START = 70;
int COMMAND_LINE_X = 5;

#define NUM_PART_TYPES 256
char *partition_type[NUM_PART_TYPES] = {
    [LINUX_MINIX] = "Linux/MINIX",
    [LINUX_SWAP]  = "Linux Swap",
    [LINUX]       = "Linux",
    [FREE_SPACE]  = "Free Space",
    [DOS_EXTENDED]= "Extended",
    [LINUX_EXTENDED] = "Linux extended",
    [0x01]        = "DOS FAT12",
    [0x02]        = "XENIX root",
    [0x03]        = "XENIX usr",
    [0x04]        = "DOS FAT16",
    [0x06]        = "DOS FAT16 (big)",
    [OS2_OR_NTFS] = "OS/2 HPFS or NTFS",
    [0x08]        = "AIX",
    [0x09]        = "AIX bootable",
    [0x0A]        = "OS/2 Boot Manager",
    [0x0B]        = "Win95 FAT32",
    [0x0C]        = "Win95 FAT32 (LBA)",
    [0x0E]        = "Win95 FAT16 (LBA)",
    [0x0F]        = "Win95 Extended (LBA)",
    [0x11]        = "Hidden DOS FAT12",
    [0x14]        = "Hidden DOS FAT16",
    [0x16]        = "Hidden DOS FAT16 (big)",
    [0x17]        = "Hidden OS/2 HPFS or NTFS",
    [0x40]        = "Venix 80286",
    [0x41]	  = "PPC PReP boot",
    [0x51]        = "Novell?",
    [0x52]        = "Microport",
    [0x63]        = "GNU HURD",
    [0x64]        = "Novell Netware 286",
    [0x65]        = "Novell Netware 386",
    [0x75]        = "PC/IX",
    [0x80]        = "Old MINIX",
    [0x93]        = "Amoeba",
    [0x94]        = "Amoeba BBT",
    [0xA5]        = "BSD/386",
    [0xA6]        = "OpenBSD",
    [0xA7]        = "NEXTSTEP",
    [0xB7]        = "BSDI fs",
    [0xB8]        = "BSDI swap",
    [0xC7]        = "Syrinx",
    [0xDB]        = "CP/M",
    [0xE1]        = "DOS access",
    [0xE3]        = "DOS R/O",
    [0xEB]	  = "BeOS fs",
    [0xF2]        = "DOS secondary",
    [0xFF]        = "BBT"
};

/* Some libc's have their own basename() */
char *my_basename(char *devname)
{
    char *s = rindex(devname, '/');
    return s ? s+1 : devname;
}

char *partition_type_text(int i)
{
    if (p_info[i].id == UNUSABLE)
	 return "Unusable";
    else if (p_info[i].id == FREE_SPACE)
	 return "Free Space";
    else if (p_info[i].id == LINUX) {
	 if (!strcmp(p_info[i].fstype, "ext2"))
	      return "Linux ext2";
	 else
	      return "Linux";
    } else if (p_info[i].id == OS2_OR_NTFS) {
	 if (!strncmp(p_info[i].fstype, "HPFS", 4))
	      return "OS/2 HPFS";
	 else if (!strncmp(p_info[i].ostype, "OS2", 3))
	      return "OS/2 IFS";
	 else if (!p_info[i].ostype)
	      return p_info[i].ostype;
	 else
	      return "NTFS";
    } else
	 return partition_type[p_info[i].id];
}

void fdexit(int ret)
{
    if (opened)
	close(fd);

    if (changed) {
	fprintf(stderr, "Disk has been changed.\n");
	fprintf(stderr, "Reboot the system to ensure the partition "
			"table is correctly updated.\n");
	
	fprintf( stderr, "\nWARNING: If you have created or modified any\n"
		         "DOS 6.x partitions, please see the cfdisk manual\n"
		         "page for additional information.\n" );
    }

    exit(ret);
}

int get_string(char *str, int len, char *def)
{
    char c;
    int i = 0;
    int x, y;
    int use_def = FALSE;

    getyx(stdscr, y, x);
    clrtoeol();

    str[i] = 0;

    if (def != NULL) {
	mvaddstr(y, x, def);
	move(y, x);
	use_def = TRUE;
    }

    refresh();
    while ((c = getch()) != '\n' && c != CR) {
	switch (c) {
	case ESC:
	    move(y, x);
	    clrtoeol();
	    refresh();
	    return GS_ESCAPE;
	case DEL:
	case '\b':
	    if (i > 0) {
		str[--i] = 0;
		mvaddch(y, x+i, ' ');
		move(y, x+i);
	    } else if (use_def) {
		clrtoeol();
		use_def = FALSE;
	    } else
		putchar(BELL);
	    break;
	default:
	    if (i < len && isprint(c)) {
		mvaddch(y, x+i, c);
		if (use_def) {
		    clrtoeol();
		    use_def = FALSE;
		}
		str[i++] = c;
		str[i] = 0;
	    } else
		putchar(BELL);
	}
	refresh();
    }

    if (use_def)
	return GS_DEFAULT;
    else
	return i;
}

void clear_warning(void)
{
    int i;

    if (!curses_started || !warning_last_time)
	return;

    move(WARNING_START,0);
    for (i = 0; i < COLS; i++)
	addch(' ');

    warning_last_time = FALSE;
}

void print_warning(char *s)
{
    if (!curses_started) {
	 fprintf(stderr, "%s\n", s);
    } else {
	mvaddstr(WARNING_START, (COLS-strlen(s))/2, s);
	putchar(BELL); /* CTRL-G */

	warning_last_time = TRUE;
    }
}

void die_x(int ret);

void fatal(char *s, int ret)
{
    char str[LINE_LENGTH];

    if (curses_started) {
	 sprintf(str, "FATAL ERROR: %s", s);
	 mvaddstr(WARNING_START, (COLS-strlen(str))/2, str);
	 sprintf(str, "Press any key to exit fdisk");
	 mvaddstr(WARNING_START+1, (COLS-strlen(str))/2, str);
	 putchar(BELL); /* CTRL-G */
	 refresh();
	 (void)getch();
	 die_x(ret);
    } else {
	 fprintf(stderr, "FATAL ERROR: %s\n", s);
	 exit(ret);
    }
}

void die(int dummy)
{
    die_x(0);
}

void die_x(int ret)
{
    signal(SIGINT, old_SIGINT);
    signal(SIGTERM, old_SIGTERM);
#ifdef SLCURSES
    SLsmg_gotorc(LINES-1, 0);
    SLsmg_refresh();
#else
    mvcur(0, COLS-1, LINES-1, 0);
#endif
    nl();
    endwin();
    printf("\n");
    fdexit(ret);
}

void read_sector(char *buffer, int sect_num)
{
    if (ext2_llseek(fd, ((ext2_loff_t) sect_num)*SECTOR_SIZE, SEEK_SET) < 0)
	fatal(BAD_SEEK, 2);
    if (read(fd, buffer, SECTOR_SIZE) != SECTOR_SIZE)
	fatal(BAD_READ, 2);
}

void write_sector(char *buffer, int sect_num)
{
    if (ext2_llseek(fd, ((ext2_loff_t) sect_num)*SECTOR_SIZE, SEEK_SET) < 0)
	fatal(BAD_SEEK, 2);
    if (write(fd, buffer, SECTOR_SIZE) != SECTOR_SIZE)
	fatal(BAD_WRITE, 2);
}

void dos_copy_to_info(char *to, int tosz, char *from, int fromsz) {
     int i;

     for(i=0; i<tosz && i<fromsz && isascii(from[i]); i++)
	  to[i] = from[i];
     to[i] = 0;
}

void get_dos_label(int i)
{
    char sector[128];
#define DOS_OSTYPE_OFFSET 3
#define DOS_LABEL_OFFSET 43
#define DOS_FSTYPE_OFFSET 54
#define DOS_OSTYPE_SZ 8
#define DOS_LABEL_SZ 11
#define DOS_FSTYPE_SZ 8
    ext2_loff_t offset;

    offset = ((ext2_loff_t) p_info[i].first_sector + p_info[i].offset)
	 * SECTOR_SIZE;
    if (ext2_llseek(fd, offset, SEEK_SET) == offset
	&& read(fd, &sector, sizeof(sector)) == sizeof(sector)) {
	 dos_copy_to_info(p_info[i].ostype, OSTYPESZ,
			  sector+DOS_OSTYPE_OFFSET, DOS_OSTYPE_SZ);
	 dos_copy_to_info(p_info[i].volume_label, LABELSZ,
			  sector+DOS_LABEL_OFFSET, DOS_LABEL_SZ);
	 dos_copy_to_info(p_info[i].fstype, FSTYPESZ,
			  sector+DOS_FSTYPE_OFFSET, DOS_FSTYPE_SZ);
    }
}

void get_ext2_label(int i)
{
#define EXT2_SUPER_MAGIC 0xEF53
#define EXT2LABELSZ 16
    struct ext2_super_block {
	char  s_dummy0[56];
	unsigned char  s_magic[2];
	char  s_dummy1[62];
	char  s_volume_name[EXT2LABELSZ];
	char  s_last_mounted[64];
	char  s_dummy2[824];
    } sb;
    char *label = sb.s_volume_name;
    ext2_loff_t offset;
    int j;

    offset = ((ext2_loff_t) p_info[i].first_sector + p_info[i].offset)
	 * SECTOR_SIZE + 1024;
    if (ext2_llseek(fd, offset, SEEK_SET) == offset
	&& read(fd, &sb, sizeof(sb)) == sizeof(sb)
	&& sb.s_magic[0] + 256*sb.s_magic[1] == EXT2_SUPER_MAGIC) {
	 for(j=0; j<EXT2LABELSZ; j++)
	      if(!isprint(label[j]))
		   label[j] = 0;
	 label[EXT2LABELSZ] = 0;
	 strncpy(p_info[i].volume_label, label, LABELSZ);
	 strncpy(p_info[i].fstype, "ext2", FSTYPESZ);
    }
}

void check_part_info(void)
{
    int i, pri = 0, log = 0;

    for (i = 0; i < num_parts; i++)
	if (p_info[i].id > 0 && IS_PRIMARY(p_info[i].num))
	    pri++;
	else if (p_info[i].id > 0 && IS_LOGICAL(p_info[i].num))
	    log++;
    if (is_extended(ext_info.id)) {
	if (log > 0)
	    pri++;
	else {
	    ext_info.first_sector = 0;
	    ext_info.last_sector = 0;
	    ext_info.offset = 0;
	    ext_info.flags = 0;
	    ext_info.id = FREE_SPACE;
	    ext_info.num = PRIMARY;
	}
    }

    if (pri >= 4) {
	for (i = 0; i < num_parts; i++)
	    if (p_info[i].id == FREE_SPACE || p_info[i].id == UNUSABLE) {
		if (is_extended(ext_info.id)) {
		    if (p_info[i].first_sector >= ext_info.first_sector &&
			p_info[i].last_sector <= ext_info.last_sector) {
			p_info[i].id = FREE_SPACE;
			p_info[i].num = LOGICAL;
		    } else if (i > 0 &&
			       p_info[i-1].first_sector >=
			       ext_info.first_sector &&
			       p_info[i-1].last_sector <=
			       ext_info.last_sector) {
			p_info[i].id = FREE_SPACE;
			p_info[i].num = LOGICAL;
		    } else if (i < num_parts-1 &&
			       p_info[i+1].first_sector >=
			       ext_info.first_sector &&
			       p_info[i+1].last_sector <=
			       ext_info.last_sector) {
			p_info[i].id = FREE_SPACE;
			p_info[i].num = LOGICAL;
		    } else
			p_info[i].id = UNUSABLE;
		} else /* if (!is_extended(ext_info.id)) */
		    p_info[i].id = UNUSABLE;
	    } else /* if (p_info[i].id > 0) */
		while (0); /* Leave these alone */
    } else { /* if (pri < 4) */
	for (i = 0; i < num_parts; i++) {
	    if (p_info[i].id == UNUSABLE)
		p_info[i].id = FREE_SPACE;
	    if (p_info[i].id == FREE_SPACE) {
		if (is_extended(ext_info.id)) {
		    if (p_info[i].first_sector >= ext_info.first_sector &&
			p_info[i].last_sector <= ext_info.last_sector)
			p_info[i].num = LOGICAL;
		    else if (i > 0 &&
			     p_info[i-1].first_sector >=
			     ext_info.first_sector &&
			     p_info[i-1].last_sector <=
			     ext_info.last_sector)
			p_info[i].num = PRI_OR_LOG;
		    else if (i < num_parts-1 &&
			     p_info[i+1].first_sector >=
			     ext_info.first_sector &&
			     p_info[i+1].last_sector <=
			     ext_info.last_sector)
			p_info[i].num = PRI_OR_LOG;
		    else
			p_info[i].num = PRIMARY;
		} else /* if (!is_extended(ext_info.id)) */
		    p_info[i].num = PRI_OR_LOG;
	    } else /* if (p_info[i].id > 0) */
		while (0); /* Leave these alone */
	}
    }
}

void remove_part(int i)
{
    int p;

    for (p = i; p < num_parts; p++)
	p_info[p] = p_info[p+1];

    num_parts--;
}

void insert_empty_part(int i, int first, int last)
{
    int p;

    for (p = num_parts; p > i; p--)
	 p_info[p] = p_info[p-1];

    p_info[i].first_sector = first;
    p_info[i].last_sector = last;
    p_info[i].offset = 0;
    p_info[i].flags = 0;
    p_info[i].id = FREE_SPACE;
    p_info[i].num = PRI_OR_LOG;
    p_info[i].volume_label[0] = 0;
    p_info[i].fstype[0] = 0;
    p_info[i].ostype[0] = 0;

    num_parts++;
}

void del_part(int i)
{
    int num = p_info[i].num;

    if (i > 0 && (p_info[i-1].id == FREE_SPACE ||
		  p_info[i-1].id == UNUSABLE)) {
	/* Merge with previous partition */
	p_info[i-1].last_sector = p_info[i].last_sector;
	remove_part(i--);
    }

    if (i < num_parts - 1 && (p_info[i+1].id == FREE_SPACE ||
			      p_info[i+1].id == UNUSABLE)) {
	/* Merge with next partition */
	p_info[i+1].first_sector = p_info[i].first_sector;
	remove_part(i);
    }

    if (i > 0)
	p_info[i].first_sector = p_info[i-1].last_sector + 1;
    else
	p_info[i].first_sector = 0;

    if (i < num_parts - 1)
	p_info[i].last_sector = p_info[i+1].first_sector - 1;
    else
	p_info[i].last_sector = sectors*heads*cylinders - 1;

    p_info[i].offset = 0;
    p_info[i].flags = 0;
    p_info[i].id = FREE_SPACE;
    p_info[i].num = PRI_OR_LOG;

    if (IS_LOGICAL(num)) {
	/* We have a logical partition --> shrink the extended partition
	 * if (1) this is the first logical drive, or (2) this is the
	 * last logical drive; and if there are any other logical drives
	 * then renumber the ones after "num".
	 */
	if (i == 0 || (i > 0 && IS_PRIMARY(p_info[i-1].num))) {
	    ext_info.first_sector = p_info[i].last_sector + 1;
	    ext_info.offset = 0;
	}
	if (i == num_parts-1 ||
	    (i < num_parts-1 && IS_PRIMARY(p_info[i+1].num)))
	    ext_info.last_sector = p_info[i].first_sector - 1;
	for (i = 0; i < num_parts; i++)
	    if (p_info[i].num > num)
		p_info[i].num--;
    }

    /* Clean up the rest of the partitions */
    check_part_info();
}

int add_part(int num, int id, int flags, int first, int last, int offset,
	     int want_label)
{
    int i, pri = 0, log = 0;

    if (num_parts == MAXIMUM_PARTS ||
	first < 0 ||
	first >= cylinders*heads*sectors ||
	last < 0 ||
	last >= cylinders*heads*sectors) {
	return -1;		/* bad start or end */
    }

    for (i = 0; i < num_parts; i++)
	if (p_info[i].id > 0 && IS_PRIMARY(p_info[i].num))
	    pri++;
	else if (p_info[i].id > 0 && IS_LOGICAL(p_info[i].num))
	    log++;
    if (is_extended(ext_info.id) && log > 0)
	pri++;

    if (IS_PRIMARY(num)) {
	if (pri >= 4) {
	    return -1;		/* no room for more */
	} else
	    pri++;
    }

    for (i = 0; i < num_parts && p_info[i].last_sector < first; i++);

    if (i == num_parts || p_info[i].id != FREE_SPACE
	|| last > p_info[i].last_sector) {
	return -1;
    }

    if (is_extended(id)) {
	if (ext_info.id != FREE_SPACE) {
	    return -1;		/* second extended */
	}
	else if (IS_PRIMARY(num)) {
	    ext_info.first_sector = first;
	    ext_info.last_sector = last;
	    ext_info.offset = offset;
	    ext_info.flags = flags;
	    ext_info.id = id;
	    ext_info.num = num;
	    ext_info.volume_label[0] = 0;
	    ext_info.fstype[0] = 0;
	    ext_info.ostype[0] = 0;
	    return 0;
	} else {
	    return -1;		/* explicit extended logical */
	}
    }

    if (IS_LOGICAL(num)) {
	if (!is_extended(ext_info.id)) {
	    print_warning("!!!! Internal error creating logical "
			  "drive with no extended partition !!!!");
	} else {
	    /* We might have a logical partition outside of the extended
	     * partition's range --> we have to extend the extended
	     * partition's range to encompass this new partition, but we
	     * must make sure that there are no primary partitions between
	     * it and the closest logical drive in extended partition.
	     */
	    if (first < ext_info.first_sector) {
		if (i < num_parts-1 && IS_PRIMARY(p_info[i+1].num)) {
		    print_warning(TWO_EXTENDEDS);
		    return -1;
		} else {
		    if (first == 0) {
			ext_info.first_sector = 0;
			ext_info.offset = first = offset;
		    } else {
			ext_info.first_sector = first;
		    }
		}
	    } else if (last > ext_info.last_sector) {
		if (i > 0 && IS_PRIMARY(p_info[i-1].num)) {
		    print_warning(TWO_EXTENDEDS);
		    return -1;
		} else {
		    ext_info.last_sector = last;
		}
	    }
	}
    }

    if (first != p_info[i].first_sector &&
	!(IS_LOGICAL(num) && first == offset)) {
	insert_empty_part(i, p_info[i].first_sector, first-1);
	i++;
    }

    if (last != p_info[i].last_sector)
	insert_empty_part(i+1, last+1, p_info[i].last_sector);

    p_info[i].first_sector = first;
    p_info[i].last_sector = last;
    p_info[i].offset = offset;
    p_info[i].flags = flags;
    p_info[i].id = id;
    p_info[i].num = num;
    p_info[i].volume_label[0] = 0;
    p_info[i].fstype[0] = 0;
    p_info[i].ostype[0] = 0;
    if (want_label) {
	 if (may_have_dos_label(id))
	      get_dos_label(i);
	 else if (id == LINUX)
	      get_ext2_label(i);
    }

    check_part_info();

    return 0;
}

int find_primary(void)
{
    int num = 0, cur = 0;

    while (cur < num_parts && IS_PRIMARY(num))
	if ((p_info[cur].id > 0 && p_info[cur].num == num) ||
	    (is_extended(ext_info.id) && ext_info.num == num)) {
	    num++;
	    cur = 0;
	} else
	    cur++;

    if (!IS_PRIMARY(num))
	return -1;
    else
	return num;
}

int find_logical(int i)
{
    int num = -1;
    int j;

    for (j = i; j < num_parts && num == -1; j++)
	if (p_info[j].id > 0 && IS_LOGICAL(p_info[j].num))
	    num = p_info[j].num;

    if (num == -1) {
	num = 4;
	for (j = 0; j < num_parts; j++)
	    if (p_info[j].id > 0 && p_info[j].num == num)
		num++;
    }

    return num;
}

void inc_logical(int i)
{
    int j;

    for (j = i; j < num_parts; j++)
	if (p_info[j].id > 0 && IS_LOGICAL(p_info[j].num))
	    p_info[j].num++;
}

/* Command menu support by Janne Kukonlehto <jtklehto@phoenix.oulu.fi> September 1994 */

/* Constants for menuType parameter of menuSelect function */
#define MENU_HORIZ 1
#define MENU_VERT 2
#define MENU_ACCEPT_OTHERS 4
#define MENU_BUTTON 8
/* Miscellenous constants */
#define MENU_SPACING 2
#define MENU_MAX_ITEMS 256 /* for simpleMenu function */
#define MENU_UP 1
#define MENU_DOWN 2
#define MENU_RIGHT 3
#define MENU_LEFT 4

struct MenuItem
{
    int key; /* Keyboard shortcut; if zero, then there is no more items in the menu item table */
    char *name; /* Item name, should be eight characters with current implementation */
    char *desc; /* Item description to be printed when item is selected */
};

/*
 * Actual function which prints the button bar and highlights the active button
 * Should not be called directly. Call function menuSelect instead.
 */

int menuUpdate( int y, int x, struct MenuItem *menuItems, int itemLength,
		char *available, int menuType, int current )
{
    int i, lmargin = x, ymargin = y;
    /* Print available buttons */
    move( y, x ); clrtoeol();
    for( i = 0; menuItems[i].key; i++ )
    {
        char buff[20];
        int lenName;
        /* Search next available button */
        while( menuItems[i].key && !strchr(available, menuItems[i].key) )
        {
            i++;
        }
        if( !menuItems[i].key ) break; /* No more menu items */
        /* If selected item is not available and we have bypassed it,
	   make current item selected */
        if( current < i && menuItems[current].key < 0 ) current = i;
        /* If current item is selected, highlight it */
        if( current == i ) /*attron( A_REVERSE )*/ standout ();
        /* Print item */
        lenName = strlen( menuItems[i].name );
        if(lenName > itemLength)
            print_warning("Menu item too long. Menu may look odd.");
        if( menuType & MENU_BUTTON )
            sprintf( buff, "[%*s%-*s]", (itemLength - lenName) / 2, "",
                (itemLength - lenName + 1) / 2 + lenName, menuItems[i].name );
        else
            sprintf( buff, "%*s%-*s", (itemLength - lenName) / 2, "",
                (itemLength - lenName + 1) / 2 + lenName, menuItems[i].name );
        mvaddstr( y, x, buff );
        /* Lowlight after selected item */
        if( current == i ) /*attroff( A_REVERSE )*/ standend ();
        /* Calculate position for the next item */
        if( menuType & MENU_VERT )
        {
            y += 1;
            if( y >= WARNING_START )
            {
                y = ymargin;
                x += itemLength + MENU_SPACING;
                if( menuType & MENU_BUTTON ) x += 2;
            }
        }
        else
        {
            x += itemLength + MENU_SPACING;
            if( menuType & MENU_BUTTON ) x += 2;
            if( x > COLUMNS - lmargin - 12 )
            {
                x = lmargin;
                y ++ ;
            }
        }
    }
    /* Print the description of selected item */
    mvaddstr( WARNING_START + 1,
	      (COLUMNS - strlen( menuItems[current].desc )) / 2,
	      menuItems[current].desc );
    return y;
}

/* This function takes a list of menu items, lets the user choose one of them *
 * and returns the value keyboard shortcut of the selected menu item          */

int menuSelect( int y, int x, struct MenuItem *menuItems, int itemLength, char *available, int menuType, int menuDefault )
{
    int i, ylast = y, key = 0, current = menuDefault;
    if( !( menuType & ( MENU_HORIZ | MENU_VERT ) ) )	
    {
        print_warning("Menu without direction. Defaulting horizontal.");
        menuType |= MENU_HORIZ;
    }
    /* Make sure that the current is one of the available items */
    while( !strchr(available, menuItems[current].key) )
    {
        current ++ ;
        if( !menuItems[current].key ) current = 0;
    }
    /* Repeat until allowable choice has been made */
    while( !key )
    {
        /* Display the menu */
        ylast = menuUpdate( y, x, menuItems, itemLength, available,
			    menuType, current );
        refresh();
        key = getch();
        /* Clear out all prompts and such */
        clear_warning();
        for( i = y; i < ylast; i ++ )
        {
            move( i, x );
            clrtoeol();
        }
        move( WARNING_START + 1, 0 );
        clrtoeol();
        /* Cursor keys */
        if( key == ESC )
        {
            /* Check whether this is a real ESC or one of extended keys */
            /*nodelay(stdscr, TRUE);*/
            key = getch();
            /*nodelay(stdscr, FALSE);*/
            if( key == /*ERR*/ ESC )
            {
                /* This is a real ESC */
                key = ESC;
            }
            if( key == '[' )
            {
                /* This is one extended keys */
                switch( getch() )
                {
                    case 'A': /* Up arrow */
                        if( menuType & MENU_VERT )
                        {
                            do {
                                current -- ;
                                if( current < 0 ) while( menuItems[current+1].key ) current ++ ;
                            } while( !strchr( available, menuItems[current].key ) );
                            key = 0;
                        }
                        else
                            key = MENU_UP;
                        break;
                    case 'B': /* Down arrow */
                        if( menuType & MENU_VERT )
                        {
                            do {
                                current ++ ;
                                if( !menuItems[current].key ) current = 0 ;
                            } while( !strchr( available, menuItems[current].key ) );
                            key = 0;
                        }
                        else
                            key = MENU_DOWN;
                        break;
                    case 'C': /* Right arrow */
                        if( menuType & MENU_HORIZ )
                        {
                            do {
                                current ++ ;
                                if( !menuItems[current].key ) 
                                {
                                    current = 0 ;
                                }
                            } while( !strchr( available, menuItems[current].key ) );
                            key = 0;
                        }
                        else
                            key = MENU_RIGHT;
                        break;
                    case 'D': /* Left arrow */
                        if( menuType & MENU_HORIZ )
                        {
                            do {
                                current -- ;
                                if( current < 0 )
                                {
                                    while( menuItems[current + 1].key ) current ++ ;
                                }
                            } while( !strchr( available, menuItems[current].key ) );
                            key = 0;
                        }
                        else
                            key = MENU_LEFT;
                        break;
                }
            }
        }
        /* Enter equals to the keyboard shortcut of current menu item */
        if( key == 13 )
        {
            key = menuItems[current].key;
        }
        /* Should all keys to be accepted? */
        if( key && (menuType & MENU_ACCEPT_OTHERS) ) break;
        /* Is pressed key among acceptable ones */
        if( key && (strchr(available, tolower(key)) || strchr(available, key)))
	    break;
        /* The key has not been accepted so far -> let's reject it */
        if( key )
        {
            key = 0;
            putchar( BELL );
            print_warning("Illegal key");
        }
    }
    /* Clear out prompts and such */
    clear_warning();
    for( i = y; i <= ylast; i ++ )
    {
        move( i, x );
        clrtoeol();
    }
    move( WARNING_START + 1, 0 );
    clrtoeol();
    return key;
}

/* A function which displays "Press a key to continue"                  *
 * and waits for a keypress.                                            *
 * Perhaps calling function menuSelect is a bit overkill but who cares? */

void menuContinue(void)
{
    static struct MenuItem menuContinueBtn[]=
    {
        { 'c', "", "Press a key to continue" },
        { 0, NULL, NULL }
    };

    menuSelect(COMMAND_LINE_Y, COMMAND_LINE_X,
	menuContinueBtn, 0, "c", MENU_HORIZ | MENU_ACCEPT_OTHERS, 0 );
}

/* Function menuSelect takes way too many parameters  *
 * Luckily, most of time we can do with this function */

int menuSimple(struct MenuItem *menuItems, int menuDefault)
{
    int i, j, itemLength = 0;
    char available[MENU_MAX_ITEMS];
    for(i = 0; menuItems[i].key; i++)
    {
        j = strlen( menuItems[i].name );
        if( j > itemLength ) itemLength = j;
        available[i] = menuItems[i].key;
    }
    available[i] = 0;
    return menuSelect(COMMAND_LINE_Y, COMMAND_LINE_X, menuItems, itemLength,
        available, MENU_HORIZ | MENU_BUTTON, menuDefault);
}

/* End of command menu support code */

void new_part(int i)
{
    char response[LINE_LENGTH], def[LINE_LENGTH];
    char c;
    int first = p_info[i].first_sector;
    int last = p_info[i].last_sector;
    int offset = 0;
    int flags = 0;
    int id = LINUX;
    int num = -1;
    int num_sects = last - first + 1;
    int len, ext, j;

    if (p_info[i].num == PRI_OR_LOG) {
        static struct MenuItem menuPartType[]=
        {
            { 'p', "Primary", "Create a new primary partition" },
            { 'l', "Logical", "Create a new logical partition" },
            { ESC, "Cancel", "Don't create a partition" },
            { 0, NULL, NULL }
        };
        
	c = menuSimple( menuPartType, 0 );
	if (toupper(c) == 'P')
	    num = find_primary();
	else if (toupper(c) == 'L')
	    num = find_logical(i);
	else
	    return;
    } else if (p_info[i].num == PRIMARY)
	num = find_primary();
    else if (p_info[i].num == LOGICAL)
	num = find_logical(i);
    else
	print_warning("!!! Internal error !!!");

    sprintf(def, "%.2f", ceiling(num_sects/20.48)/100);
    mvaddstr(COMMAND_LINE_Y, COMMAND_LINE_X, "Size (in MB): ");
    if ((len = get_string(response, LINE_LENGTH, def)) <= 0 &&
	len != GS_DEFAULT)
	return;
    else if (len > 0) {
#define num_cyls(bytes) (round_int(bytes/SECTOR_SIZE/(sectors*heads)))
	for (j = 0;
	     j < len-1 && (isdigit(response[j]) || response[j] == '.');
	     j++);
	if (toupper(response[j]) == 'K') {
	    num_sects = num_cyls(atof(response)*1024)*sectors*heads;
	} else if (toupper(response[j]) == 'M') {
	    num_sects = num_cyls(atof(response)*1024*1024)*sectors*heads;
	} else if (toupper(response[j]) == 'C') {
	    num_sects = round_int(atof(response))*sectors*heads;
	} else if (toupper(response[j]) == 'S') {
	    num_sects = round_int(atof(response));
	} else {
	    num_sects = num_cyls(atof(response)*1024*1024)*sectors*heads;
	}
    }

    if (num_sects <= 0 ||
	num_sects > p_info[i].last_sector - p_info[i].first_sector + 1)
	return;

    move( COMMAND_LINE_Y, COMMAND_LINE_X ); clrtoeol();
    if (num_sects < p_info[i].last_sector - p_info[i].first_sector + 1) {
	/* Determine where inside free space to put partition.
	 */
	static struct MenuItem menuPlace[]=
	{
	    { 'b', "Beginning", "Add partition at beginning of free space" },
	    { 'e', "End", "Add partition at end of free space" },
	    { ESC, "Cancel", "Don't create a partition" },
	    { 0, NULL, NULL }
	};
	c = menuSimple( menuPlace, 0 );
	if (toupper(c) == 'B')
	    last = first + num_sects - 1;
	else if (toupper(c) == 'E')
	    first = last - num_sects + 1;
	else
	    return;
    }

    if (IS_LOGICAL(num) && !is_extended(ext_info.id)) {
	/* We want to add a logical partition, but need to create an
	 * extended partition first.
	 */
	if ((ext = find_primary()) < 0) {
	    print_warning(NEED_EXT);
	    return;
	}
	(void) add_part(ext, DOS_EXTENDED, 0, first, last,
		       (first == 0 ? sectors : 0), 0);
	first = ext_info.first_sector + ext_info.offset;
    }

    if (IS_LOGICAL(num))
	inc_logical(i);

    /* Now we have a complete partition to ourselves */
    if (first == 0 || IS_LOGICAL(num))
	offset = sectors;

    (void) add_part(num, id, flags, first, last, offset, 0);
}

void clear_p_info(void)
{
    num_parts = 1;
    p_info[0].first_sector = 0;
    p_info[0].last_sector = sectors*heads*cylinders - 1;
    p_info[0].offset = 0;
    p_info[0].flags = 0;
    p_info[0].id = FREE_SPACE;
    p_info[0].num = PRI_OR_LOG;

    ext_info.first_sector = 0;
    ext_info.last_sector = 0;
    ext_info.offset = 0;
    ext_info.flags = 0;
    ext_info.id = FREE_SPACE;
    ext_info.num = PRIMARY;
}

void fill_p_info(void)
{
    int pn, i, bs, bsz;
    struct partition *p;
    struct hd_geometry geometry;
    partition_table buffer;
    partition_info tmp_ext = { 0, 0, 0, 0, FREE_SPACE, PRIMARY };

    if ((fd = open(disk_device, O_RDWR)) < 0) {
	 if ((fd = open(disk_device, O_RDONLY)) < 0)
	      fatal(BAD_OPEN, 2);
	 opentype = O_RDONLY;
	 print_warning(READONLY_WARN);
	 if (curses_started) {
	      refresh();
	      getch();
	      clear_warning();
	 }
    } else
	 opentype = O_RDWR;
    opened = TRUE;

    /* Blocks are visible in more than one way:
       e.g. as block on /dev/hda and as block on /dev/hda3
       By a bug in the Linux buffer cache, we will see the old
       contents of /dev/hda when the change was made to /dev/hda3.
       In order to avoid this, discard all blocks on /dev/hda.
       Note that partition table blocks do not live in /dev/hdaN,
       so this only plays a role if we want to show volume labels. */
    ioctl(fd, BLKFLSBUF);	/* ignore errors */
				/* e.g. Permission Denied */

    if (!ioctl(fd, HDIO_GETGEO, &geometry)) {
	if (!heads)
	    heads = geometry.heads;
	if (!sectors)
	    sectors = geometry.sectors;
	if (!cylinders)
	    cylinders = geometry.cylinders;
    }

    if (!heads || !sectors || !cylinders)
	fatal(BAD_GEOMETRY, 3);	/* probably a file or cdrom */

    read_sector(buffer.c.b, 0);

    clear_p_info();

    if (!zero_table) {
	for (i = 0; i < 4; i++) {
	    p = & buffer.p.part[i];
	    bs = get_start_sect(p);
	    bsz = get_nr_sects(p);

	    if (p->sys_ind > 0 &&
		add_part(i, p->sys_ind, p->boot_ind,
			 ((bs <= sectors) ? 0 : bs),
			 bs + bsz - 1,
			 ((bs <= sectors) ? bs : 0),
			 1)) {
		fatal(BAD_PRIMARY, 4);
	    }
	    if (is_extended(buffer.p.part[i].sys_ind))
		tmp_ext = ext_info;
	}

	if (is_extended(tmp_ext.id)) {
	    ext_info = tmp_ext;
	    logical_sectors[logical] =
		 ext_info.first_sector + ext_info.offset;
	    read_sector(buffer.c.b, logical_sectors[logical++]);
	    i = 4;
	    do {
		for (pn = 0;
		     pn < 4 && (!buffer.p.part[pn].sys_ind ||
			       is_extended(buffer.p.part[pn].sys_ind));
		     pn++);

		if (pn < 4) {
			p = & buffer.p.part[pn];
			bs = get_start_sect(p);
			bsz = get_nr_sects(p);

			if (add_part(i++, p->sys_ind, p->boot_ind,
			     logical_sectors[logical-1],
			     logical_sectors[logical-1] + bs + bsz - 1,
			     bs, 1))
				fatal(BAD_LOGICAL, 4);
		}

		for (pn = 0;
		     pn < 4 && !is_extended(buffer.p.part[pn].sys_ind);
		     pn++);
		if (pn < 4) {
			p = & buffer.p.part[pn];
			bs = get_start_sect(p);
			logical_sectors[logical] = ext_info.first_sector
				+ ext_info.offset + bs;
			read_sector(buffer.c.b, logical_sectors[logical++]);
		}
	    } while (pn < 4 && logical < MAXIMUM_PARTS-4);
	}
    }
}

void fill_part_table(struct partition *p, partition_info *pi)
{
    int sects;

    p->boot_ind = pi->flags;
    p->sys_ind = pi->id;
    if (IS_LOGICAL(pi->num))
	set_start_sect(p,pi->offset);
    else
	set_start_sect(p,pi->first_sector + pi->offset);
    set_nr_sects(p, pi->last_sector - (pi->first_sector+pi->offset) + 1);
    sects = (((pi->first_sector+pi->offset)/(sectors*heads) > 1023) ?
	     heads*sectors*1024 - 1 : pi->first_sector+pi->offset);
    set_hsc(p->head, p->sector, p->cyl, sects);
    sects = ((pi->last_sector/(sectors*heads) > 1023) ?
	     heads*sectors*1024 - 1 : pi->last_sector);
    set_hsc(p->end_head, p->end_sector, p->end_cyl, sects);
}

void fill_primary_table(partition_table *buffer)
{
    int i;

    /* Zero out existing table */
    for (i = 0x1BE; i < SECTOR_SIZE; i++)
	buffer->c.b[i] = 0;

    for (i = 0; i < num_parts; i++)
	if (IS_PRIMARY(p_info[i].num))
	    fill_part_table(&(buffer->p.part[p_info[i].num]), &(p_info[i]));

    if (is_extended(ext_info.id))
	fill_part_table(&(buffer->p.part[ext_info.num]), &ext_info);

    buffer->p.magicflag[0] = PART_TABLE_FLAG0;
    buffer->p.magicflag[1] = PART_TABLE_FLAG1;
}

void fill_logical_table(partition_table *buffer, partition_info *pi)
{
    struct partition *p;
    int i, sects;

    for (i = 0; i < logical && pi->first_sector != logical_sectors[i]; i++);
    if (i == logical || buffer->p.magicflag[0] != PART_TABLE_FLAG0
	             || buffer->p.magicflag[1] != PART_TABLE_FLAG1)
	for (i = 0; i < SECTOR_SIZE; i++)
	    buffer->c.b[i] = 0;

    /* Zero out existing table */
    for (i = 0x1BE; i < SECTOR_SIZE; i++)
	buffer->c.b[i] = 0;

    fill_part_table(&(buffer->p.part[0]), pi);

    for (i = 0;
	 i < num_parts && pi->num != p_info[i].num - 1;
	 i++);

    if (i < num_parts) {
	p = &(buffer->p.part[1]);
	pi = &(p_info[i]);

	p->boot_ind = 0;
	p->sys_ind = DOS_EXTENDED;
	set_start_sect(p, pi->first_sector - ext_info.first_sector - ext_info.offset);
	set_nr_sects(p, pi->last_sector - pi->first_sector + 1);
	sects = ((pi->first_sector/(sectors*heads) > 1023) ?
		 heads*sectors*1024 - 1 : pi->first_sector);
	set_hsc(p->head, p->sector, p->cyl, sects);
	sects = ((pi->last_sector/(sectors*heads) > 1023) ?
		 heads*sectors*1024 - 1 : pi->last_sector);
	set_hsc(p->end_head, p->end_sector, p->end_cyl, sects);
    }

    buffer->p.magicflag[0] = PART_TABLE_FLAG0;
    buffer->p.magicflag[1] = PART_TABLE_FLAG1;
}

void write_part_table(void)
{
    int i, ct, done = FALSE, len;
    partition_table buffer;
    struct stat s;
    int is_bdev;
    char response[LINE_LENGTH];

    if (opentype == O_RDONLY) {
	 print_warning(READONLY_WARN);
	 refresh();
	 getch();
	 clear_warning();
	 return;
    }

    is_bdev = 0;
    if(fstat(fd, &s) == 0 && S_ISBLK(s.st_mode))
	 is_bdev = 1;

    if (is_bdev) {
	 print_warning(WRITE_WARN);

	 while (!done) {
	      mvaddstr(COMMAND_LINE_Y, COMMAND_LINE_X,
		       "Are you sure you want write the partition table "
		       "to disk? (yes or no): ");
	      len = get_string(response, LINE_LENGTH, NULL);
	      clear_warning();
	      if (len == GS_ESCAPE)
		   return;
	      else if (len == 2 &&
		       toupper(response[0]) == 'N' &&
		       toupper(response[1]) == 'O') {
		   print_warning(NO_WRITE);
		   return;
	      } else if (len == 3 &&
			 toupper(response[0]) == 'Y' &&
			 toupper(response[1]) == 'E' &&
			 toupper(response[2]) == 'S')
		   done = TRUE;
	      else
		   print_warning(YES_NO);
	 }

	 clear_warning();
	 print_warning(WRITING_PART);
	 refresh();
    }

    read_sector(buffer.c.b, 0);
    fill_primary_table(&buffer);
    write_sector(buffer.c.b, 0);

    for (i = 0; i < num_parts; i++)
	if (IS_LOGICAL(p_info[i].num)) {
	    read_sector(buffer.c.b, p_info[i].first_sector);
	    fill_logical_table(&buffer, &(p_info[i]));
	    write_sector(buffer.c.b, p_info[i].first_sector);
	}

    if (is_bdev) {
	 sync();
	 sleep(2);
	 if (!ioctl(fd,BLKRRPART))
	      changed = TRUE;
	 sync();
	 sleep(4);

	 clear_warning();
	 if (changed)
	      print_warning(YES_WRITE);
	 else
	      print_warning(RRPART_FAILED);
    } else
	 print_warning(YES_WRITE);

    /* Check: unique bootable primary partition? */
    ct = 0;
    for (i = 0; i < num_parts; i++)
	if (IS_PRIMARY(i) && p_info[i].flags == ACTIVE_FLAG)
	    ct++;
    if (ct != 1)
	print_warning(NOT_DOS_MBR_BOOTABLE);
}

void fp_printf(FILE *fp, char *format, ...)
{
    va_list args;
    char buf[1024];
    int y, x;

    va_start(args, format);
    vsprintf(buf, format, args);
    va_end(args);

    if (fp == NULL) {
	/* The following works best if the string to be printed has at
           most only one newline. */
	printw("%s", buf);
	getyx(stdscr, y, x);
	if (y >= COMMAND_LINE_Y-2) {
	    menuContinue();
	    erase();
	    move(0, 0);
	}
    } else
	fprintf(fp, "%s", buf);
}

#define MAX_PER_LINE 16
void print_file_buffer(FILE *fp, char *buffer)
{
    int i,l;

    for (i = 0, l = 0; i < SECTOR_SIZE; i++, l++) {
	if (l == 0)
	    fp_printf(fp, "0x%03X:", i);
	fp_printf(fp, " %02X", (unsigned char) buffer[i]);
	if (l == MAX_PER_LINE - 1) {
	    fp_printf(fp, "\n");
	    l = -1;
	}
    }
    if (l > 0)
	fp_printf(fp, "\n");
    fp_printf(fp, "\n");
}

void print_raw_table(void)
{
    int i, to_file;
    partition_table buffer;
    char fname[LINE_LENGTH];
    FILE *fp;

    if (print_only) {
	fp = stdout;
	to_file = TRUE;
    } else {
	mvaddstr(COMMAND_LINE_Y, COMMAND_LINE_X,
		 "Enter filename or press RETURN to display on screen: ");

	if ((to_file = get_string(fname, LINE_LENGTH, NULL)) < 0)
	    return;

	if (to_file) {
	    if ((fp = fopen(fname, "w")) == NULL) {
		char errstr[LINE_LENGTH];
		sprintf(errstr, PRINT_OPEN_ERR, fname);
		print_warning(errstr);
		return;
	    }
	} else {
	    fp = NULL;
	    erase();
	    move(0, 0);
	}
    }

    fp_printf(fp, "Disk Drive: %s\n", disk_device);

    fp_printf(fp, "Sector 0:\n");
    read_sector(buffer.c.b, 0);
    fill_primary_table(&buffer);
    print_file_buffer(fp, buffer.c.b);

    for (i = 0; i < num_parts; i++)
	if (IS_LOGICAL(p_info[i].num)) {
	    fp_printf(fp, "Sector %d:\n", p_info[i].first_sector);
	    read_sector(buffer.c.b, p_info[i].first_sector);
	    fill_logical_table(&buffer, &(p_info[i]));
	    print_file_buffer(fp, buffer.c.b);
	}

    if (to_file) {
	if (!print_only)
	    fclose(fp);
    } else {
        menuContinue();
    }
}

void print_p_info_entry(FILE *fp, partition_info *p)
{
    int size;
    char part_str[40];

    if (p->id == UNUSABLE)
	fp_printf(fp, "   None   ");
    else if (p->id == FREE_SPACE && p->num == PRI_OR_LOG)
	fp_printf(fp, "   Pri/Log");
    else if (p->id == FREE_SPACE && p->num == PRIMARY)
	fp_printf(fp, "   Primary");
    else if (p->id == FREE_SPACE && p->num == LOGICAL)
	fp_printf(fp, "   Logical");
    else
	fp_printf(fp, "%2d %-7.7s", p->num+1,
		  IS_LOGICAL(p->num) ? "Logical" : "Primary");

    fp_printf(fp, " ");

    fp_printf(fp, "%8d%c", p->first_sector,
	      ((p->first_sector/(sectors*heads)) !=
	       ((float)p->first_sector/(sectors*heads)) ?
	       '*' : ' '));

    fp_printf(fp, "%8d%c", p->last_sector,
	      (((p->last_sector+1)/(sectors*heads)) !=
	       ((float)(p->last_sector+1)/(sectors*heads)) ?
	       '*' : ' '));

    fp_printf(fp, "%7d%c", p->offset,
	      ((((p->first_sector == 0 || IS_LOGICAL(p->num)) &&
		 (p->offset != sectors)) ||
		(p->first_sector != 0 && IS_PRIMARY(p->num) &&
		 p->offset != 0)) ?
	       '#' : ' '));

    size = p->last_sector - p->first_sector + 1;
    fp_printf(fp, "%8d%c", size,
	      ((size/(sectors*heads)) != ((float)size/(sectors*heads)) ?
	       '*' : ' '));

    fp_printf(fp, " ");

    if (p->id == UNUSABLE)
	sprintf(part_str, "%.17s", "Unusable");
    else if (p->id == FREE_SPACE)
	sprintf(part_str, "%.17s", "Free Space");
    else if (partition_type[p->id])
	sprintf(part_str, "%.17s (%02X)", partition_type[p->id], p->id);
    else
	sprintf(part_str, "%.17s (%02X)", "Unknown", p->id);
    fp_printf(fp, "%-22.22s", part_str);

    fp_printf(fp, " ");

    if (p->flags == ACTIVE_FLAG)
	fp_printf(fp, "Boot (%02X)", p->flags);
    else if (p->flags != 0)
	fp_printf(fp, "Unknown (%02X)", p->flags);
    else
	fp_printf(fp, "None (%02X)", p->flags);

    fp_printf(fp, "\n");
}

void print_p_info(void)
{
    char fname[LINE_LENGTH];
    FILE *fp;
    int i, to_file, pext = is_extended(ext_info.id);

    if (print_only) {
	fp = stdout;
	to_file = TRUE;
    } else {
	mvaddstr(COMMAND_LINE_Y, COMMAND_LINE_X,
		 "Enter filename or press RETURN to display on screen: ");

	if ((to_file = get_string(fname, LINE_LENGTH, NULL)) < 0)
	    return;

	if (to_file) {
	    if ((fp = fopen(fname, "w")) == NULL) {
		char errstr[LINE_LENGTH];
		sprintf(errstr, PRINT_OPEN_ERR, fname);
		print_warning(errstr);
		return;
	    }
	} else {
	    fp = NULL;
	    erase();
	    move(0, 0);
	}
    }

    fp_printf(fp, "Partition Table for %s\n", disk_device);
    fp_printf(fp, "\n");
    fp_printf(fp, "            First    Last\n");
    fp_printf(fp, " # Type     Sector   Sector   Offset  Length   Filesystem Type (ID)   Flags\n");
    fp_printf(fp, "-- ------- -------- --------- ------ --------- ---------------------- ---------\n");

    for (i = 0; i < num_parts; i++) {
	if (pext && (p_info[i].first_sector >= ext_info.first_sector)) {
	    print_p_info_entry(fp,&ext_info);
	    pext = FALSE;
	}
	print_p_info_entry(fp, &(p_info[i]));
    }

    if (to_file) {
	if (!print_only)
	    fclose(fp);
    } else {
        menuContinue();
    }
}

void print_part_entry(FILE *fp, int num, partition_info *pi)
{
    int first = 0, start = 0, end = 0, size = 0;
    int ss = 0, sh = 0, sc = 0;
    int es = 0, eh = 0, ec = 0;
    int flags = 0, id = 0;

    if (pi != NULL) {
	flags = pi->flags;
	id = pi->id;

	if (IS_LOGICAL(num))
	    first = pi->offset;
	else
	    first = pi->first_sector + pi->offset;

	start = pi->first_sector + pi->offset;
	end = pi->last_sector;
	size = end - start + 1;
	if ((start/(sectors*heads)) > 1023)
	    start = heads*sectors*1024 - 1;
	if ((end/(sectors*heads)) > 1023)
	    end = heads*sectors*1024 - 1;

	ss = start % sectors + 1;
	start /= sectors;
	sh = start % heads;
	sc = start / heads;

	es = end % sectors + 1;
	end /= sectors;
	eh = end % heads;
	ec = end / heads;
    }

    fp_printf(fp, "%2d  0x%02X %4d %4d %4d 0x%02X %4d %4d %4d %8d %9d\n",
	      num+1, flags, sh, ss, sc, id, eh, es, ec, first, size);
}


void print_part_table(void)
{
    int i, j, to_file;
    char fname[LINE_LENGTH];
    FILE *fp;

    if (print_only) {
	fp = stdout;
	to_file = TRUE;
    } else {
	mvaddstr(COMMAND_LINE_Y, COMMAND_LINE_X,
		 "Enter filename or press RETURN to display on screen: ");

	if ((to_file = get_string(fname, LINE_LENGTH, NULL)) < 0)
	    return;

	if (to_file) {
	    if ((fp = fopen(fname, "w")) == NULL) {
		char errstr[LINE_LENGTH];
		sprintf(errstr, PRINT_OPEN_ERR, fname);
		print_warning(errstr);
		return;
	    }
	} else {
	    fp = NULL;
	    erase();
	    move(0, 0);
	}
    }

    fp_printf(fp, "Partition Table for %s\n", disk_device);
    fp_printf(fp, "\n");
    fp_printf(fp, "         ---Starting---      ----Ending----    Start Number of\n");
    fp_printf(fp, " # Flags Head Sect Cyl   ID  Head Sect Cyl    Sector  Sectors\n");
    fp_printf(fp, "-- ----- ---- ---- ---- ---- ---- ---- ---- -------- ---------\n");

    for (i = 0; i < 4; i++) {
	for (j = 0;
	     j < num_parts && (p_info[j].id <= 0 || p_info[j].num != i);
	     j++);
	if (j < num_parts) {
	    print_part_entry(fp, i, &(p_info[j]));
	} else if (is_extended(ext_info.id) && ext_info.num == i) {
	    print_part_entry(fp, i, &ext_info);
	} else {
	    print_part_entry(fp, i, NULL);
	}
    }

    for (i = 0; i < num_parts; i++)
	if (IS_LOGICAL(p_info[i].num))
	    print_part_entry(fp, p_info[i].num, &(p_info[i]));

    if (to_file) {
	if (!print_only)
	    fclose(fp);
    } else {
        menuContinue();
    }
}

void print_tables(void)
{
    int done = FALSE;

    static struct MenuItem menuFormat[]=
    {
        { 'r', "Raw", "Print the table using raw data format" },
        { 's', "Sectors", "Print the table ordered by sectors" },
        { 't', "Table", "Just print the partition table" },
        { ESC, "Cancel", "Don't print the table" },
        { 0, NULL, NULL }
    };
    
    while (!done)
	switch ( toupper(menuSimple( menuFormat, 2)) ) {
	case 'R':
	    print_raw_table();
	    done = TRUE;
	    break;
	case 'S':
	    print_p_info();
	    done = TRUE;
	    break;
	case 'T':
	    print_part_table();
	    done = TRUE;
	    break;
	case ESC:
	    done = TRUE;
	    break;
	}
}

#define END_OF_HELP "EOHS!"
#define NEW_HELP_SCREEN "SNHS!"
void display_help()
{
    char *help_text[] = {
	"Help Screen for cfdisk " VERSION,
	"",
	"This is cfdisk, a curses based disk partitioning programs, which",
	"allows you to create, delete and modify partitions on your hard",
	"disk drive.",
	"",
	"Copyright (C) 1994-1998 Kevin E. Martin & aeb",
	"",
	"Command      Meaning",
	"-------      -------",
	"  b          Toggle bootable flag of the current partition",
	"  d          Delete the current partition",
	"  g          Change cylinders, heads, sectors-per-track parameters",
	"             WARNING: This option should only be used by people who",
	"             know what they are doing.",
	"  h          Print this screen",
	"  m          Maximize disk usage of the current partition",
	"             Note: This may make the partition incompatible with",
	"             DOS, OS/2, ...",
	"  n          Create new partition from free space",
	"  p          Print partition table to the screen or to a file",
	"             There are several different formats for the partition",
	"             that you can choose from:",
	"                r - Raw data (exactly what would be written to disk)",
	"                s - Table ordered by sectors",
	"                t - Table in raw format",
	"  q          Quit program without writing partition table",
	"  t          Change the filesystem type",
	"  u          Change units of the partition size display",
	"             Rotates through Mb, sectors and cylinders",
	"  W          Write partition table to disk (must enter upper case W)",
        "             Since this might destroy data on the disk, you must",
	"             either confirm or deny the write by entering `yes' or",
	"             `no'",
	"Up Arrow     Move cursor to the previous partition",
	"Down Arrow   Move cursor to the next partition",
	"CTRL-L       Redraws the screen",
	"  ?          Print this screen",
	"",
	"Note: All of the commands can be entered with either upper or lower",
	"case letters (except for Writes).",
	END_OF_HELP
    };

    int cur_line = 0;
    FILE *fp = NULL;

    erase();
    move(0, 0);
    while (strcmp(help_text[cur_line], END_OF_HELP))
	if (!strcmp(help_text[cur_line], NEW_HELP_SCREEN)) {
	    menuContinue();
	    erase();
	    move(0, 0);
	    cur_line++;
	} else
	    fp_printf(fp, "%s\n", help_text[cur_line++]);

    menuContinue();
}

int change_geometry(void)
{
    int ret_val = FALSE;
    int done = FALSE;
    char def[LINE_LENGTH];
    char response[LINE_LENGTH];
    int tmp_val;

    while (!done) {
        static struct MenuItem menuGeometry[]=
        {
            { 'c', "Cylinders", "Change cylinder geometry" },
            { 'h', "Heads", "Change head geometry" },
            { 's', "Sectors", "Change sector geometry" },
            { 'd', "Done", "Done with changing geometry" },
            { 0, NULL, NULL }
        };
	move(COMMAND_LINE_Y, COMMAND_LINE_X);
	clrtoeol();
	refresh();

	clear_warning();

	switch (toupper( menuSimple(menuGeometry, 3) )) {
	case 'C':
	    sprintf(def, "%d", cylinders);
	    mvaddstr(COMMAND_LINE_Y, COMMAND_LINE_X,
		     "Enter the number of cylinders: ");
	    if (get_string(response, LINE_LENGTH, def) > 0) {
		tmp_val = atoi(response);
		if (tmp_val > 0 && tmp_val <= MAX_CYLINDERS) {
		    cylinders = tmp_val;
		    ret_val = TRUE;
		} else
		    print_warning(BAD_CYLINDERS);
	    }
	    break;
	case 'H':
	    sprintf(def, "%d", heads);
	    mvaddstr(COMMAND_LINE_Y, COMMAND_LINE_X,
		     "Enter the number of heads: ");
	    if (get_string(response, LINE_LENGTH, def) > 0) {
		tmp_val = atoi(response);
		if (tmp_val > 0 && tmp_val <= MAX_HEADS) {
		    heads = tmp_val;
		    ret_val = TRUE;
		} else
		    print_warning(BAD_HEADS);
	    }
	    break;
	case 'S':
	    sprintf(def, "%d", sectors);
	    mvaddstr(COMMAND_LINE_Y, COMMAND_LINE_X,
		     "Enter the number of sectors per track: ");
	    if (get_string(response, LINE_LENGTH, def) > 0) {
		tmp_val = atoi(response);
		if (tmp_val > 0 && tmp_val <= MAX_SECTORS) {
		    sectors = tmp_val;
		    ret_val = TRUE;
		} else
		    print_warning(BAD_SECTORS);
	    }
	    break;
	case ESC:
	case 'D':
	    done = TRUE;
	    break;
	default:
	    putchar(BELL);
	    break;
	}
    }

    if (ret_val) {
	int disk_end = heads*sectors*cylinders-1;

	if (p_info[num_parts-1].last_sector > disk_end) {
	    while (p_info[num_parts-1].first_sector > disk_end) {
		if (p_info[num_parts-1].id == FREE_SPACE ||
		    p_info[num_parts-1].id == UNUSABLE)
		    remove_part(num_parts-1);
		else
		    del_part(num_parts-1);
	    }

	    p_info[num_parts-1].last_sector = disk_end;

	    if (ext_info.last_sector > disk_end)
		ext_info.last_sector = disk_end;
	} else if (p_info[num_parts-1].last_sector < disk_end) {
	    if (p_info[num_parts-1].id == FREE_SPACE ||
		p_info[num_parts-1].id == UNUSABLE) {
		p_info[num_parts-1].last_sector = disk_end;
	    } else {
		insert_empty_part(num_parts,
				  p_info[num_parts-1].last_sector+1,
				  disk_end);
	    }
	}

	/* Make sure the partitions are correct */
	check_part_info();
    }

    return ret_val;
}

void change_id(int i)
{
    char id[LINE_LENGTH], def[LINE_LENGTH];
    int num_types = 0;
    int num_across, num_down;
    int len, new_id = ((p_info[i].id == LINUX) ? LINUX_SWAP : LINUX);
    int y_start, y_end;
    int j, pos;

    for (num_types = 0, j = 1; j < NUM_PART_TYPES; j++)
	if (partition_type[j])
	    num_types++;

    num_across = COLS/COL_ID_WIDTH;
    num_down = (((float)num_types)/num_across + 1);
    y_start = COMMAND_LINE_Y - 1 - num_down;
    if (y_start > DISK_TABLE_START+cur_part+4)
	y_start = DISK_TABLE_START+cur_part+4;
    y_end = y_start + num_down - 1;

    for (j = y_start - 1; j <= y_end + 1; j++) {
	move(j, 0);
	clrtoeol();
    }

    for (pos = 0, j = 1; j < NUM_PART_TYPES; j++)
	if (partition_type[j]) {
	    move(y_start + pos % num_down, (pos/num_down)*COL_ID_WIDTH + 1);
	    printw("%02X %-16.16s", j, partition_type[j]);
	    pos++;
	}

    sprintf(def, "%02X", new_id);
    mvaddstr(COMMAND_LINE_Y, COMMAND_LINE_X, "Enter filesystem type: ");
    if ((len = get_string(id, 2, def)) <= 0 && len != GS_DEFAULT)
	return;

    if (len != GS_DEFAULT) {
	if (!isxdigit(id[0]))
	    return;
	new_id = (isdigit(id[0]) ? id[0] - '0' : tolower(id[0]) - 'a' + 10);
	if (len == 2) {
	    if (isxdigit(id[1]))
		new_id = new_id*16 +
		    (isdigit(id[1]) ? id[1] - '0' : tolower(id[1]) - 'a' + 10);
	    else
		return;
	}
    }

    if (new_id == 0)
	print_warning(ID_EMPTY);
    else if (is_extended(new_id))
	print_warning(ID_EXT);
    else
	p_info[i].id = new_id;
}

void draw_partition(int i)
{
    int size, j;
    int y = i + DISK_TABLE_START + 2 - (cur_part/NUM_ON_SCREEN)*NUM_ON_SCREEN;
    char *t;

    if (!arrow_cursor) {
	move(y, 0);
	for (j = 0; j < COLS; j++)
	    addch(' ');
    }

    if (p_info[i].id > 0) {
	mvprintw(y, NAME_START,
		 "%s%d", my_basename(disk_device), p_info[i].num+1);
	if (p_info[i].flags) {
	    if (p_info[i].flags == ACTIVE_FLAG)
		mvaddstr(y, FLAGS_START, "Boot");
	    else
		mvprintw(y, FLAGS_START, "Unk(%02X)", p_info[i].flags);
	    if (p_info[i].first_sector == 0 || IS_LOGICAL(p_info[i].num)) {
		if (p_info[i].offset != sectors)
		    addstr(", NC");
	    } else {
		if (p_info[i].offset != 0)
		    addstr(", NC");
	    }
	} else {
	    if (p_info[i].first_sector == 0 || IS_LOGICAL(p_info[i].num)) {
		if (p_info[i].offset != sectors)
		    mvaddstr(y, FLAGS_START, "NC");
	    } else {
		if (p_info[i].offset != 0)
		    mvaddstr(y, FLAGS_START, "NC");
	    }
	}
    }
    mvaddstr(y, PTYPE_START,
	     (p_info[i].id == UNUSABLE ? "" :
	      (IS_LOGICAL(p_info[i].num) ? "Logical" :
	       (p_info[i].num >= 0 ? "Primary" :
		(p_info[i].num == PRI_OR_LOG ? "Pri/Log" :
		 (p_info[i].num == PRIMARY ? "Primary" : "Logical"))))));

    t = partition_type_text(i);
    if (t)
	 mvaddstr(y, FSTYPE_START, t);
    else
	 mvprintw(y, FSTYPE_START, "Unknown (%02X)", p_info[i].id);

    if (p_info[i].volume_label[0]) {
	int l = strlen(p_info[i].volume_label);
	int s = SIZE_START-5-l;
	mvprintw(y, (s > LABEL_START) ? LABEL_START : s,
		 " [%s]  ", p_info[i].volume_label);
    }

    size = p_info[i].last_sector - p_info[i].first_sector + 1;
    if (display_units == SECTORS)
	mvprintw(y, SIZE_START, "%9d", size);
    else if (display_units == CYLINDERS)
	mvprintw(y, SIZE_START, "%9d", size/(sectors*heads));
    else
	mvprintw(y, SIZE_START, "%9.2f", ceiling(size/20.48)/100);
    if (((size/(sectors*heads)) != ceiling(size/(sectors*(float)heads))) ||
	((p_info[i].first_sector/(sectors*heads)) !=
	 ceiling(p_info[i].first_sector/(sectors*heads))))
	mvprintw(y, COLUMNS-1, "*");
}

void init_const(void)
{
    if (!defined) {
	NAME_START = (((float)NAME_START)/COLUMNS)*COLS;
	FLAGS_START = (((float)FLAGS_START)/COLUMNS)*COLS;
	PTYPE_START = (((float)PTYPE_START)/COLUMNS)*COLS;
	FSTYPE_START = (((float)FSTYPE_START)/COLUMNS)*COLS;
	LABEL_START = (((float)LABEL_START)/COLUMNS)*COLS;
	SIZE_START = (((float)SIZE_START)/COLUMNS)*COLS;
	COMMAND_LINE_X = (((float)COMMAND_LINE_X)/COLUMNS)*COLS;

	COMMAND_LINE_Y = LINES - 4;
	WARNING_START = LINES - 2;

	if ((NUM_ON_SCREEN = COMMAND_LINE_Y - DISK_TABLE_START - 3) <= 0)
	    NUM_ON_SCREEN = 1;

	COLUMNS = COLS;
	defined = TRUE;
    }
}

void draw_screen(void)
{
    int i;
    char *line;

    line = (char *)malloc((COLS+1)*sizeof(char));

    if (warning_last_time) {
	for (i = 0; i < COLS; i++) {
	    move(WARNING_START, i);
	    line[i] = inch();
	}
	line[COLS] = 0;
    }

    erase();

    if (warning_last_time)
	mvaddstr(WARNING_START, 0, line);


    sprintf(line, "cfdisk %s", VERSION);
    mvaddstr(HEADER_START, (COLS-strlen(line))/2, line);
    sprintf(line, "Disk Drive: %s", disk_device);
    mvaddstr(HEADER_START+2, (COLS-strlen(line))/2, line);
    sprintf(line, "Heads: %d   Sectors per Track: %d   Cylinders: %d",
	    heads, sectors, cylinders);
    mvaddstr(HEADER_START+3, (COLS-strlen(line))/2, line);

    mvaddstr(DISK_TABLE_START, NAME_START, "Name");
    mvaddstr(DISK_TABLE_START, FLAGS_START, "Flags");
    mvaddstr(DISK_TABLE_START, PTYPE_START-1, "Part Type");
    mvaddstr(DISK_TABLE_START, FSTYPE_START, "FS Type");
    mvaddstr(DISK_TABLE_START, LABEL_START+1, "[Label]");
    if (display_units == SECTORS)
	mvaddstr(DISK_TABLE_START, SIZE_START, "  Sectors");
    else if (display_units == CYLINDERS)
	mvaddstr(DISK_TABLE_START, SIZE_START, "Cylinders");
    else
	mvaddstr(DISK_TABLE_START, SIZE_START, "Size (MB)");

    move(DISK_TABLE_START+1, 1);
    for (i = 1; i < COLS-1; i++)
	addch('-');

    if (NUM_ON_SCREEN >= num_parts)
	for (i = 0; i < num_parts; i++)
	    draw_partition(i);
    else
	for (i = (cur_part/NUM_ON_SCREEN)*NUM_ON_SCREEN;
	     i < NUM_ON_SCREEN + (cur_part/NUM_ON_SCREEN)*NUM_ON_SCREEN &&
	     i < num_parts;
	     i++)
	    draw_partition(i);

    free(line);
}

int draw_cursor(int move)
{
    if (move != 0 && (cur_part + move < 0 || cur_part + move >= num_parts))
	return -1;

    if (arrow_cursor)
	mvaddstr(DISK_TABLE_START + cur_part + 2
		 - (cur_part/NUM_ON_SCREEN)*NUM_ON_SCREEN, 0, "   ");
    else
	draw_partition(cur_part);

    cur_part += move;

    if (((cur_part - move)/NUM_ON_SCREEN)*NUM_ON_SCREEN !=
	(cur_part/NUM_ON_SCREEN)*NUM_ON_SCREEN)
	draw_screen();

    if (arrow_cursor)
	mvaddstr(DISK_TABLE_START + cur_part + 2
		 - (cur_part/NUM_ON_SCREEN)*NUM_ON_SCREEN, 0, "-->");
    else {
	standout();
	draw_partition(cur_part);
	standend();
    }

    return 0;
}

void do_curses_fdisk(void)
{
    int done = FALSE;
    char command;

    static struct MenuItem menuMain[]=
    {
        { 'b', "Bootable", "Toggle bootable flag of the current partition" },
        { 'd', "Delete", "Delete the current partition" },
        { 'g', "Geometry", "Change disk geometry (experts only)" },
        { 'h', "Help", "Print help screen" },
        { 'm', "Maximize", "Maximize disk usage of the current partition (experts only)" },
        { 'n', "New", "Create new partition from free space" },
        { 'p', "Print", "Print partition table to the screen or to a file" },
        { 'q', "Quit", "Quit program without writing partition table" },
        { 't', "Type", "Change the filesystem type (DOS, Linux, OS/2 and so on)" },
        { 'u', "Units", "Change units of the partition size display (MB, sect, cyl)" },
        { 'W', "Write", "Write partition table to disk (this might destroy data)" },
        { 0, NULL, NULL }
    };
    curses_started = 1;
    initscr();
    init_const();

    old_SIGINT = signal(SIGINT, die);
    old_SIGTERM = signal(SIGTERM, die);
#ifdef DEBUG
    signal(SIGINT, old_SIGINT);
    signal(SIGTERM, old_SIGTERM);
#endif

    cbreak();
    noecho();
    nonl();

    fill_p_info();

    draw_screen();

    while (!done) {
	char *s;

	(void)draw_cursor(0);

	if (p_info[cur_part].id == FREE_SPACE) {
	    s = ((opentype == O_RDWR) ? "hnpquW" : "hnpqu");
	    command = menuSelect(COMMAND_LINE_Y, COMMAND_LINE_X, menuMain, 8,
	        s, MENU_HORIZ | MENU_BUTTON | MENU_ACCEPT_OTHERS, 0);
	} else if (p_info[cur_part].id > 0) {
	    s = ((opentype == O_RDWR) ? "bdhmpqtuW" : "bdhmpqtu");
	    command = menuSelect(COMMAND_LINE_Y, COMMAND_LINE_X, menuMain, 8,
	        s, MENU_HORIZ | MENU_BUTTON | MENU_ACCEPT_OTHERS, 0);
	} else {
	    s = ((opentype == O_RDWR) ? "hpquW" : "hpqu");
	    command = menuSelect(COMMAND_LINE_Y, COMMAND_LINE_X, menuMain, 8,
	        s, MENU_HORIZ | MENU_BUTTON | MENU_ACCEPT_OTHERS, 0);
	}
	switch ( command ) {
	case 'B':
	case 'b':
	    if (p_info[cur_part].id > 0)
		p_info[cur_part].flags ^= 0x80;
	    else
		print_warning(NO_FLAGS);
	    break;
	case 'D':
	case 'd':
	    if (p_info[cur_part].id > 0) {
		del_part(cur_part);
		if (cur_part >= num_parts)
		    cur_part = num_parts - 1;
		draw_screen();
	    } else
		print_warning(DEL_EMPTY);
	    break;
	case 'G':
	case 'g':
	    if (change_geometry())
		draw_screen();
	    break;
	case 'M':
	case 'm':
	    if (p_info[cur_part].id > 0) {
		if (p_info[cur_part].first_sector == 0 ||
		    IS_LOGICAL(p_info[cur_part].num)) {
		    if (p_info[cur_part].offset == sectors)
			p_info[cur_part].offset = 1;
		    else
			p_info[cur_part].offset = sectors;
		    draw_screen();
		} else if (p_info[cur_part].offset != 0)
		    p_info[cur_part].offset = 0;
		else
		    print_warning(MAX_UNMAXABLE);
	    } else
		print_warning(MAX_UNMAXABLE);
	    break;
	case 'N':
	case 'n':
	    if (p_info[cur_part].id == FREE_SPACE) {
		new_part(cur_part);
		draw_screen();
	    } else if (p_info[cur_part].id == UNUSABLE)
		print_warning(ADD_UNUSABLE);
	    else
		print_warning(ADD_EXISTS);
	    break;
	case 'P':
	case 'p':
	    print_tables();
	    draw_screen();
	    break;
	case 'Q':
	case 'q':
	    done = TRUE;
	    break;
	case 'T':
	case 't':
	    if (p_info[cur_part].id > 0) {
		change_id(cur_part);
		draw_screen();
	    } else
		print_warning(TYPE_EMPTY);
	    break;
	case 'U':
	case 'u':
	    if (display_units == MEGABYTES)
		display_units = SECTORS;
	    else if (display_units == SECTORS)
		display_units = CYLINDERS;
	    else if (display_units == CYLINDERS)
		display_units = MEGABYTES;
	    draw_screen();
	    break;
	case 'W':
	    write_part_table();
	    break;
	case 'H':
	case 'h':
	case '?':
	    display_help();
	    draw_screen();
	    break;
	case MENU_UP : /* Up arrow */
	    if (!draw_cursor(-1))
		command = 0;
	    else
		print_warning(NO_MORE_PARTS);
	    break;
	case MENU_DOWN : /* Down arrow */
	    if (!draw_cursor(1))
		command = 0;
	    else
		print_warning(NO_MORE_PARTS);
	    break;
	case REDRAWKEY:
	    clear();
	    draw_screen();
	    break;
	default:
	    print_warning(BAD_COMMAND);
	    putchar(BELL); /* CTRL-G */
	}
    }

    die_x(0);
}

void copyright(void)
{
    fprintf(stderr, "Copyright (C) 1994-1997 Kevin E. Martin & aeb\n");
}

void usage(char *prog_name)
{
    fprintf(stderr, "\nUsage:\n");
    fprintf(stderr, "Print version:\n");
    fprintf(stderr, "\t%s -v\n", prog_name);
    fprintf(stderr, "Print partition table:\n");
    fprintf(stderr, "\t%s -P {r|s|t} [options] device\n", prog_name);
    fprintf(stderr, "Interactive use:\n");
    fprintf(stderr, "\t%s [options] device\n", prog_name);
    fprintf(stderr, "
Options:
-a: Use arrow instead of highlighting;
-z: Start with a zero partition table, instead of reading the pt from disk;
-c C -h H -s S: Override the kernel's idea of the number of cylinders,
                the number of heads and the number of sectors/track.\n\n");

    copyright();
}

int
main(int argc, char **argv)
{
    int c;
    int i, len;

    setlocale(LC_CTYPE, "");

    while ((c = getopt(argc, argv, "ac:h:s:vzP:")) != EOF)
	switch (c) {
	case 'a':
	    arrow_cursor = TRUE;
	    break;
	case 'c':
	    cylinders = atoi(optarg);
	    if (cylinders <= 0 || cylinders > MAX_CYLINDERS) {
		fprintf(stderr, "%s: %s\n", argv[0], BAD_CYLINDERS);
		exit(1);
	    }
	    break;
	case 'h':
	    heads = atoi(optarg);
	    if (heads <= 0 || heads > MAX_HEADS) {
		fprintf(stderr, "%s: %s\n", argv[0], BAD_HEADS);
		exit(1);
	    }
	    break;
	case 's':
	    sectors = atoi(optarg);
	    if (sectors <= 0 || sectors > MAX_SECTORS) {
		fprintf(stderr, "%s: %s\n", argv[0], BAD_SECTORS);
		exit(1);
	    }
	    break;
	case 'v':
	    fprintf(stderr, "cfdisk %s\n", VERSION);
	    copyright();
	    exit(0);
	case 'z':
	    zero_table = TRUE;
	    break;
	case 'P':
	    len = strlen(optarg);
	    for (i = 0; i < len; i++) {
		switch (optarg[i]) {
		case 'r':
		    print_only |= PRINT_RAW_TABLE;
		    break;
		case 's':
		    print_only |= PRINT_SECTOR_TABLE;
		    break;
		case 't':
		    print_only |= PRINT_PARTITION_TABLE;
		    break;
		default:
		    usage(argv[0]);
		    break;
		}
	    }
	    break;
	default:
	    usage(argv[0]);
	    exit(1);
	}

    if (argc-optind == 1)
	disk_device = argv[optind];
    else if (argc-optind != 0) {
	usage(argv[0]);
	exit(1);
    } else if ((fd = open(DEFAULT_DEVICE, O_RDONLY)) < 0)
	disk_device = ALTERNATE_DEVICE;
    else close(fd);

    if (print_only) {
	fill_p_info();
	if (print_only & PRINT_RAW_TABLE)
	    print_raw_table();
	if (print_only & PRINT_SECTOR_TABLE)
	    print_p_info();
	if (print_only & PRINT_PARTITION_TABLE)
	    print_part_table();
    } else
	do_curses_fdisk();

    return 0;
}
