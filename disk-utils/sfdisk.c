/*
 * sfdisk version 3.0 - aeb - 950813
 *
 * Copyright (C) 1995  Andries E. Brouwer (aeb@cwi.nl)
 *
 * This program is free software. You can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation: either Version 1
 * or (at your option) any later version.
 *
 * A.V. Le Blanc (LeBlanc@mcc.ac.uk) wrote Linux fdisk 1992-1994,
 * patched by various people (faith@cs.unc.edu, martin@cs.unc.edu,
 * leisner@sdsp.mc.xerox.com, esr@snark.thyrsus.com, aeb@cwi.nl)
 * 1993-1995, with version numbers (as far as I have seen) 0.93 - 2.0e.
 * This program had (head,sector,cylinder) as basic unit, and was
 * (therefore) broken in several ways for the use on larger disks -
 * for example, my last patch (from 2.0d to 2.0e) was required
 * to allow a partition to cross cylinder 8064, and to write an
 * extended partition past the 4GB mark.
 *
 * The current program is a rewrite from scratch, and I started a
 * version numbering at 3.0.
 * 	Andries Brouwer, aeb@cwi.nl, 950813
 *
 * Well, a good user interface is still lacking. On the other hand,
 * many configurations cannot be handled by any other fdisk.
 * I changed the name to sfdisk to prevent confusion. - aeb, 970501
 */

#define PROGNAME "sfdisk"
#define VERSION "3.07"
#define DATE "980518"

#include <stdio.h>
#include <stdlib.h>		/* atoi, free */
#include <stdarg.h>		/* varargs */
#include <unistd.h>		/* read, write */
#include <fcntl.h>		/* O_RDWR */
#include <errno.h>		/* ERANGE */
#include <string.h>		/* index() */
#include <ctype.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/unistd.h>	/* _syscall */
#include <linux/hdreg.h>	/* HDIO_GETGEO */
#include <linux/fs.h>		/* BLKGETSIZE */

#define SIZE(a)	(sizeof(a)/sizeof(a[0]))

/*
 * Table of contents:
 *  A. About seeking
 *  B. About sectors
 *  C. About heads, sectors and cylinders
 *  D. About system Ids
 *  E. About partitions
 *  F. The standard input
 *  G. The command line
 *  H. Listing the current situation
 *  I. Writing the new situation
 */
int exit_status = 0;

int force = 0;		/* 1: do what I say, even if it is stupid ... */
int quiet = 0;		/* 1: suppress all warnings */
int Linux = 0;		/* 1: suppress warnings irrelevant for Linux */
int DOS = 0;		/* 1: shift extended partitions by #sectors, not 1 */
int dump = 0;           /* 1: list in a format suitable for later input */
int verify = 0;         /* 1: check that listed partition is reasonable */
int no_write = 0;	/* 1: do not actually write to disk */
int no_reread = 0;	/* 1: skip the BLKRRPART ioctl test at startup */
int leave_last = 0;	/* 1: don't allocate the last cylinder */
int opt_list = 0;
char *save_sector_file = NULL;
char *restore_sector_file = NULL;

void
warn(char *s, ...) {
    va_list p;

    va_start(p, s);
    fflush(stdout);
    if (!quiet)
      vfprintf (stderr, s, p);
    va_end(p);
}

void
error(char *s, ...) {
    va_list p;

    va_start(p, s);
    fflush(stdout);
    fprintf(stderr, "\n" PROGNAME ": ");
    vfprintf(stderr, s, p);
    va_end(p);
}

void
fatal(char *s, ...) {
    va_list p;

    va_start(p, s);
    fflush(stdout);
    fprintf(stderr, "\n" PROGNAME ": ");
    vfprintf(stderr, s, p);
    va_end(p);
    exit(1);
}

/*
 *  A. About seeking
 */

/*
 * sseek: seek to specified sector - return 0 on failure
 *
 * For >4GB disks lseek needs a > 32bit arg, and we have to use llseek.
 * On the other hand, a 32 bit sector number is OK until 2TB.
 * The routines _llseek and sseek below are the only ones that
 * know about the loff_t type.
 */
#ifndef __alpha__
static
_syscall5(int,  _llseek,  uint,  fd, ulong, hi, ulong, lo,
       loff_t *, res, uint, wh);
#endif

int
sseek(char *dev, unsigned int fd, unsigned long s) {
    loff_t in, out;
    in = ((loff_t) s << 9);
    out = 1;

#ifndef __alpha__
    if (_llseek (fd, in>>32, in & 0xffffffff, &out, SEEK_SET) != 0) {
#else
	 if ((out = lseek(fd, in, SEEK_SET)) != in) {
#endif
	perror("llseek");
	error("seek error on %s - cannot seek to %lu\n", dev, s);
	return 0;
    }

    if (in != out) {
	error("seek error: wanted 0x%08x%08x, got 0x%08x%08x\n",
	       (uint)(in>>32), (uint)(in & 0xffffffff),
	       (uint)(out>>32), (uint)(out & 0xffffffff));
	return 0;
    }
    return 1;
}

/*
 *  B. About sectors
 */

/*
 * We preserve all sectors read in a chain - some of these will
 * have to be modified and written back.
 */
struct sector {
    struct sector *next;
    unsigned long sectornumber;
    int to_be_written;
    char data[512];
} *sectorhead;

void
free_sectors(void) {
    struct sector *s;

    while (sectorhead) {
	s = sectorhead;
	sectorhead = s->next;
	free(s);
    }
}

struct sector *
get_sector(char *dev, int fd, unsigned long sno) {
    struct sector *s;

    for(s = sectorhead; s; s = s->next)
	if(s->sectornumber == sno)
	    return s;

    if (!sseek(dev, fd, sno))
	return 0;

    if (!(s = (struct sector *) malloc(sizeof(struct sector))))
	fatal("out of memory - giving up\n");

    if (read(fd, s->data, sizeof(s->data)) != sizeof(s->data)) {
	perror("read");
	error("read error on %s - cannot read sector %lu\n", dev, sno);
	free(s);
	return 0;
    }

    s->next = sectorhead;
    sectorhead = s;
    s->sectornumber = sno;
    s->to_be_written = 0;

    return s;
}

int
msdos_signature (struct sector *s) {
    if (*(unsigned short *) (s->data + 0x1fe) != 0xaa55) {
	error("ERROR: sector %lu does not have an msdos signature\n",
	       s->sectornumber);
	return 0;
    }
    return 1;
}

int
write_sectors(char *dev, int fd) {
    struct sector *s;

    for (s = sectorhead; s; s = s->next)
	if (s->to_be_written) {
	    if (!sseek(dev, fd, s->sectornumber))
		return 0;
	    if (write(fd, s->data, sizeof(s->data)) != sizeof(s->data)) {
		perror("write");
		error("write error on %s - cannot write sector %lu\n",
		       dev, s->sectornumber);
		return 0;
	    }
	    s->to_be_written = 0;
	}
    return 1;
}

void
ulong_to_chars(unsigned long u, char *uu) {
    int i;

    for(i=0; i<4; i++) {
	uu[i] = (u & 0xff);
	u >>= 8;
    }
}

unsigned long
chars_to_ulong(unsigned char *uu) {
    int i;
    unsigned long u = 0;

    for(i=3; i>=0; i--)
	u = (u << 8) | uu[i];
    return u;
}

int
save_sectors(char *dev, int fdin) {
    struct sector *s;
    char ss[516];
    int fdout;

    fdout = open(save_sector_file, O_WRONLY | O_CREAT, 0444);
    if (fdout < 0) {
	perror(save_sector_file);
	error("cannot open partition sector save file (%s)\n",
	       save_sector_file);
	return 0;
    }

    for (s = sectorhead; s; s = s->next)
	if (s->to_be_written) {
	    ulong_to_chars(s->sectornumber, ss);
	    if (!sseek(dev, fdin, s->sectornumber))
		return 0;
	    if (read(fdin, ss+4, 512) != 512) {
		perror("read");
		error("read error on %s - cannot read sector %lu\n",
		       dev, s->sectornumber);
		return 0;
	    }
	    if (write(fdout, ss, sizeof(ss)) != sizeof(ss)) {
		perror("write");
		error("write error on %s\n", save_sector_file);
		return 0;
	    }
	}
    return 1;
}

void reread_disk_partition(char *dev, int fd);

int
restore_sectors(char *dev) {
    int fdin, fdout, ct;
    struct stat statbuf;
    char *ss0, *ss;
    unsigned long sno;

    if (stat(restore_sector_file, &statbuf) < 0) {
	perror(restore_sector_file);
	error("cannot stat partition restore file (%s)\n",
	       restore_sector_file);
	return 0;
    }
    if (statbuf.st_size % 516) {
	error("partition restore file has wrong size - not restoring\n");
	return 0;
    }
    if (!(ss = (char *) malloc(statbuf.st_size))) {
	error("out of memory?\n");
	return 0;
    }
    fdin = open(restore_sector_file, O_RDONLY);
    if (fdin < 0) {
	perror(restore_sector_file);
	error("cannot open partition restore file (%s)\n",
	       restore_sector_file);
	return 0;
    }
    if (read(fdin, ss, statbuf.st_size) != statbuf.st_size) {
	perror("read");
	error("error reading %s\n", restore_sector_file);
	return 0;
    }

    fdout = open(dev, O_WRONLY);
    if (fdout < 0) {
	perror(dev);
	error("cannot open device %s for writing\n", dev);
	return 0;
    }

    ss0 = ss;
    ct = statbuf.st_size/516;
    while(ct--) {
	sno = chars_to_ulong(ss);
	if (!sseek(dev, fdout, sno))
	  return 0;
	if (write(fdout, ss+4, 512) != 512) {
	    perror(dev);
	    error("error writing sector %lu on %s\n", sno, dev);
	    return 0;
	}
	ss += 516;
    }
    free(ss0);

    reread_disk_partition(dev, fdout);

    return 1;
}

/*
 *  C. About heads, sectors and cylinders
 */

/*
 * <linux/hdreg.h> defines HDIO_GETGEO and
 * struct hd_geometry {
 *      unsigned char heads;
 *      unsigned char sectors;
 *      unsigned short cylinders;
 *      unsigned long start;
 * };
 */

unsigned long cylindersize;
unsigned long heads, sectors, cylinders;
unsigned long specified_heads, specified_sectors, specified_cylinders;

void
get_cylindersize(char *dev, int fd, int silent) {
    struct hd_geometry g;
    int ioctl_ok = 0;

    heads = sectors = cylinders = 0;

    if (!ioctl(fd, HDIO_GETGEO, &g)) {
	ioctl_ok = 1;

	heads = g.heads;
	sectors = g.sectors;
	cylinders = g.cylinders;
    }

    if (specified_heads)
      heads = specified_heads;
    if (specified_sectors)
      sectors = specified_sectors;
    if (specified_cylinders)
      cylinders = specified_cylinders;

    cylindersize = heads * sectors;

    if (ioctl_ok) {
	if (g.start && !force) {
	    warn(
		 "Warning: start=%d - this looks like a partition rather than\n"
		 "the entire disk. Using fdisk on it is probably meaningless.\n"
		 "[Use the --force option if you really want this]\n", g.start);
	    exit(1);
	}
	if (heads != g.heads)
	  warn("Warning: HDIO_GETGEO says that there are %d heads\n",
	       g.heads);
	if (sectors != g.sectors)
	  warn("Warning: HDIO_GETGEO says that there are %d sectors\n",
	       g.sectors);
	if (cylinders != g.cylinders)
	  warn("Warning: HDIO_GETGEO says that there are %d cylinders\n",
	       g.cylinders);
    } else if (!silent)
      if (!heads || !sectors || !cylinders)
	printf("Disk %s: cannot get geometry\n", dev);
    if (sectors > 63)
      warn("Warning: unlikely number of sectors (%d) - usually at most 63\n"
	   "This will give problems with all software that uses C/H/S addressing.\n",
	   sectors);
    if (!silent)
      printf("\nDisk %s: %lu heads, %lu sectors, %lu cylinders\n",
	     dev, heads, sectors, cylinders);
}

typedef struct { unsigned char h,s,c; } chs; /* has some c bits in s */
chs zero_chs = { 0,0,0 };

typedef struct { unsigned long h,s,c; } longchs;
longchs zero_longchs;

chs
longchs_to_chs (longchs aa) {
    chs a;

    if (aa.h < 256 && aa.s < 64 && aa.c < 1024) {
	a.h = aa.h;
	a.s = aa.s | ((aa.c >> 2) & 0xc0);
	a.c = (aa.c & 0xff);
    } else if (heads && sectors) {
	a.h = heads - 1;
	a.s = sectors | 0xc0;
	a.c = 0xff;
    } else
      a = zero_chs;
    return a;
}

longchs
chs_to_longchs (chs a) {
    longchs aa;

    aa.h = a.h;
    aa.s = (a.s & 0x3f);
    aa.c = (a.s & 0xc0);
    aa.c = (aa.c << 2) + a.c;
    return aa;
}

longchs
ulong_to_longchs (unsigned long sno) {
    longchs aa;

    if (heads && sectors && cylindersize) {
	aa.s = 1 + sno % sectors;
	aa.h = (sno / sectors) % heads;
	aa.c = sno / cylindersize;
	return aa;
    } else {
	return zero_longchs;
    }
}

unsigned long
longchs_to_ulong (longchs aa) {
    return (aa.c*cylindersize + aa.h*sectors + aa.s - 1);
}

chs
ulong_to_chs (unsigned long sno) {
    return longchs_to_chs(ulong_to_longchs(sno));
}

unsigned long
chs_to_ulong (chs a) {
    return longchs_to_ulong(chs_to_longchs(a));
}

int
is_equal_chs (chs a, chs b) {
    return (a.h == b.h && a.s == b.s && a.c == b.c);
}

int
chs_ok (chs a, char *v, char *w) {
    longchs aa = chs_to_longchs(a);
    int ret = 1;

    if (is_equal_chs(a, zero_chs))
      return 1;
    if (heads && aa.h >= heads) {
	warn("%s of partition %s has impossible value for head: "
	     "%d (should be in 0-%d)\n", w, v, aa.h, heads-1);
	ret = 0;
    }
    if (sectors && (aa.s == 0 || aa.s > sectors)) {
	warn("%s of partition %s has impossible value for sector: "
	     "%d (should be in 1-%d)\n", w, v, aa.s, sectors);
	ret = 0;
    }
    if (cylinders && aa.c >= cylinders) {
	warn("%s of partition %s has impossible value for cylinders: "
	     "%d (should be in 0-%d)\n", w, v, aa.c, cylinders-1);
	ret = 0;
    }
    return ret;
}

/*
 *  D. About system Ids
 */

#define EMPTY_PARTITION		0
#define EXTENDED_PARTITION	5
#define WIN98_EXTENDED		0x0f
#define DM6_AUX1PARTITION	0x51
#define DM6_AUX3PARTITION	0x53
#define DM6_PARTITION		0x54
#define EZD_PARTITION		0x55
#define LINUX_SWAP              0x82
#define LINUX_NATIVE	        0x83
#define LINUX_EXTENDED		0x85
#define BSD_PARTITION		0xa5

/*
 * List of system Id's, adapted from fdisk 2.0d and <linux/genhd.h>
 * and SFS and several other sources.
 */
struct systypes {
    unsigned char type;
    char *name;
} sys_types[] = {
    {0, "Empty"},
    {1, "DOS 12-bit FAT"},		/* Primary DOS with 12-bit FAT */
    {2, "XENIX /"},			/* XENIX / filesystem */
    {3, "XENIX /usr"},			/* XENIX /usr filesystem */
    {4, "DOS 16-bit FAT <32M"},		/* Primary DOS with 16-bit FAT */
    {5, "DOS Extended"},		/* DOS 3.3+ extended partition */
    {6, "DOS 16-bit FAT >=32M"},
    {7, "HPFS / NTFS"},
    {8, "AIX boot or SplitDrive"},
    {9, "AIX data or Coherent"},
    {0x0a, "OS/2 Boot Manager"},
    {0x0b, "Win95 FAT32"},
    {0x0c, "Win95 FAT32 (LBA)"},
    {0x0e, "Win95 FAT16 (LBA)"},
    {0x0f, "Win95 Extended (LBA)"},
    {0x10, "OPUS"},
    {0x11, "Hidden DOS FAT12"},
    {0x12, "Compaq diagnostics"},
    {0x14, "Hidden DOS FAT16"},
    {0x16, "Hidden DOS FAT16 (big)"},
    {0x17, "Hidden HPFS/NTFS"},
    {0x18, "AST Windows swapfile"},
    {0x24, "NEC DOS"},
    {0x3c, "PartitionMagic recovery"},
    {0x40, "Venix 80286"},
    {0x41, "Linux/MINIX (sharing disk with DRDOS)"},
    {0x42, "SFS or Linux swap (sharing disk with DRDOS)"},
    {0x43, "Linux native (sharing disk with DRDOS)"},
    {0x50, "DM (disk manager)"},
    {0x51, "DM6 Aux1 (or Novell)"},
    {0x52, "CP/M or Microport SysV/AT"},
    {0x53, "DM6 Aux3"},
    {0x54, "DM6"},
    {0x55, "EZ-Drive (disk manager)"},
    {0x56, "Golden Bow (disk manager)"},
    {0x5c, "Priam Edisk (disk manager)"},
    {0x61, "SpeedStor"},
    {0x63, "GNU HURD or Mach or Sys V/386 (such as ISC UNIX)"},
    {0x64, "Novell Netware 286"},
    {0x65, "Novell Netware 386"},
    {0x70, "DiskSecure Multi-Boot"},
    {0x75, "PC/IX"},
    {0x77, "QNX4.x"},
    {0x78, "QNX4.x 2nd part"},
    {0x79, "QNX4.x 3rd part"},
    {0x80, "MINIX until 1.4a"},
    {0x81, "MINIX / old Linux"},
    {0x82, "Linux swap"},
    {0x83, "Linux native"},
    {0x84, "OS/2 hidden C: drive"},
    {0x85, "Linux extended"},
    {0x86, "NTFS volume set"},
    {0x87, "NTFS volume set"},
    {0x93, "Amoeba"},
    {0x94, "Amoeba BBT"},		/* (bad block table) */
    {0xa0, "IBM Thinkpad hibernation"}, /* according to dan@fch.wimsey.bc.ca */
    {0xa5, "BSD/386"},			/* 386BSD */
    {0xa6, "OpenBSD"},
    {0xa7, "NeXTSTEP 486"},
    {0xb7, "BSDI fs"},
    {0xb8, "BSDI swap"},
    {0xc1, "DRDOS/sec (FAT-12)"},
    {0xc4, "DRDOS/sec (FAT-16, < 32M)"},
    {0xc6, "DRDOS/sec (FAT-16, >= 32M)"},
    {0xc7, "Syrinx"},
    {0xdb, "CP/M or Concurrent CP/M or Concurrent DOS or CTOS"},
    {0xe1, "DOS access or SpeedStor 12-bit FAT extended partition"},
    {0xe3, "DOS R/O or SpeedStor"},
    {0xe4, "SpeedStor 16-bit FAT extended partition < 1024 cyl."},
    {0xf1, "SpeedStor"},
    {0xf2, "DOS 3.3+ secondary"},
    {0xf4, "SpeedStor large partition"},
    {0xfe, "SpeedStor >1024 cyl. or LANstep"},
    {0xff, "Xenix Bad Block Table"}
};


const char *
sysname(unsigned char type) {
    struct systypes *s;

    for (s = sys_types; s - sys_types < SIZE(sys_types); s++)
      if (s->type == type)
	return s->name;
    return "Unknown";
}

void
list_types(void) {
    struct systypes *s;

    printf("Id  Name\n\n");
    for (s = sys_types; s - sys_types < SIZE(sys_types); s++)
      printf("%2x  %s\n", s->type, s->name);
}

int
is_extended(unsigned char type) {
	return (type == EXTENDED_PARTITION
		|| type == LINUX_EXTENDED
		|| type == WIN98_EXTENDED);
}

int
is_bsd(unsigned char type) {
	return (type == BSD_PARTITION);
}

/*
 *  E. About partitions
 */

/* MS/DOS partition */

struct partition {
    unsigned char bootable;		/* 0 or 0x80 */
    chs begin_chs;
    unsigned char sys_type;
    chs end_chs;
    unsigned int start_sect;	/* starting sector counting from 0 */
    unsigned int nr_sects;		/* nr of sectors in partition */
};

/* Unfortunately, partitions are not aligned, and non-Intel machines
   are unhappy with non-aligned integers. So, we need a copy by hand. */
int
copy_to_int(unsigned char *cp) {
    unsigned int m;

    m = *cp++;
    m += (*cp++ << 8);
    m += (*cp++ << 16);
    m += (*cp++ << 24);
    return m;
}

void
copy_from_int(int m, char *cp) {
    *cp++ = (m & 0xff); m >>= 8;
    *cp++ = (m & 0xff); m >>= 8;
    *cp++ = (m & 0xff); m >>= 8;
    *cp++ = (m & 0xff);
}

void
copy_to_part(char *cp, struct partition *p) {
    p->bootable = *cp++;
    p->begin_chs.h = *cp++;
    p->begin_chs.s = *cp++;
    p->begin_chs.c = *cp++;
    p->sys_type = *cp++;
    p->end_chs.h = *cp++;
    p->end_chs.s = *cp++;
    p->end_chs.c = *cp++;
    p->start_sect = copy_to_int(cp);
    p->nr_sects = copy_to_int(cp+4);
}

void
copy_from_part(struct partition *p, char *cp) {
    *cp++ = p->bootable;
    *cp++ = p->begin_chs.h;
    *cp++ = p->begin_chs.s;
    *cp++ = p->begin_chs.c;
    *cp++ = p->sys_type;
    *cp++ = p->end_chs.h;
    *cp++ = p->end_chs.s;
    *cp++ = p->end_chs.c;
    copy_from_int(p->start_sect, cp);
    copy_from_int(p->nr_sects, cp+4);
}

/* Roughly speaking, Linux doesn't use any of the above fields except
   for partition type, start sector and number of sectors. (However,
   see also linux/drivers/scsi/fdomain.c.)
   The only way partition type is used (in the kernel) is the comparison
   for equality with EXTENDED_PARTITION (and these Disk Manager types). */

struct part_desc {
    unsigned long start;
    unsigned long size;
    unsigned long sector, offset; /* disk location of this info */
    struct partition p;
    struct part_desc *ep;	  /* extended partition containing this one */
    int ptype;
#define DOS_TYPE	0
#define BSD_TYPE	1
} zero_part_desc;

struct part_desc *
outer_extended_partition(struct part_desc *p) {
    while (p->ep)
      p = p->ep;
    return p;
}

int
is_parent(struct part_desc *pp, struct part_desc *p) {
    while (p) {
	if (pp == p)
	  return 1;
	p = p->ep;
    }
    return 0;
}

struct disk_desc {
    struct part_desc partitions[128];
    int partno;
} oldp, newp;

/* determine where on the disk this information goes */
void
add_sector_and_offset(struct disk_desc *z) {
    int pno;
    struct part_desc *p;

    for (pno = 0; pno < z->partno; pno++) {
	p = &(z->partitions[pno]);
	p->offset = 0x1be + (pno%4)*sizeof(struct partition);
	p->sector = (p->ep ? p->ep->start : 0);
    }
}

/* tell the kernel to reread the partition tables */
int reread_ioctl(int fd) {
    if(ioctl(fd, BLKRRPART)) {
	perror("BLKRRPART");
	return -1;
    }
    return 0;
}

/* reread after writing */
void
reread_disk_partition(char *dev, int fd) {
    printf("Re-reading the partition table ...\n");
    fflush(stdout);
    sync();
    sleep(3);			/* superfluous since 1.3.20 */

    if(reread_ioctl(fd))
      printf("The command to re-read the partition table failed\n"
	     "Reboot your system now, before using mkfs\n");

    if (close(fd)) {
	perror(dev);
	printf("Error closing %s\n", dev);
    }
    printf("\n");
}

/* find Linux name of this partition, assuming that it will have a name */
int
index_to_linux(int pno, struct disk_desc *z) {
    int i, ct = 1;
    struct part_desc *p = &(z->partitions[0]);
    for (i=0; i<pno; i++,p++)
	if(i < 4 || (p->size > 0 && !is_extended(p->p.sys_type)))
	  ct++;
    return ct;
}

int
linux_to_index(int lpno, struct disk_desc *z) {
    int i, ct = 0;
    struct part_desc *p = &(z->partitions[0]);
    for (i=0; i<z->partno && ct < lpno; i++,p++)
      if((i < 4 || (p->size > 0 && !is_extended(p->p.sys_type)))
	 && ++ct == lpno)
	return i;
    return -1;
}

int
asc_to_index(char *pnam, struct disk_desc *z) {
    int pnum, pno;

    if (*pnam == '#') {
	pno = atoi(pnam+1);
    } else {
	pnum = atoi(pnam);
	pno = linux_to_index(pnum, z);
    }
    if (!(pno >= 0 && pno < z->partno))
      fatal("%s: no such partition\n", pnam);
    return pno;
}

/*
 * List partitions - in terms of sectors, blocks or cylinders
 */
#define F_SECTOR   1
#define F_BLOCK    2
#define F_CYLINDER 3
#define F_MEGABYTE 4

int default_format = F_MEGABYTE;
int specified_format = 0;
int show_extended = 0;
int one_only = 0;
int one_only_pno;
int increment = 0;

void
set_format(char c) {
    switch(c) {
      default:
	printf("unrecognized format - using sectors\n");
      case 'S': specified_format = F_SECTOR; break;
      case 'B': specified_format = F_BLOCK; break;
      case 'C': specified_format = F_CYLINDER; break;
      case 'M': specified_format = F_MEGABYTE; break;
    }
}

unsigned long
unitsize(int format) {
    default_format = (cylindersize ? F_CYLINDER : F_MEGABYTE);
    if (!format && !(format = specified_format))
      format = default_format;

    switch(format) {
      default:
      case F_CYLINDER:
	if(cylindersize) return cylindersize;
      case F_SECTOR:
	return 1;
      case F_BLOCK:
	return 2;
      case F_MEGABYTE:
	return 2048;
    }
}

unsigned long
get_disksize(int format) {
    unsigned long cs = cylinders;
    if (cs && leave_last)
      cs--;
    return (cs * cylindersize) / unitsize(format);
}

void
out_partition_header(char *dev, int format) {
    if (dump) {
	printf("# partition table of %s\n", dev);
	printf("unit: sectors\n\n");
	return;
    }

    default_format = (cylindersize ? F_CYLINDER : F_MEGABYTE);
    if (!format && !(format = specified_format))
      format = default_format;

    switch(format) {
      default:
	printf("unimplemented format - using %s\n",
	       cylindersize ? "cylinders" : "sectors");
      case F_CYLINDER:
	if (cylindersize) {
	  printf("Units = cylinders of %lu bytes, blocks of 1024 bytes"
		 ", counting from %d\n\n",
		 cylindersize<<9, increment);
	    printf("   Device Boot Start     End   #cyls   #blocks   Id  System\n");
	    break;
	}
	/* fall through */
      case F_SECTOR:
	printf("Units = sectors of 512 bytes, counting from %d\n\n",
	       increment);
	printf("   Device Boot    Start       End  #sectors  Id  System\n");
	break;
      case F_BLOCK:
	printf("Units = blocks of 1024 bytes, counting from %d\n\n",
	       increment);
	printf("   Device Boot   Start       End   #blocks   Id  System\n");
	break;
      case F_MEGABYTE:
	printf("Units = megabytes of 1048576 bytes, blocks of 1024 bytes"
	       ", counting from %d\n\n", increment);
	printf("   Device Boot Start   End     MB   #blocks   Id  System\n");
	break;
    }
}

static void
out_rounddown(int width, unsigned long n, unsigned long unit, int inc) {
    printf("%*lu", width, inc + n/unit);
    if (unit != 1)
      putchar((n % unit) ? '+' : ' ');
    putchar(' ');
}

static void
out_roundup(int width, unsigned long n, unsigned long unit, int inc) {
    if (n == (unsigned long)(-1))
      printf("%*s", width, "-");
    else
      printf("%*lu", width, inc + n/unit);
    if (unit != 1)
      putchar(((n+1) % unit) ? '-' : ' ');
    putchar(' ');
}

static void
out_roundup_size(int width, unsigned long n, unsigned long unit) {
    printf("%*lu", width, (n+unit-1)/unit);
    if (unit != 1)
      putchar((n % unit) ? '-' : ' ');
    putchar(' ');
}

void
out_partition(char *dev, int format, struct part_desc *p, struct disk_desc *z) {
    unsigned long start, end, size;
    int pno, lpno;

    if (!format && !(format = specified_format))
      format = default_format;

    pno = p - &(z->partitions[0]); 	/* our index */
    lpno = index_to_linux(pno, z); 	/* name of next one that has a name */
    if(pno == linux_to_index(lpno, z))  /* was that us? */
      printf("%8s%-2u", dev, lpno);     /* yes */
    else if(show_extended)
      printf("    -     ");
    else
      return;
    putchar(dump ? ':' : ' ');

    start = p->start;
    end = p->start + p->size - 1;
    size = p->size;

    if (dump) {
	printf(" start=%9lu", start);
	printf(", size=%8lu", size);
	if (p->ptype == DOS_TYPE) {
	    printf(", Id=%2x", p->p.sys_type);
	    if (p->p.bootable == 0x80)
		printf(", bootable");
	}
	printf("\n");
	return;
    }

    if(p->ptype != DOS_TYPE || p->p.bootable == 0)
      printf("   ");
    else if(p->p.bootable == 0x80)
      printf(" * ");
    else
      printf(" ? ");		/* garbage */

    switch(format) {
      case F_CYLINDER:
	if (cylindersize) {
	    out_rounddown(6, start, cylindersize, increment);
	    out_roundup(6, end, cylindersize, increment);
	    out_roundup_size(6, size, cylindersize);
	    out_rounddown(8, size, 2, 0);
	    break;
	}
	/* fall through */
      default:
      case F_SECTOR:
	out_rounddown(9, start, 1, increment);
	out_roundup(9, end, 1, increment);
	out_rounddown(9, size, 1, 0);
	break;
      case F_BLOCK:
#if 0
	printf("%8lu,%3lu ",
	       p->sector/2, ((p->sector & 1) ? 512 : 0) + p->offset);
#endif
	out_rounddown(8, start, 2, increment);
	out_roundup(8, end, 2, increment);
	out_rounddown(8, size, 2, 0);
	break;
      case F_MEGABYTE:
	out_rounddown(5, start, 2048, increment);
	out_roundup(5, end, 2048, increment);
	out_roundup_size(5, size, 2048);
	out_rounddown(8, size, 2, 0);
	break;
    }
    if (p->ptype == DOS_TYPE) {
        printf(" %2x  %s\n",
	   p->p.sys_type, sysname(p->p.sys_type));
    } else {
	printf("\n");
    }

    /* Is chs as we expect? */
    if (!quiet && p->ptype == DOS_TYPE) {
	chs a, b;
	longchs aa, bb;
	a = (size ? ulong_to_chs(start) : zero_chs);
	b = p->p.begin_chs;
	aa = chs_to_longchs(a);
	bb = chs_to_longchs(b);
	if(a.s && !is_equal_chs(a, b))
	  printf("\t\tstart: (c,h,s) expected (%ld,%ld,%ld) found (%ld,%ld,%ld)\n",
		 aa.c, aa.h, aa.s, bb.c, bb.h, bb.s);
	a = (size ? ulong_to_chs(end) : zero_chs);
	b = p->p.end_chs;
	aa = chs_to_longchs(a);
	bb = chs_to_longchs(b);
	if(a.s && !is_equal_chs(a, b))
	  printf("\t\tend: (c,h,s) expected (%ld,%ld,%ld) found (%ld,%ld,%ld)\n",
		 aa.c, aa.h, aa.s, bb.c, bb.h, bb.s);
	if(cylinders && cylinders < 1024 && bb.c > cylinders)
	  printf("partition ends on cylinder %ld, beyond the end of the disk\n",
	       bb.c);
    }
}

void
out_partitions(char *dev, struct disk_desc *z) {
    int pno, format = 0;

    if (z->partno == 0)
	printf("No partitions found\n");
    else {
	out_partition_header(dev, format);
	for(pno=0; pno < z->partno; pno++) {
	    out_partition(dev, format, &(z->partitions[pno]), z);
	    if(show_extended && pno%4==3)
	      printf("\n");
	}
    }
}

static int
disj(struct part_desc *p, struct part_desc *q) {
    return
      ((p->start + p->size <= q->start)
	|| (is_extended(p->p.sys_type)
	    && q->start + q->size <= p->start + p->size));
}

char *
pnumber(struct part_desc *p, struct disk_desc *z) {
    static char buf[20];
    int this, next;
    struct part_desc *p0 = &(z->partitions[0]);

    this = index_to_linux(p-p0, z);
    next = index_to_linux(p-p0+1, z);

    if (next > this)
      sprintf(buf, "%d", this);
    else
      sprintf(buf, "[%d]", this);
    return buf;
}

int
partitions_ok(struct disk_desc *z) {
    struct part_desc *partitions = &(z->partitions[0]), *p, *q;
    int partno = z->partno;

#define PNO(p) pnumber(p, z)

    /* Have at least 4 partitions been defined? */
    if (partno < 4) {
         if (!partno)
	      fatal("no partition table present.\n");
	 else
	      fatal("strange, only %d partitions defined.\n", partno);
	 return 0;
    }

    /* Are the partitions of size 0 marked empty?
       And do they have start = 0? And bootable = 0? */
    for (p = partitions; p - partitions < partno; p++)
      if (p->size == 0) {
	  if(p->p.sys_type != EMPTY_PARTITION)
	    warn("Warning: partition %s has size 0 but is not marked Empty\n",
		 PNO(p));
	  else if(p->p.bootable != 0)
	    warn("Warning: partition %s has size 0 and is bootable\n",
		 PNO(p));
	  else if(p->p.start_sect != 0)
	    warn("Warning: partition %s has size 0 and nonzero start\n",
		 PNO(p));
	  /* all this is probably harmless, no error return */
      }

    /* Are the logical partitions contained in their extended partitions? */
    for (p = partitions+4; p < partitions+partno; p++)
      if (p->ptype == DOS_TYPE)
      if (p->size && !is_extended(p->p.sys_type)) {
	  q = p->ep;
	  if (p->start < q->start || p->start + p->size > q->start + q->size) {
	      warn("Warning: partition %s ", PNO(p));
	      warn("is not contained in partition %s\n", PNO(q));
	      return 0;
	  }
      }

    /* Are the data partitions mutually disjoint? */
    for (p = partitions; p < partitions+partno; p++)
      if (p->size && !is_extended(p->p.sys_type))
	for (q = p+1; q < partitions+partno; q++)
	  if (q->size && !is_extended(q->p.sys_type))
	    if(!((p->start > q-> start) ? disj(q,p) : disj(p,q))) {
		warn("Warning: partitions %s ", PNO(p));
		warn("and %s overlap\n", PNO(q));
		return 0;
	    }

    /* Are the data partitions and the extended partition
       table sectors disjoint? */
    for (p = partitions; p < partitions+partno; p++)
      if (p->size && !is_extended(p->p.sys_type))
        for (q = partitions; q < partitions+partno; q++)
          if (is_extended(q->p.sys_type))
            if (p->start <= q->start && p->start + p->size > q->start) {
                warn("Warning: partition %s contains part of ", PNO(p));
		warn("the partition table (sector %lu),\n", q->start);
		warn("and will destroy it when filled\n");
		return 0;
	    }

    /* Do they start past zero and end before end-of-disk? */
    { unsigned long ds = get_disksize(F_SECTOR);
    for (p = partitions; p < partitions+partno; p++)
      if (p->size) {
	  if(p->start == 0) {
	      warn("Warning: partition %s starts at sector 0\n", PNO(p));
	      return 0;
          }
          if (p->size && p->start + p->size > ds) {
	      warn("Warning: partition %s extends past end of disk\n", PNO(p));
	      return 0;
          }
      }
    }

    /* At most one chain of DOS extended partitions ? */
    /* It seems that the OS/2 fdisk has the additional requirement
       that the extended partition must be the fourth one */
    { int ect = 0;
      for (p = partitions; p < partitions+4; p++)
	if (p->p.sys_type == EXTENDED_PARTITION)
	  ect++;
      if (ect > 1 && !Linux) {
	  warn("Among the primary partitions, at most one can be extended\n");
	  warn(" (although this is not a problem under Linux)\n");
	  return 0;
      }
    }

    /*
     * Do all partitions start at a cylinder boundary ?
     * (this is not required for Linux)
     * The first partition starts after MBR.
     * Logical partitions start slightly after the containing extended partn.
     */
    if (cylindersize) {
	for(p = partitions; p < partitions+partno; p++)
	  if (p->size) {
	      if(p->start % cylindersize != 0
		 && (!p->ep || p->start / cylindersize != p->ep->start / cylindersize)
		 && (p->p.start_sect >= cylindersize)) {
		  warn("Warning: partition %s does not start "
		       "at a cylinder boundary\n", PNO(p));
		  if (!Linux)
		    return 0;
	      }
	      if((p->start + p->size) % cylindersize) {
		  warn("Warning: partition %s does not end "
		       "at a cylinder boundary\n", PNO(p));
		  if (!Linux)
		    return 0;
	      }
	  }
    }

    /* Usually, one can boot only from primary partitions. */
    /* In fact, from a unique one only. */
    /* do not warn about bootable extended partitions -
       often LILO is there */
    { int pno = -1;
    for(p = partitions; p < partitions+partno; p++)
      if (p->p.bootable) {
	  if (pno == -1)
	    pno = p - partitions;
	  else if (p - partitions < 4) {
	      warn("Warning: more than one primary partition is marked "
		   "bootable (active)\n"
		   "This does not matter for LILO, but the DOS MBR will "
		   "not boot this disk.\n");
	      break;
          }
	  if (p - partitions >= 4) {
	      warn("Warning: usually one can boot from primary partitions "
		   "only\n" "LILO disregards the `bootable' flag.\n");
	      break;
          }
      }
      if (pno == -1 || pno >= 4)
	warn("Warning: no primary partition is marked bootable (active)\n"
	     "This does not matter for LILO, but the DOS MBR will "
	     "not boot this disk.\n");
    }

    /* Is chs as we expect? */
    for(p = partitions; p < partitions+partno; p++)
      if(p->ptype == DOS_TYPE) {
	chs a, b;
	longchs aa, bb;
	a = p->size ? ulong_to_chs(p->start) : zero_chs;
	b = p->p.begin_chs;
	aa = chs_to_longchs(a);
	bb = chs_to_longchs(b);
	if (!chs_ok(b, PNO(p), "start"))
	  return 0;
	if(a.s && !is_equal_chs(a, b))
	  warn("partition %s: start: (c,h,s) expected (%ld,%ld,%ld) found (%ld,%ld,%ld)\n",
	       PNO(p), aa.c, aa.h, aa.s, bb.c, bb.h, bb.s);
	a = p->size ? ulong_to_chs(p->start + p->size - 1) : zero_chs;
	b = p->p.end_chs;
	aa = chs_to_longchs(a);
	bb = chs_to_longchs(b);
	if (!chs_ok(b, PNO(p), "end"))
	  return 0;
	if(a.s && !is_equal_chs(a, b))
	  warn("partition %s: end: (c,h,s) expected (%ld,%ld,%ld) found (%ld,%ld,%ld)\n",
	       PNO(p), aa.c, aa.h, aa.s, bb.c, bb.h, bb.s);
	if(cylinders && cylinders < 1024 && bb.c > cylinders)
	  warn("partition %s ends on cylinder %ld, beyond the end of the disk\n",
	       PNO(p), bb.c);
    }

    return 1;

#undef PNO
}

static void
extended_partition(char *dev, int fd, struct part_desc *ep, struct disk_desc *z) {
    char *cp;
    struct sector *s;
    unsigned long start, here, next;
    int i, moretodo = 1;
    struct partition p;
    struct part_desc *partitions = &(z->partitions[0]);
    int pno = z->partno;

    here = start = ep->start;

    while (moretodo) {
	moretodo = 0;

	if (!(s = get_sector(dev, fd, here)))
	    break;

	if (!msdos_signature(s))
	    break;

	cp = s->data + 0x1be;

	if (pno+4 >= SIZE(z->partitions)) {
	    printf("too many partitions - ignoring those past nr (%d)\n",
		   pno-1);
	    break;
	}

	next = 0;

	for (i=0; i<4; i++,cp += sizeof(struct partition)) {
	    partitions[pno].sector = here;
	    partitions[pno].offset = cp - s->data;
	    partitions[pno].ep = ep;
	    copy_to_part(cp,&p);
	    if (is_extended(p.sys_type)) {
		if (next)
		  printf("tree of partitions?\n");
		partitions[pno].start = next = start + p.start_sect;
		moretodo = 1;
	    } else {
		partitions[pno].start = here + p.start_sect;
	    }
	    partitions[pno].size = p.nr_sects;
	    partitions[pno].ptype = DOS_TYPE;
	    partitions[pno].p = p;
	    pno++;
        }
	here = next;
    }

    z->partno = pno;
}

#define BSD_DISKMAGIC   (0x82564557UL)
#define BSD_MAXPARTITIONS       8
#define BSD_FS_UNUSED           0
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
struct bsd_disklabel {
	u32	d_magic;
	char	d_junk1[4];
        char    d_typename[16];
        char    d_packname[16];
	char	d_junk2[92];
        u32	d_magic2;
	char	d_junk3[2];
        u16	d_npartitions;          /* number of partitions in following */
	char	d_junk4[8];
     struct  bsd_partition {         /* the partition table */
                u32   p_size;         /* number of sectors in partition */
                u32   p_offset;       /* starting sector */
                u32   p_fsize;        /* filesystem basic fragment size */
                u8    p_fstype;       /* filesystem type, see below */
                u8    p_frag;         /* filesystem fragments per block */
                u16   p_cpg;          /* filesystem cylinders per group */
     } d_partitions[BSD_MAXPARTITIONS];      /* actually may be more */
};

static void
bsd_partition(char *dev, int fd, struct part_desc *ep, struct disk_desc *z) {
	struct bsd_disklabel *l;
	struct bsd_partition *bp, *bp0;
	unsigned long start = ep->start;
	struct sector *s;
	struct part_desc *partitions = &(z->partitions[0]);
	int pno = z->partno;

	if (!(s = get_sector(dev,fd,start+1)))
		return;
	l = (struct bsd_disklabel *) (s->data);
	if (l->d_magic != BSD_DISKMAGIC)
		return;

	bp = bp0 = &l->d_partitions[0];
	while (bp - bp0 <= BSD_MAXPARTITIONS) {
		if (pno+1 >= SIZE(z->partitions)) {
			printf("too many partitions - ignoring those "
			       "past nr (%d)\n", pno-1);
			break;
		}
		if (bp->p_fstype != BSD_FS_UNUSED) {
			partitions[pno].start = bp->p_offset;
			partitions[pno].size = bp->p_size;
			partitions[pno].sector = start+1;
			partitions[pno].offset = (char *)bp - (char *)bp0;
			partitions[pno].ep = 0;
			partitions[pno].ptype = BSD_TYPE;
			pno++;
		}
		bp++;
	}
	z->partno = pno;
}

static int
msdos_partition(char *dev, int fd, unsigned long start, struct disk_desc *z) {
    int i;
    char *cp;
    struct partition pt;
    struct sector *s;
    struct part_desc *partitions = &(z->partitions[0]);
    int pno = z->partno;

    if (!(s = get_sector(dev, fd, start)))
	return 0;

    if (!msdos_signature(s))
	return 0;

    cp = s->data + 0x1be;
    copy_to_part(cp,&pt);

    /* If I am not mistaken, recent kernels will hide this from us,
	   so we will never actually see traces of a Disk Manager */
    if (pt.sys_type == DM6_PARTITION
	|| pt.sys_type == EZD_PARTITION
	|| pt.sys_type == DM6_AUX1PARTITION
	|| pt.sys_type == DM6_AUX3PARTITION) {
	printf("detected Disk Manager - unable to handle that\n");
	return 0;
    }
    { unsigned int sig = *(unsigned short *)(s->data + 2);
      if (sig <= 0x1ae
	  && *(unsigned short *)(s->data + sig) == 0x55aa
	  && (1 & *(unsigned char *)(s->data + sig + 2))) {
	  printf("DM6 signature found - giving up\n");
	  return 0;
      }
    }

    for (pno=0; pno<4; pno++,cp += sizeof(struct partition)) {
	partitions[pno].sector = start;
	partitions[pno].offset = cp - s->data;
	copy_to_part(cp,&pt);
	partitions[pno].start = start + pt.start_sect;
	partitions[pno].size = pt.nr_sects;
	partitions[pno].ep = 0;
	partitions[pno].p = pt;
    }

    z->partno = pno;

    for (i=0; i<4; i++) {
	if (is_extended(partitions[i].p.sys_type)) {
	    if (!partitions[i].size) {
		printf("strange..., an extended partition of size 0?\n");
		continue;
	    }
	    extended_partition(dev, fd, &partitions[i], z);
	}
	if (is_bsd(partitions[i].p.sys_type)) {
	    if (!partitions[i].size) {
		printf("strange..., a BSD partition of size 0?\n");
		continue;
	    }
	    bsd_partition(dev, fd, &partitions[i], z);
	}
    }
    return 1;
}

static int
osf_partition(char *dev, int fd, unsigned long start, struct disk_desc *z) {
	return 0;
}

static int
sun_partition(char *dev, int fd, unsigned long start, struct disk_desc *z) {
	return 0;
}

static int
amiga_partition(char *dev, int fd, unsigned long start, struct disk_desc *z) {
	return 0;
}

void
get_partitions(char *dev, int fd, struct disk_desc *z) {
    z->partno = 0;

    if (!msdos_partition(dev, fd, 0, z)
	&& !osf_partition(dev, fd, 0, z)
	&& !sun_partition(dev, fd, 0, z)
	&& !amiga_partition(dev, fd, 0, z)) {
	printf(" %s: unrecognized partition\n", dev);
	return;
    }
}

int
write_partitions(char *dev, int fd, struct disk_desc *z) {
    struct sector *s;
    struct part_desc *partitions = &(z->partitions[0]), *p;
    int pno = z->partno;

    if (no_write) {
	printf("-n flag was given: Nothing changed\n");
	exit(0);
    }

    for (p = partitions; p < partitions+pno; p++) {
	s = get_sector(dev, fd, p->sector);
	if (!s) return 0;
	s->to_be_written = 1;
	copy_from_part(&(p->p), s->data + p->offset);
	*(unsigned short *)(&(s->data[0x1fe])) = 0xaa55;
    }
    if (save_sector_file) {
	if (!save_sectors(dev, fd)) {
	    fatal("Failed saving the old sectors - aborting\n");
	    return 0;
	}
    }
    if (!write_sectors(dev, fd)) {
	error("Failed writing the partition on %s\n", dev);
	return 0;
    }
    return 1;
}

/*
 *  F. The standard input
 */

/*
 * Input format:
 * <start> <size> <type> <bootable> <c,h,s> <c,h,s>
 * Fields are separated by whitespace or comma or semicolon possibly
 * followed by whitespace; initial and trailing whitespace is ignored.
 * Numbers can be octal, decimal or hexadecimal, decimal is default
 * The <c,h,s> parts can (and probably should) be omitted.
 * Bootable is specified as [*|-], with as default not-bootable.
 * Type is given in hex, without the 0x prefix, or is [E|S|L|X], where
 * L (LINUX_NATIVE (83)) is the default, S is LINUX_SWAP (82), and E
 * is EXTENDED_PARTITION (5), X is LINUX_EXTENDED (85).
 * The default value of start is the first nonassigned sector/cylinder/...
 * The default value of size is as much as possible (until next
 * partition or end-of-disk).
 * .: end of chain of extended partitions.
 *
 * On interactive input an empty line means: all defaults.
 * Otherwise empty lines are ignored.
 */

int eof, eob;

struct dumpfld {
    int fldno;
    char *fldname;
    int is_bool;
} dumpflds[] = {
    { 0, "start", 0 },
    { 1, "size", 0 },
    { 2, "Id", 0 },
    { 3, "bootable", 1 },
    { 4, "bh", 0 },
    { 5, "bs", 0 },
    { 6, "bc", 0 },
    { 7, "eh", 0 },
    { 8, "es", 0 },
    { 9, "ec", 0 }
};

/*
 * Read a line, split it into fields
 *
 * (some primitive handwork, but a more elaborate parser seems
 *  unnecessary)
 */
#define RD_EOF (-1)
#define RD_CMD (-2)

int
read_stdin(unsigned char **fields, unsigned char *line, int fieldssize, int linesize) {
    unsigned char *lp, *ip;
    int c, fno;

    /* boolean true and empty string at start */
    line[0] = '*';
    line[1] = 0;
    for (fno=0; fno < fieldssize; fno++)
      fields[fno] = line + 1;
    fno = 0;

    /* read a line from stdin */
    lp = fgets(line+2, linesize, stdin);
    if (lp == NULL) {
	eof = 1;
	return RD_EOF;
    }
    if (!(lp = index(lp, '\n')))
      fatal("long or incomplete input line - quitting\n");
    *lp = 0;

    /* remove comments, if any */
    if ((lp = index(line+2, '#')) != 0)
      *lp = 0;

    /* recognize a few commands - to be expanded */
    if (!strcmp(line+2, "unit: sectors")) {
	specified_format = F_SECTOR;
	return RD_CMD;
    }

    /* dump style? - then bad input is fatal */
    if ((ip = index(line+2, ':')) != 0) {
	struct dumpfld *d;

      nxtfld:
	    ip++;
	    while(isspace(*ip))
	      ip++;
	    if (*ip == 0)
	      return fno;
	    for(d = dumpflds; d-dumpflds < SIZE(dumpflds); d++) {
		if(!strncmp(ip, d->fldname, strlen(d->fldname))) {
		    ip += strlen(d->fldname);
		    while(isspace(*ip))
		      ip++;
		    if (d->is_bool)
			fields[d->fldno] = line;
		    else if (*ip == '=') {
			while(isspace(*++ip)) ;
			fields[d->fldno] = ip;
			while(isalnum(*ip)) 	/* 0x07FF */
			  ip++;
		    } else
		      fatal("input error: `=' expected after %s field\n",
			    d->fldname);
		    if (fno <= d->fldno)
		      fno = d->fldno + 1;
		    if(*ip == 0)
		      return fno;
		    if(*ip != ',' && *ip != ';')
		      fatal("input error: unexpected character %c after %s field\n",
			    *ip, d->fldname);
		    *ip = 0;
		    goto nxtfld;
		}
	    }
	    fatal("unrecognized input: %s\n", ip);
    }

    /* split line into fields */
    lp = ip = line+2;
    fields[fno++] = lp;
    while((c = *ip++) != 0) {
	if (!lp[-1] && (c == '\t' || c == ' '))
	  ;
	else if (c == '\t' || c == ' ' || c == ',' || c == ';') {
	    *lp++ = 0;
	    if (fno < fieldssize)
		fields[fno++] = lp;
	    continue;
	} else
	  *lp++ = c;
    }

    if (lp == fields[fno-1])
      fno--;
    return fno;
}

/* read a number, use default if absent */
int
get_ul(char *u, unsigned long *up, unsigned long def, int base) {
    char *nu;

    if (*u) {
	errno = 0;
	*up = strtoul(u, &nu, base);
	if (errno == ERANGE) {
	    printf("number too big\n");
	    return -1;
	}
	if (*nu) {
	    printf("trailing junk after number\n");
	    return -1;
	}
    } else
      *up = def;
    return 0;
}

/* There are two common ways to structure extended partitions:
   as nested boxes, and as a chain. Sometimes the partitions
   must be given in order. Sometimes all logical partitions
   must lie inside the outermost extended partition.
NESTED: every partition is contained in the surrounding partitions
   and is disjoint from all others.
CHAINED: every data partition is contained in the surrounding partitions
   and disjoint from all others, but extended partitions may lie outside
   (insofar as allowed by all_logicals_inside_outermost_extended).
ONESECTOR: all data partitions are mutually disjoint; extended partitions
   each use one sector only (except perhaps for the outermost one).
*/
int partitions_in_order = 0;
int all_logicals_inside_outermost_extended = 1;
enum { NESTED, CHAINED, ONESECTOR } boxes = NESTED;

/* find the default value for <start> - assuming entire units */
unsigned long
first_free(int pno, int is_extended, struct part_desc *ep, int format,
	   unsigned long mid, struct disk_desc *z) {
    unsigned long ff, fff;
    unsigned long unit = unitsize(format);
    struct part_desc *partitions = &(z->partitions[0]), *pp = 0;

    /* if containing ep undefined, look at its container */
    if (ep && ep->p.sys_type == EMPTY_PARTITION)
      ep = ep->ep;

    if (ep) {
	if (boxes == NESTED || (boxes == CHAINED && !is_extended))
	  pp = ep;
	else if (all_logicals_inside_outermost_extended)
	  pp = outer_extended_partition(ep);
    }
#if 0
    ff = pp ? (pp->start + unit - 1) / unit : 0;
#else
    /* rounding up wastes almost an entire cylinder - round down
       and leave it to compute_start_sect() to fix the difference */
    ff = pp ? pp->start / unit : 0;
#endif
    /* MBR and 1st sector of an extended partition are never free */
    if (unit == 1)
      ff++;

  again:
    for(pp = partitions; pp < partitions+pno; pp++) {
	if (!is_parent(pp, ep) && pp->size > 0) {
	    if ((partitions_in_order || pp->start / unit <= ff
		                     || (mid && pp->start / unit <= mid))
		&& (fff = (pp->start + pp->size + unit - 1) / unit) > ff) {
		ff = fff;
		goto again;
	    }
	}
    }

    return ff;
}

/* find the default value for <size> - assuming entire units */
unsigned long
max_length(int pno, int is_extended, struct part_desc *ep, int format,
	   unsigned long start, struct disk_desc *z) {
    unsigned long fu;
    unsigned long unit = unitsize(format);
    struct part_desc *partitions = &(z->partitions[0]), *pp = 0;

    /* if containing ep undefined, look at its container */
    if (ep && ep->p.sys_type == EMPTY_PARTITION)
      ep = ep->ep;

    if (ep) {
	if (boxes == NESTED || (boxes == CHAINED && !is_extended))
	  pp = ep;
	else if (all_logicals_inside_outermost_extended)
	  pp = outer_extended_partition(ep);
    }
    fu = pp ? (pp->start + pp->size) / unit : get_disksize(format);
	
    for(pp = partitions; pp < partitions+pno; pp++)
      if (!is_parent(pp, ep) && pp->size > 0
	  && pp->start / unit >= start && pp->start / unit < fu)
	fu = pp->start / unit;

    return (fu > start) ? fu - start : 0;
}

/* compute starting sector of a partition inside an extended one */
/* ep is 0 or points to surrounding extended partition */
int
compute_start_sect(struct part_desc *p, struct part_desc *ep) {
    unsigned long base;
    int inc = (DOS && sectors) ? sectors : 1;
    int delta;

    if (ep && p->start + p->size >= ep->start + 1)
      delta = p->start - ep->start - inc;
    else if (p->start == 0 && p->size > 0)
      delta = -inc;
    else
      delta = 0;
    if (delta < 0) {
	p->start -= delta;
	p->size += delta;
	if (is_extended(p->p.sys_type) && boxes == ONESECTOR)
	  p->size = inc;
	else if ((int)(p->size) <= 0) {
	    warn("no room for partition descriptor\n");
	    return 0;
	}
    }
    base = (!ep ? 0
	        : (is_extended(p->p.sys_type) ?
		   outer_extended_partition(ep) : ep)->start);
    p->ep = ep;
    if (p->p.sys_type == EMPTY_PARTITION && p->size == 0) {
        p->p.start_sect = 0;
	p->p.begin_chs = zero_chs;
	p->p.end_chs = zero_chs;
    } else {
        p->p.start_sect = p->start - base;
	p->p.begin_chs = ulong_to_chs(p->start);
	p->p.end_chs = ulong_to_chs(p->start + p->size - 1);
    }
    p->p.nr_sects = p->size;
    return 1;
}    

/* build the extended partition surrounding a given logical partition */
int
build_surrounding_extended(struct part_desc *p, struct part_desc *ep,
			   struct disk_desc *z) {
    int inc = (DOS && sectors) ? sectors : 1;
    int format = F_SECTOR;
    struct part_desc *p0 = &(z->partitions[0]), *eep = ep->ep;

    if (boxes == NESTED) {
	ep->start = first_free(ep-p0, 1, eep, format, p->start, z);
	ep->size = max_length(ep-p0, 1, eep, format, ep->start, z);
	if (ep->start > p->start || ep->start + ep->size < p->start + p->size) {
	    warn("cannot build surrounding extended partition\n");
	    return 0;
	}
    } else {
	ep->start = p->start;
	if(boxes == CHAINED)
	  ep->size = p->size;
	else
	  ep->size = inc;
    }

    ep->p.nr_sects = ep->size;
    ep->p.bootable = 0;
    ep->p.sys_type = EXTENDED_PARTITION;
    if (!compute_start_sect(ep, eep) || !compute_start_sect(p, ep)) {
	ep->p.sys_type = EMPTY_PARTITION;
	ep->size = 0;
	return 0;
    }

    return 1;
}

int
read_line(int pno, struct part_desc *ep, char *dev, int interactive,
	  struct disk_desc *z) {
    unsigned char line[1000];
    unsigned char *fields[11];
    int fno, pct = pno%4;
    struct part_desc p, *orig;
    unsigned long ff, ff1, ul, ml, ml1, def;
    int format, lpno, is_extd;

    if (eof || eob)
      return -1;

    lpno = index_to_linux(pno, z);

    if (interactive) {
	if (pct == 0 && (show_extended || pno == 0))
	  warn("\n");
	warn("%8s%d: ", dev, lpno);
    }

    /* read input line - skip blank lines when reading from a file */
    do {
	fno = read_stdin(fields, line, SIZE(fields), SIZE(line));
    } while(fno == RD_CMD || (fno == 0 && !interactive));
    if (fno == RD_EOF) {
	return -1;
    } else if (fno > 10 && *(fields[10]) != 0) {
	printf("too many input fields\n");
	return 0;
    }

    if (fno == 1 && !strcmp(fields[0], ".")) {
	eob = 1;
	return -1;
    }

    /* use specified format, but round to cylinders if F_MEGABYTE specified */
    format = 0;
    if (cylindersize && specified_format == F_MEGABYTE)
      format = F_CYLINDER;

    orig = (one_only ? &(oldp.partitions[pno]) : 0);

    p = zero_part_desc;
    p.ep = ep;

    /* first read the type - we need to know whether it is extended */
    /* stop reading when input blank (defaults) and all is full */
    is_extd = 0;
    if (fno == 0) {		/* empty line */
	if (orig && is_extended(orig->p.sys_type))
	  is_extd = 1;
	ff = first_free(pno, is_extd, ep, format, 0, z);
	ml = max_length(pno, is_extd, ep, format, ff, z);
	if (ml == 0 && is_extd == 0) {
	    is_extd = 1;
	    ff = first_free(pno, is_extd, ep, format, 0, z);
	    ml = max_length(pno, is_extd, ep, format, ff, z);
	}
	if (ml == 0 && pno >= 4) {
	    /* no free blocks left - don't read any further */
	    warn("No room for more\n");
	    return -1;
	}
    }
    if (fno < 3 || !*(fields[2]))
      ul = orig ? orig->p.sys_type :
	   (is_extd || (pno > 3 && pct == 1 && show_extended))
	     ? EXTENDED_PARTITION : LINUX_NATIVE;
    else if(!strcmp(fields[2], "L"))
      ul = LINUX_NATIVE;
    else if(!strcmp(fields[2], "S"))
      ul = LINUX_SWAP;
    else if(!strcmp(fields[2], "E"))
      ul = EXTENDED_PARTITION;
    else if(!strcmp(fields[2], "X"))
      ul = LINUX_EXTENDED;
    else if (get_ul(fields[2], &ul, LINUX_NATIVE, 16))
      return 0;
    if (ul > 255) {
	warn("Illegal type\n");
	return 0;
    }
    p.p.sys_type = ul;
    is_extd = is_extended(ul);

    /* find start */
    ff = first_free(pno, is_extd, ep, format, 0, z);
    ff1 = ff * unitsize(format);
    def = orig ? orig->start : (pno > 4 && pct > 1) ? 0 : ff1;
    if (fno < 1 || !*(fields[0]))
      p.start = def;
    else {
	if (get_ul(fields[0], &ul, def / unitsize(0), 0))
	  return 0;
	p.start = ul * unitsize(0);
	p.start -= (p.start % unitsize(format));
    }

    /* find length */
    ml = max_length(pno, is_extd, ep, format, p.start / unitsize(format), z);
    ml1 = ml * unitsize(format);
    def = orig ? orig->size : (pno > 4 && pct > 1) ? 0 : ml1;
    if (fno < 2 || !*(fields[1]))
      p.size = def;
    else {
	if (get_ul(fields[1], &ul, def / unitsize(0), 0))
	  return 0;
	p.size = ul * unitsize(0) + unitsize(format) - 1;
	p.size -= (p.size % unitsize(format));
    }
    if (p.size > ml1) {
	warn("Warning: exceeds max allowable size (%lu)\n", ml1 / unitsize(0));
	if (!force)
	  return 0;
    }
    if (p.size == 0 && pno >= 4 && (fno < 2 || !*(fields[1]))) {
	warn("Warning: empty partition\n");
	if (!force)
	  return 0;
    }
    p.p.nr_sects = p.size;

    if (p.size == 0 && !orig) {
	if(fno < 1 || !*(fields[0]))
	  p.start = 0;
	if(fno < 3 || !*(fields[2]))
	  p.p.sys_type = EMPTY_PARTITION;
    }

    if (p.start < ff1 && p.size > 0) {
	warn("Warning: bad partition start (earliest %lu)\n",
	     (ff1 + unitsize(0) - 1) / unitsize(0));
	if (!force)
	  return 0;
    }

    if (fno < 4 || !*(fields[3]))
      ul = (orig ? orig->p.bootable : 0);
    else if (!strcmp(fields[3], "-"))
      ul = 0;
    else if (!strcmp(fields[3], "*") || !strcmp(fields[3], "+"))
      ul = 0x80;
    else {
    	warn("unrecognized bootable flag - choose - or *\n");
    	return 0;
    }
    p.p.bootable = ul;

    if (ep && ep->p.sys_type == EMPTY_PARTITION) {
      if(!build_surrounding_extended(&p, ep, z))
	return 0;
    } else
      if(!compute_start_sect(&p, ep))
	return 0;

    { longchs aa = chs_to_longchs(p.p.begin_chs), bb;

      if (fno < 5) {
	  bb = aa;
      } else if (fno < 7) {
	  warn("partial c,h,s specification?\n");
	  return 0;
      } else if(get_ul(fields[4], &bb.c, aa.c, 0) ||
		get_ul(fields[5], &bb.h, aa.h, 0) ||
		get_ul(fields[6], &bb.s, aa.s, 0))
	return 0;
      p.p.begin_chs = longchs_to_chs(bb);
    }
    { longchs aa = chs_to_longchs(p.p.end_chs), bb;

      if (fno < 8) {
	  bb = aa;
      } else if (fno < 10) {
	  warn("partial c,h,s specification?\n");
	  return 0;
      } else if(get_ul(fields[7], &bb.c, aa.c, 0) ||
		get_ul(fields[8], &bb.h, aa.h, 0) ||
		get_ul(fields[9], &bb.s, aa.s, 0))
	return 0;
      p.p.end_chs = longchs_to_chs(bb);
    }

    if (pno > 3 && p.size && show_extended && p.p.sys_type != EMPTY_PARTITION
	        && (is_extended(p.p.sys_type) != (pct == 1))) {
	warn("Extended partition not where expected\n");
	if (!force)
	  return 0;
    }

    z->partitions[pno] = p;
    if (pno >= z->partno)
      z->partno += 4;		/* reqd for out_partition() */

    if (interactive)
      out_partition(dev, 0, &(z->partitions[pno]), z);

    return 1;
}

/* ep either points to the extended partition to contain this one,
   or to the empty partition that may become extended or is 0 */
int
read_partition(char *dev, int interactive, int pno, struct part_desc *ep,
	       struct disk_desc *z) {
    struct part_desc *p = &(z->partitions[pno]);
    int i;

    if (one_only) {
	*p = oldp.partitions[pno];
	if (one_only_pno != pno)
	  goto ret;
    } else if (!show_extended && pno > 4 && pno%4)
	  goto ret;

    while (!(i = read_line(pno, ep, dev, interactive, z)))
      if (!interactive)
	fatal("bad input\n");
    if (i < 0) {
	p->ep = ep;
	return 0;
    }

  ret:
    p->ep = ep;
    if (pno >= z->partno)
      z->partno += 4;
    return 1;
}

void
read_partition_chain(char *dev, int interactive, struct part_desc *ep,
		     struct disk_desc *z) {
    int i, base;

    eob = 0;
    while (1) {
	base = z->partno;
	if (base+4 > SIZE(z->partitions)) {
	    printf("too many partitions\n");
	    break;
	}
	for (i=0; i<4; i++)
	  if (!read_partition(dev, interactive, base+i, ep, z))
	    return;
	for (i=0; i<4; i++) {
	    ep = &(z->partitions[base+i]);
	    if (is_extended(ep->p.sys_type) && ep->size)
	      break;
	}
	if (i == 4) {
	    /* nothing found - maybe an empty partition is going
	       to be extended */
	    if (one_only || show_extended)
	      break;
	    ep = &(z->partitions[base+1]);
	    if (ep->size || ep->p.sys_type != EMPTY_PARTITION)
	      break;
	}
    }
}

void
read_input(char *dev, int interactive, struct disk_desc *z) {
    int i;
    struct part_desc *partitions = &(z->partitions[0]), *ep;

    for (i=0; i < SIZE(z->partitions); i++)
      partitions[i] = zero_part_desc;
    z->partno = 0;

    if (interactive)
      warn("
Input in the following format; absent fields get a default value.
<start> <size> <type [E,S,L,X,hex]> <bootable [-,*]> <c,h,s> <c,h,s>
Usually you only need to specify <start> and <size> (and perhaps <type>).
");
    eof = 0;

    for (i=0; i<4; i++)
      read_partition(dev, interactive, i, 0, z);
    for (i=0; i<4; i++) {
	ep = partitions+i;
	if (is_extended(ep->p.sys_type) && ep->size)
	  read_partition_chain(dev, interactive, ep, z);
    }
    add_sector_and_offset(z);
}

/*
 *  G. The command line
 */

static void version(void) {
    printf(PROGNAME " version " VERSION " (aeb@cwi.nl, " DATE ")\n");
}

static void
usage(void) {
    version();
    printf("Usage:
	" PROGNAME " [options] device ...
device: something like /dev/hda or /dev/sda
useful options:
    -s [or --show-size]: list size of a partition
    -c [or --id]:        print or change partition Id
    -l [or --list]:      list partitions of each device
    -d [or --dump]:      idem, but in a format suitable for later input
    -i [or --increment]: number cylinders etc. from 1 instead of from 0
    -uS, -uB, -uC, -uM:  accept/report in units of sectors/blocks/cylinders/MB
    -T [or --list-types]:list the known partition types
    -D [or --DOS]:       for DOS-compatibility: waste a little space
    -R [or --re-read]:   make kernel reread partition table
    -N# :                change only the partition with number #
    -n :                 do not actually write to disk
    -O file :            save the sectors that will be overwritten to file
    -I file :            restore these sectors again
    -v [or --version]:   print version
    -? [or --help]:      print this message
dangerous options:
    -g [or --show-geometry]: print the kernel's idea of the geometry
    -x [or --show-extended]: also list extended partitions on output
                           or expect descriptors for them on input
    -L  [or --Linux]:      do not complain about things irrelevant for Linux
    -q  [or --quiet]:      suppress warning messages
    You can override the detected geometry using:
    -C# [or --cylinders #]:set the number of cylinders to use
    -H# [or --heads #]:    set the number of heads to use
    -S# [or --sectors #]:  set the number of sectors to use
    You can disable all consistency checking with:
    -f  [or --force]:      do what I say, even if it is stupid
");
    exit(1);
}

static void
activate_usage(char *progn) {
    printf("Usage:
  %s device	 	list active partitions on device
  %s device n1 n2 ...	activate partitions n1 ..., inactivate the rest
  %s device		activate partition n, inactivate the other ones
", progn, progn, PROGNAME " -An");
    exit(1);
}

static void
unhide_usage(char *progn) {
    exit(1);
}

static char short_opts[] = "cdfgilnqsu:vx?1A::C:DH:I:LN:O:RS:TU::V";

#define PRINT_ID 0400
#define CHANGE_ID 01000

static const struct option long_opts[] = {
    { "change-id",	  no_argument, NULL, 'c' + CHANGE_ID },
    { "print-id",	  no_argument, NULL, 'c' + PRINT_ID },
    { "id",		  no_argument, NULL, 'c' },
    { "dump",             no_argument, NULL, 'd' },
    { "force",            no_argument, NULL, 'f' },
    { "show-geometry",	  no_argument, NULL, 'g' },
    { "increment",        no_argument, NULL, 'i' },
    { "list",             no_argument, NULL, 'l' },
    { "quiet",            no_argument, NULL, 'q' },
    { "show-size",        no_argument, NULL, 's' },
    { "unit",       required_argument, NULL, 'u' },
    { "version",          no_argument, NULL, 'v' },
    { "show-extended",    no_argument, NULL, 'x' },
    { "help",	          no_argument, NULL, '?' },
    { "one-only",         no_argument, NULL, '1' },
    { "cylinders",  required_argument, NULL, 'C' },
    { "heads",      required_argument, NULL, 'H' },
    { "sectors",    required_argument, NULL, 'S' },
    { "activate",   optional_argument, NULL, 'A' },
    { "DOS",              no_argument, NULL, 'D' },
    { "Linux",            no_argument, NULL, 'L' },
    { "re-read",          no_argument, NULL, 'R' },
    { "list-types",       no_argument, NULL, 'T' },
    { "unhide",     optional_argument, NULL, 'U' },
    { "no-reread",        no_argument, NULL, 160 },
    { "IBM",              no_argument, NULL, 161 },
    { "leave-last",       no_argument, NULL, 161 },
/* undocumented flags - not all completely implemented */
    { "in-order",         no_argument, NULL, 128 },
    { "not-in-order",     no_argument, NULL, 129 },
    { "inside-outer",     no_argument, NULL, 130 },
    { "not-inside-outer", no_argument, NULL, 131 },
    { "nested",           no_argument, NULL, 132 },
    { "chained",          no_argument, NULL, 133 },
    { "onesector",        no_argument, NULL, 134 },
    { NULL, 0, NULL, 0 }
};

/* default devices to list */
static struct devd {
    char *pref, *letters;
} defdevs[] = {
    { "hd", "abcdefgh" },
    { "sd", "abcde" },
    { "xd", "ab" }
};

int
is_ide_cdrom(char *device) {
    /* No device was given explicitly, and we are trying some
       likely things.  But opening /dev/hdc may produce errors like
           "hdc: tray open or drive not ready"
       if it happens to be a CD-ROM drive. So try to be careful.
       This only works since 2.1.73. */

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

void do_list(char *dev, int silent);
void do_size(char *dev, int silent);
void do_geom(char *dev, int silent);
void do_fdisk(char *dev);
void do_reread(char *dev);
void do_change_id(char *dev, char *part, char *id);
void do_unhide(char **av, int ac, char *arg);
void do_activate(char **av, int ac, char *arg);

int total_size;

int
main(int argc, char **argv) {
    char *progn;
    int c;
    char *dev;
    int opt_size = 0;
    int opt_out_geom = 0;
    int opt_reread = 0;
    int activate = 0;
    int do_id = 0;
    int unhide = 0;
    int fdisk = 0;
    char *activatearg = 0;
    char *unhidearg = 0;

    if (argc < 1)
      fatal("no command?\n");
    if ((progn = rindex(argv[0], '/')) == NULL)
      progn = argv[0];
    else
      progn++;
    if (!strcmp(progn, "activate"))
      activate = 1;		/* equivalent to `fdisk -A' */
#if 0				/* not important enough to deserve a name */
    else if (!strcmp(progn, "unhide"))
      unhide = 1;		/* equivalent to `fdisk -U' */
#endif
    else
      fdisk = 1;

    while ((c = getopt_long (argc, argv, short_opts, long_opts, NULL)) != -1) {
	switch (c) {
	  case 'f':
	    force = 1; break;	/* does not imply quiet */
	  case 'g':
	    opt_out_geom = 1; break;
	  case 'i':
	    increment = 1; break;
	  case 'c':
	  case 'c' + PRINT_ID:
	  case 'c' + CHANGE_ID:
	    do_id = c; break;
	  case 'd':
	    dump = 1; /* fall through */
	  case 'l':
	    opt_list = 1; break;
	  case 'n':
	    no_write = 1; break;
	  case 'q':
	    quiet = 1; break;
	  case 's':
	    opt_size = 1; break;
	  case 'u':
	    set_format(*optarg); break;
	  case 'v':
	    version();
	    exit(0);
	  case 'x':
	    show_extended = 1; break;
	  case 'A':
	    activatearg = optarg;
	    activate = 1; break;
	  case 'C':
	    specified_cylinders = atoi(optarg); break;
	  case 'D':
	    DOS = 1; break;
	  case 'H':
	    specified_heads = atoi(optarg); break;
	  case 'L':
	    Linux = 1; break;
	  case 'N':
	    one_only = atoi(optarg); break;
	  case 'I':
	    restore_sector_file = optarg; break;
	  case 'O':
	    save_sector_file = optarg; break;
	  case 'R':
	    opt_reread = 1; break;
	  case 'S':
	    specified_sectors = atoi(optarg); break;
	  case 'T':
	    list_types();
	    exit(0);
	  case 'U':
	    unhidearg = optarg;
	    unhide = 1; break;
	  case 'V':
	    verify = 1; break;
	  case '?':
	  default:
	    usage(); break;

	  /* undocumented flags */
	  case 128:
	    partitions_in_order = 1; break;
	  case 129:
	    partitions_in_order = 0; break;
	  case 130:
	    all_logicals_inside_outermost_extended = 1; break;
	  case 131:
	    all_logicals_inside_outermost_extended = 0; break;
	  case 132:
	    boxes = NESTED; break;
	  case 133:
	    boxes = CHAINED; break;
	  case 134:
	    boxes = ONESECTOR; break;

	  /* more flags */
	  case 160:
	    no_reread = 1; break;
	  case 161:
	    leave_last = 1; break;
	}
    }

    if (optind == argc && (opt_list || opt_out_geom || opt_size || verify)) {
	struct devd *dp;
	char *lp;
	char device[10];

	total_size = 0;

	for(dp = defdevs; dp-defdevs < SIZE(defdevs); dp++) {
	    lp = dp->letters;
	    while(*lp) {
		sprintf(device, "/dev/%s%c", dp->pref, *lp++);
		if (!strcmp(dp->pref, "hd") && is_ide_cdrom(device))
		  continue;
		if (opt_out_geom)
		  do_geom(device, 1);
		if (opt_size)
		  do_size(device, 1);
		if (opt_list || verify)
		  do_list(device, 1);
	    }
	}

	if (opt_size)
	  printf("total: %d blocks\n", total_size);

	exit(exit_status);
    }

    if (optind == argc) {
	if (activate)
	  activate_usage(fdisk ? "fdisk -A" : progn);
	else if (unhide)
	  unhide_usage(fdisk ? "fdisk -U" : progn);
	else
	  usage();
    }

    if (opt_list || opt_out_geom || opt_size || verify) {
	while (optind < argc) {
	    if (opt_out_geom)
	      do_geom(argv[optind], 0);
	    if (opt_size)
	      do_size(argv[optind], 0);
	    if (opt_list || verify)
	      do_list(argv[optind], 0);
	    optind++;
	}
	exit(exit_status);
    }

    if (activate) {
	do_activate(argv+optind, argc-optind, activatearg);
	exit(exit_status);
    }
    if (unhide) {
	do_unhide(argv+optind, argc-optind, unhidearg);
	exit(exit_status);
    }
    if (do_id) {
        if ((do_id & PRINT_ID) != 0 && optind != argc-2)
	  fatal("usage: fdisk --print-id device partition-number\n");
	else if ((do_id & CHANGE_ID) != 0 && optind != argc-3)
	  fatal("usage: fdisk --change-id device partition-number Id\n");
	else if (optind != argc-3 && optind != argc-2)
	  fatal("usage: fdisk --id device partition-number [Id]\n");
	do_change_id(argv[optind], argv[optind+1],
		     (optind == argc-2) ? 0 : argv[optind+2]);
	exit(exit_status);
    }
      
    if (optind != argc-1)
      fatal("can specify only one device (except with -l or -s)\n");
    dev = argv[optind];

    if (opt_reread)
      do_reread(dev);
    else if (restore_sector_file)
      restore_sectors(dev);
    else
      do_fdisk(dev);

    return 0;
}

/*
 *  H. Listing the current situation
 */

int
my_open (char *dev, int rw, int silent) {
    int fd, mode;

    mode = (rw ? O_RDWR : O_RDONLY);
    fd = open(dev, mode);
    if (fd < 0 && !silent) {
	perror(dev);
	fatal("cannot open %s %s\n", dev, rw ? "read-write" : "for reading");
    }
    return fd;
}

void
do_list (char *dev, int silent) {
    int fd;
    struct disk_desc *z;

    fd = my_open(dev, 0, silent);
    if (fd < 0)
	return;

    z = &oldp;

    free_sectors();
    get_cylindersize(dev, fd, dump ? 1 : opt_list ? 0 : 1);
    get_partitions(dev, fd, z);

    if (opt_list)
      out_partitions(dev, z);

    if (verify) {
	if (partitions_ok(z))
	  warn("%s: OK\n", dev);
	else
	  exit_status = 1;
    }
}

void
do_geom (char *dev, int silent) {
    int fd;
    struct hd_geometry g;

    fd = my_open(dev, 0, silent);
    if (fd < 0)
	return;

    /* get_cylindersize(dev, fd, silent); */
    if (!ioctl(fd, HDIO_GETGEO, &g))
	printf("%s: %d cylinders, %d heads, %d sectors/track\n",
	       dev, g.cylinders, g.heads, g.sectors);
    else
	printf("%s: unknown geometry\n", dev);
}

/* for compatibility with earlier fdisk: provide option -s */
void
do_size (char *dev, int silent) {
    int fd, size;

    fd = my_open(dev, 0, silent);
    if (fd < 0)
	return;

    if(ioctl(fd, BLKGETSIZE, &size)) {
	if(!silent) {
	    perror(dev);
	    fatal("BLKGETSIZE ioctl failed for %s\n", dev);
	}
	return;
    }

    size /= 2;			/* convert sectors to blocks */
    if (silent)
      printf("%s: %9d\n", dev, size);
    else
      printf("%d\n", size);

    total_size += size;
}

/*
 * Activate: usually one wants to have a single primary partition
 * to be active. OS/2 fdisk makes non-bootable logical partitions
 * active - I don't know what that means to OS/2 Boot Manager.
 *
 * Call: activate /dev/hda 2 5 7	make these partitions active
 *					and the remaining ones inactive
 * Or:   fdisk -A /dev/hda 2 5 7
 *
 * If only a single partition must be active, one may also use the form
 *       fdisk -A2 /dev/hda
 *
 * With "activate /dev/hda" or "fdisk -A /dev/hda" the active partitions
 * are listed but not changed. To get zero active partitions, use
 * "activate /dev/hda none" or "fdisk -A /dev/hda none".
 * Use something like `echo ",,,*" | fdisk -N2 /dev/hda' to only make
 * /dev/hda2 active, without changing other partitions.
 *
 * A warning will be given if after the change not precisely one primary
 * partition is active.
 *
 * The present syntax was chosen to be (somewhat) compatible with the
 * activate from the LILO package.
 */
void
set_active (struct disk_desc *z, char *pnam) {
    int pno;

    pno = asc_to_index(pnam, z);
    z->partitions[pno].p.bootable = 0x80;
}

void
do_activate (char **av, int ac, char *arg) {
    char *dev = av[0];
    int fd;
    int rw, i, pno, lpno;
    struct disk_desc *z;

    z = &oldp;

    rw = (!no_write && (arg || ac > 1));
    fd = my_open(dev, rw, 0);

    free_sectors();
    get_cylindersize(dev, fd, 1);
    get_partitions(dev, fd, z);

    if (!arg && ac == 1) {
	/* list active partitions */
	for (pno=0; pno < z->partno; pno++) {
	    if (z->partitions[pno].p.bootable) {
		lpno = index_to_linux(pno, z);
		if (pno == linux_to_index(lpno, z))
		  printf("%s%d\n", dev, lpno);
		else
		  printf("%s#%d\n", dev, pno);
		if (z->partitions[pno].p.bootable != 0x80)
		  warn("bad active byte: 0x%x instead of 0x80\n",
		       z->partitions[pno].p.bootable);
	    }
	}
    } else {
	/* clear `active byte' everywhere */
	for (pno=0; pno < z->partno; pno++)
	    z->partitions[pno].p.bootable = 0;

	/* then set where desired */
	if (ac == 1)
	  set_active(z, arg);
	else for(i=1; i<ac; i++)
	  set_active(z, av[i]);

	/* then write to disk */
	if(write_partitions(dev, fd, z))
	  warn("Done\n\n");
	else
	  exit_status = 1;
    }
    i = 0;
    for (pno=0; pno < z->partno && pno < 4; pno++)
      if (z->partitions[pno].p.bootable)
	i++;
    if (i != 1)
      warn("You have %d active primary partitions. This does not matter for LILO,\n"
	   "but the DOS MBR will only boot a disk with 1 active partition.\n", i);
}

void
set_unhidden (struct disk_desc *z, char *pnam) {
    int pno;
    unsigned char id;

    pno = asc_to_index(pnam, z);
    id = z->partitions[pno].p.sys_type;
    if (id == 0x11 || id == 0x14 || id == 0x16 || id == 0x17)
      id -= 0x10;
    else
      fatal("partition %s has id %x and is not hidden\n", pnam, id);
    z->partitions[pno].p.sys_type = id;
}

/*
 * maybe remove and make part of --change-id
 */
void
do_unhide (char **av, int ac, char *arg) {
    char *dev = av[0];
    int fd, rw, i;
    struct disk_desc *z;

    z = &oldp;

    rw = !no_write;
    fd = my_open(dev, rw, 0);

    free_sectors();
    get_cylindersize(dev, fd, 1);
    get_partitions(dev, fd, z);

    /* unhide where desired */
    if (ac == 1)
      set_unhidden(z, arg);
    else for(i=1; i<ac; i++)
      set_unhidden(z, av[i]);

    /* then write to disk */
    if(write_partitions(dev, fd, z))
      warn("Done\n\n");
    else
      exit_status = 1;
}

void do_change_id(char *dev, char *pnam, char *id) {
    int fd, rw, pno;
    struct disk_desc *z;
    unsigned long i;

    z = &oldp;

    rw = !no_write;
    fd = my_open(dev, rw, 0);

    free_sectors();
    get_cylindersize(dev, fd, 1);
    get_partitions(dev, fd, z);

    pno = asc_to_index(pnam, z);
    if (id == 0) {
      printf("%x\n", z->partitions[pno].p.sys_type);
      return;
    }
    i = strtoul(id, NULL, 16);
    if (i > 255)
      fatal("Bad Id %x\n", i);
    z->partitions[pno].p.sys_type = i;

    if(write_partitions(dev, fd, z))
      warn("Done\n\n");
    else
      exit_status = 1;
}

void
do_reread(char *dev) {
    int fd;

    fd = my_open(dev, 0, 0);
    if(reread_ioctl(fd))
      printf("This disk is currently in use.\n");
}

/*
 *  I. Writing the new situation
 */

void
do_fdisk(char *dev){
    int fd;
    int c, answer;
    struct stat statbuf;
    int interactive = isatty(0);
    struct disk_desc *z;

    if (stat(dev, &statbuf) < 0) {
	perror(dev);
	fatal("Fatal error: cannot find %s\n", dev);
    }
    if (!S_ISBLK(statbuf.st_mode)) {
	warn("Warning: %s is not a block device\n", dev);
    }
    fd = my_open(dev, !no_write, 0);

    if(!no_write && !no_reread) {
	warn("Checking that no-one is using this disk right now ...\n");
	if(reread_ioctl(fd)) {
	    printf("
This disk is currently in use - repartitioning is probably a bad idea.
Umount all file systems, and swapoff all swap partitions on this disk.
Use the --no-reread flag to suppress this check.\n");
	    if (!force) {
		printf("Use the --force flag to overrule all checks.\n");
		exit(1);
	    }
	} else
	  warn("OK");
    }

    z = &oldp;

    free_sectors();
    get_cylindersize(dev, fd, 0);
    get_partitions(dev, fd, z);

    printf("Old situation:\n");
    out_partitions(dev, z);

    if (one_only && (one_only_pno = linux_to_index(one_only, z)) < 0)
      fatal("Partition %d does not exist, cannot change it\n", one_only);

    z = &newp;

    while(1) {

	read_input(dev, interactive, z);

	printf("New situation:\n");
	out_partitions(dev, z);

	if (!partitions_ok(z) && !force) {
	    if(!interactive)
	      fatal("I don't like these partitions - nothing changed.\n"
		    "(If you really want this, use the --force option.)\n");
	    else
	      printf("I don't like this - probably you should answer No\n");
	}
      ask:
	if (interactive) {
	    if (no_write)
	      printf("Are you satisfied with this? [ynq] ");
	    else
	      printf("Do you want to write this to disk? [ynq] ");
	    answer = c = getchar();
	    while (c != '\n' && c != EOF)
	      c = getchar();
	    if (c == EOF)
		printf("\nfdisk: premature end of input\n");
	    if (c == EOF || answer == 'q' || answer == 'Q') {
		fatal("Quitting - nothing changed\n");
	    } else if (answer == 'n' || answer == 'N') {
		continue;
	    } else if (answer == 'y' || answer == 'Y') {
		break;
	    } else {
		printf("Please answer one of y,n,q\n");
		goto ask;
	    }
	} else
	  break;
    }

    if(write_partitions(dev, fd, z))
      printf("Successfully wrote the new partition table\n\n");
    else
      exit_status = 1;

    reread_disk_partition(dev, fd);
    
    warn("If you created or changed a DOS partition, /dev/foo7, say, then use dd(1)\n"
	 "to zero the first 512 bytes:  dd if=/dev/zero of=/dev/foo7 bs=512 count=1\n"
	 "(See fdisk(8).)\n");
    
    sync();			/* superstition */
    sleep(3);
    exit(exit_status);
}
