/*
 * rescuept - Andries Brouwer - aeb@cwi.nl - 1999
 *
 * This may be distributed under the GPL.
 *
 * call: rescuept /dev/xxx
 *
 * The output is a proposed partition table, in the
 * form of input to sfdisk. Typical use:
 *
 *	./rescuept /dev/xxx > xxx.pt
 * now look at xxx.pt to see whether it resembles what
 * you expected, and possibly edit the partition types;
 * if you are satisfied, then
 *	sfdisk /dev/xxx < xxx.pt
 * will restore your partition table. If you are cautious, use
 *	sfdisk /dev/xxx -O xxx.old < xxx.pt
 * so that the original state can be retrieved using
 *	sfdisk /dev/xxx -I xxx.old
 *
 * Here xxx stands for hda or hdb or sda or sdb or ... 
 */
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#ifndef BLKGETSIZE
#define BLKGETSIZE _IO(0x12,96)
#endif

char *progname;
char *device;

#define MAXPARTITIONS	100

#define MAXPAGESZ	65536

#define BUFSZ	1024000
#define BUFSECS	(BUFSZ/512)
char buf[BUFSZ];
int bufstart = -1;

typedef unsigned int uint32;
typedef int sint32;
typedef unsigned short uint16;
typedef short sint16;
typedef unsigned char uchar;

void read_sectors(int fd, char *buf, int sectornr, int sectorct) {
	extern long long llseek(int fd, long long offset, int whence);
	long long offset;
	int n;

	offset = sectornr;
	offset *= 512;
	if (llseek(fd, offset, SEEK_SET) != offset) {
		fprintf(stderr, "%s: llseek error\n", progname);
		exit(1);
	}
	n = read(fd, buf, sectorct*512);
	if (n != sectorct*512) {
		if (n == -1)
			perror("read");
		fprintf(stderr, "%s: error reading sectors %d-%d\n",
			progname, sectornr, sectornr+sectorct-1);
		exit(1);
	}
}


/*
 * Partition table stuff
 */

struct partition {
    unsigned char bootable;		/* 0 or 0x80 */
    uchar begin_chs[3];
    unsigned char sys_type;
    uchar end_chs[3];
    unsigned int start_sect;		/* starting sector counting from 0 */
    unsigned int nr_sects;		/* nr of sectors in partition */
};

int
is_extended(unsigned char sys_type) {
	return (sys_type == 0x5 || sys_type == 0xf || sys_type == 0x85);
}

/*
 * List of (extended) partition table sectors found
 */
struct epts {
	int secno;
	char pt4[64];
} epts[MAXPARTITIONS];
int eptsct;

void addepts(int secno, char *data) {
	if (eptsct >= MAXPARTITIONS)
		return;		/* ignore */
	epts[eptsct].secno = secno;
	memcpy(epts[eptsct].pt4, data+512-66, 64);
	eptsct++;
}

/*
 * List of guessed partitions
 */
struct pt {
	int pno;
	int start;
	int size;
	unsigned char type;
} pts[MAXPARTITIONS];
int partno;

void addpart(int start, int size, unsigned char type) {
	if (partno >= MAXPARTITIONS)
		return;		/* ignore */
	pts[partno].start = start;
	pts[partno].size = size;
	pts[partno].type = type;
	partno++;
}

void outparts() {
	int i;

	for(i=0; i<partno; i++)
		printf("%s%d : start=%9d, size=%8d, Id=%2x\n",
		       device, pts[i].pno,
		       pts[i].start, pts[i].size, pts[i].type);
}

void outmsg(char *msg, int start, int nextstart, unsigned char type) {
	printf("# %5d MB %16s (type %2x): sectors %9d-%9d\n",
	       ((nextstart-start)+1024)/2048, msg, type, start, nextstart-1);
}

int
create_extended_partition(int fd, int secno, int size) {
	int sec = secno;
	int cursec = secno;
	int pno = partno;	/* number of extd partition */
	int ei = eptsct-1;
	unsigned char type = 0x5;
	int lastseen = secno;
	int ok = 0;

	if (epts[ei].secno != secno) {
		fprintf(stderr, "%s: program bug\n", progname);
		exit(1);
	}

	outmsg("candidate ext pt", secno, secno+1, type);
	addpart(secno, 1, type);		/* size to be filled in later */
	
	while(1) {
		char buf[512];
		struct partition *p1, *p2, *pr, *pe;
		p1 = (struct partition *)(& epts[ei].pt4[0]);
		p2 = (struct partition *)(& epts[ei].pt4[16]);
		/* for the time being we just ignore the rest */

		if (is_extended(p1->sys_type)) {
			pr = p2;
			pe = p1;
		} else if (is_extended(p2->sys_type)) {
			pr = p1;
			pe = p2;
		} else if (p1->sys_type == 0) {
			pr = p2;
			pe = 0;
		} else if (p2->sys_type == 0) {
			pr = p1;
			pe = 0;
		} else
			break;

		/* first handle the real partition, if any */
		if (pr->sys_type != 0) {
			int ss = cursec + pr->start_sect;
			int es = ss + pr->nr_sects;
			outmsg("found in ept", ss, es, pr->sys_type);
			addpart(ss, pr->nr_sects, pr->sys_type);
			if (lastseen < es - 1)
				lastseen = es - 1;
			if (lastseen >= size)
				break;
		}


		/* then the extended link */

		if (!pe) {
			ok = 1;
			break;
		}
		type = pe->sys_type;
		cursec = sec + pe->start_sect;
		if (cursec >= size)
			break;
		read_sectors(fd, buf, cursec, 1);
		addepts(cursec, buf);
		ei = eptsct-1;
	}

	if (!ok || lastseen == secno) {
		printf("# retracted\n");
		partno = pno;
		return 0;
	}

	pts[pno].type = type;
	pts[pno].size = lastseen+1-secno;
	outmsg("extended part ok", secno, lastseen+1, type);
	return lastseen;
}
			
		

/*
 * Recognize an ext2 superblock
 */
#define EXT2_SUPER_MAGIC	0xEF53

struct ext2_super_block {
	uint32  s_inodes_count;		 /* 0: Inodes count */
	uint32  s_blocks_count;		 /* 4: Blocks count */
	uint32  s_r_blocks_count;	 /* 8: Reserved blocks count */
	uint32  s_free_blocks_count;	 /* 12: Free blocks count */
	uint32  s_free_inodes_count;	 /* 16: Free inodes count */
	uint32  s_first_data_block;	 /* 20: First Data Block */
	uint32  s_log_block_size;	 /* 24: Block size */
	sint32  s_log_frag_size;	 /* 28: Fragment size */
	uint32  s_blocks_per_group;	 /* 32: # Blocks per group */
	uint32  s_frags_per_group;	 /* 36: # Fragments per group */
	uint32  s_inodes_per_group;	 /* 40: # Inodes per group */
	uint32  s_mtime;		 /* 44: Mount time */
	uint32  s_wtime;		 /* 48: Write time */
	uint16  s_mnt_count;	   	 /* 52: Mount count */
	sint16  s_max_mnt_count;	 /* 54: Maximal mount count */
	uint16  s_magic;		 /* 56: Magic signature */
	uint16  s_state;		 /* 58: File system state */
	uint16  s_errors;	         /* 60: Behaviour when detecting errors */
	uint16  s_minor_rev_level;	 /* 62: minor revision level */
	uint32  s_lastcheck;	         /* 64: time of last check */
	uint32  s_checkinterval;	 /* 68: max. time between checks */
	uint32  s_creator_os;	         /* 72: OS */
	uint32  s_rev_level;	         /* 76: Revision level */
	uint16  s_def_resuid;	  	 /* 80: Default uid for reserved blocks */
	uint16  s_def_resgid;	  	 /* 82: Default gid for reserved blocks */

	/* more stuff in later versions - especially s_block_group_nr is useful */
	uint32	s_first_ino;		 /* 84: First non-reserved inode */
	uint16  s_inode_size;		 /* 88: size of inode structure */
	uint16	s_block_group_nr;	 /* 90: block group # of this superblock */
	uint32	s_feature_compat;	 /* 92: compatible feature set */
	uint32	s_feature_incompat;	 /* 96: incompatible feature set */
	uint32	s_feature_ro_compat;	 /* 100: readonly-compatible feature set */
	uchar	s_uuid[16];		 /* 104: 128-bit uuid for volume */
	char	s_volume_name[16];	 /* 120: volume name */
	char	s_last_mounted[64];	 /* 136: directory where last mounted */
	uint32	s_algorithm_usage_bitmap;/* 200: For compression */
	uchar	s_prealloc_blocks;	 /* 204: Nr of blocks to try to preallocate*/
	uchar	s_prealloc_dir_blocks;	 /* 205: Nr to preallocate for dirs */
	uchar	s_reserved[818];	 /* 206-1023 */
};

/*
 * Heuristic to weed out false alarms for ext2 superblocks.
 * Recompile this after 2005, of if you destroy things that
 * have not been written the past ten years.
 */
#define YEAR (60*60*24*365)
#define LOWERLIMIT (1992-1970)*YEAR
#define UPPERLIMIT (2005-1970)*YEAR
int
is_time(uint32 t) {
	return (t >= LOWERLIMIT && t <= UPPERLIMIT);
}

int
is_ztime(uint32 t) {
	return (t == 0 || (t >= LOWERLIMIT && t <= UPPERLIMIT));
}

/*
 * Recognize a FAT filesystem
 */

struct fat_boot_sector_start {
	uchar   jump_code[3];   /* 0: Bootstrap short or near jump */
				/* usually jump code (e.g. eb 3e or eb 58) + nop (0x90) */
	uchar   system_id[8];   /* 3: OEM Name */
				/* fat16: MSDOS5.0 or MSWIN4.0 or ... */
				/* fat32: MSWIN4.1 (=W95 OSR2) */
	/* BIOS Parameter Block (BPB) */
	uchar   sector_size[2]; /* 11: bytes/sector (usually 512 or 2048) */
	uchar   cluster_size;   /* 13: sectors/cluster (a power of two in 1..128) */
	uint16  reserved;       /* 14: reserved sectors (I see 1 for FAT16, 17 for FAT32) */
				/* The # of sectors preceding the first FAT,
				   including the boot sector, so at least 1 */
	uchar   fats;	  	/* 16: # of copies of FAT (usually 2) */
	uchar   dir_entries[2]; /* 17: max # of root directory entries (n/a for FAT32) */
				/* (usually 512; 0 for FAT32) */
	uchar   sectors[2];     /* 19: total # of sectors (in <32MB partn) or 0 */
	uchar   media;	 	/* 21: media code (0xf8 for hard disks) */
	uint16  fat_length;     /* 22: sectors/FAT (n/a: 0 for FAT32) */
	uint16  secs_track;     /* 24: S = # sectors/track (in 1..63) */
	uint16  heads;	 	/* 26: H = # of heads (in 1..255) */
	uint32  hidden;		/* 28: # of hidden sectors in partition, before boot sector */
				/* (offset from cyl boundary - often equal to S) */
	uint32  total_sect;     /* 32: # of sectors (if sectors == 0) */
};

/* Media descriptor byte:
   f8	hard disk
   Floppy types:
   f0   3.5"	36/2/80 2880k
   f0	3.5"	18/2/80	1440k
   f9	3.5"	9/2/80	720k
   f9	5.25"	15/2/80	1200k
   fa	both	9/1/80	320k
   fb	both	9/2/80	640k
   fc	5.25"	9/1/40	180k
   fd	5.25"	9/2/40	360k
   fe	5.25"	8/1/40	160k
   ff	5.25"	8/2/40	320k
   Conclusion: this byte does not differentiate between 3.5" and 5.25",
   it does not give the capacity or the number of sectors per track.
   However, maybe C and H can be derived.
*/

struct fat_boot_sector_middle { /* offset 36-61 for FAT16, 64-89 for FAT32 */
	/* Extended BIOS Parameter Block */
	uchar   drive_number;	/* 0: logical drive number of partition */
				/* (typically 0 for floppy, 0x80 for each disk) */
	uchar   current_head;	/* Originally: track containing boot record. (Usually 0)
				   For WNT: bit 0: dirty: chkdsk must be run
					    bit 1: also run surface scan */
	uchar   extd_signature;	/* 2: extended signature (0x29) */
				/* WNT requires either 0x28 or 0x29 */
	uchar   serial_nr[4];	/* 3: serial number of partition */
	uchar   volume_name[11];/* 7: volume name of partition */
				/* WNT stores the volume label as a special file
				   in the root directory */
	uchar   fs_name[8];	/* 18: filesystem name (FAT12, FAT16, FAT32) */
};

struct fat16_boot_sector {
	struct fat_boot_sector_start s;		/* 0-35 */
	struct fat_boot_sector_middle m;	/* 36-61 */
	uchar  boot_code[448];			/* 62-509 */
	uchar  signature[2];			/* 510-511: aa55 */
};

struct fat32_boot_sector {
	struct fat_boot_sector_start s;		/* 0-35 */

	uint32  fat32_length;   /* 36: sectors/FAT */
	uint16  flags;		/* 40: bit 7: fat mirroring, low 4: active fat */
				/* If mirroring is disabled (bit8 set) the FAT
				   info is only written to the active FAT copy. */
	uchar   version[2];     /* 42: major, minor filesystem version */
	uint32  root_cluster;   /* 44: first cluster in root directory */
	uint16  info_sector;    /* 48: filesystem info sector # relative
				   to partition start (usually 1) */
	uint16  backup_boot;    /* 50: backup boot sector # relat. to part. start */
	uint16  reserved2[6];   /* 52-63: Unused */

	struct fat_boot_sector_middle m;	/* 64-89 */
	uchar  boot_code[420];			/* 90-509 */
	uchar  signature[2];			/* 510-511: aa55 */
};

/*
 * The boot code contains message strings ("Invalid system disk")
 * but these are often localized ("Ongeldige diskette ").
 * After these messages one finds two or three filenames.
 * (MSDOS 6.2: "\r\nNon-System disk or disk error\r\n"
 * "Replace and press any key when ready\r\n", "IO      SYS", "MSDOS   SYS") 
 * (W95: "IO      SYS", "MSDOS   SYS", "WINBOOT SYS")
 * In all cases the sector seems to end with 0, 0, 55, aa.
 *
 * Random collection of messages (closed by \0377 or 0):
 * "\r\nInvalid system disk"
 * "\r\nOngeldige diskette "
 * "\r\nDisk I/O error"
 * "\r\nI/O-fout      "
 * "\r\nReplace the disk, and then press any key\r\n"
 * "\r\nVervang de diskette en druk op een toets\r\n"
 * This seems to suggest that the localized strings have the same length.
 *
 * "Non-System disk or disk error"
 * "Replace and press any key when ready"
 * "Disk Boot failure"
 *
 * "BOOT: Couldn't find NTLDR"
 * "I/O error reading disk"
 * "Please insert another disk"
 */

struct fat32_boot_fsinfo {
	uint32   signature1;     /* 41 61 52 52 */
	uchar    unknown1[480];
	uint32   signature2;     /* 61 41 72 72 0x61417272L */
	uint32   free_clusters;  /* Free cluster count.  -1 if unknown */
	uint32   next_cluster;   /* Most recently allocated cluster.
				  * Unused under Linux. */
	uchar    unknown2[14];
	uchar    signature[2];	 /* 510-511: aa55 */
};

struct msdos_dir_entry {
	uchar    name[8],ext[3]; /* name and extension */
	uchar    attr;	  	 /* attribute bits */
	uchar    lcase;	 	 /* Case for base and extension */
	uchar    ctime_ms;       /* Creation time, milliseconds */
	uint16   ctime;		 /* Creation time */
	uint16   cdate;		 /* Creation date */
	uint16   adate;		 /* Last access date */
	uint16   starthi;	 /* High 16 bits of cluster in FAT32 */
	uint16   time,date,start;/* time, date and first cluster */
	uint32   size;		 /* file size (in bytes) */
};

/* New swap space */
struct swap_header_v1 {
        char         bootbits[1024];    /* Space for disklabel etc. */
        unsigned int version;
        unsigned int last_page;
        unsigned int nr_badpages;
        unsigned int padding[125];
        unsigned int badpages[1];
};

struct unixware_slice {
	unsigned short slice_type;
	unsigned short slice_flags;
	unsigned int start;
	unsigned int size;
};

struct bsd_disklabel {
	uchar	d_magic[4];
	uchar	d_stuff1[4];
	uchar	d_typename[16];		/* type name, e.g. "eagle" */
	uchar	d_packname[16];		/* pack identifier */ 
	uint32	d_secsize;		/* # of bytes per sector */
	uint32	d_nsectors;		/* # of data sectors per track */
	uint32	d_ntracks;		/* # of tracks per cylinder */
	uint32	d_ncylinders;		/* # of data cylinders per unit */
	uint32	d_secpercyl;		/* # of data sectors per cylinder */
	uint32	d_secperunit;		/* # of data sectors per unit */
	uchar	d_stuff2[68];
	uchar	d_magic2[4];		/* the magic number (again) */
	uint16	d_checksum;		/* xor of data incl. partitions */

			/* filesystem and partition information: */
	uint16	d_npartitions;		/* number of partitions in following */
	uint32	d_bbsize;		/* size of boot area at sn0, bytes */
	uint32	d_sbsize;		/* max size of fs superblock, bytes */
	struct	bsd_partition {		/* the partition table */
		uint32	p_size;		/* number of sectors in partition */
		uint32	p_offset;	/* starting sector */
		uchar	p_stuff[8];
	} d_partitions[16];		/* 16 is for openbsd */
};


int
main(int argc, char **argv){
	int i,j,fd;
	long size;
	int pagesize, pagesecs;
	unsigned char *bp;
	struct ext2_super_block *e2bp;
	struct fat16_boot_sector *fat16bs;
	struct fat32_boot_sector *fat32bs;

	progname = argv[0];

	if (argc != 2) {
		fprintf(stderr, "call: %s device\n", progname);
		exit(1);
	}

	device = argv[1];

	fd = open(device, O_RDONLY);
	if (fd < 0) {
		perror(device);
		fprintf(stderr, "%s: could not open %s\n", progname, device);
		exit(1);
	}

	if (ioctl(fd, BLKGETSIZE, &size)) {
		struct stat s;
		perror("BLKGETSIZE");
		fprintf(stderr, "%s: could not get device size\n", progname);
		if (stat(device, &s)) {
			fprintf(stderr, "and also stat fails. Aborting.\n");
			exit(1);
		}
		size = s.st_size / 512;
	}

	pagesize = getpagesize();
	if (pagesize <= 0)
		pagesize = 4096;
	else if (pagesize > MAXPAGESZ) {
		fprintf(stderr, "%s: ridiculous pagesize %d\n", progname, pagesize);
		exit(1);
	}
	pagesecs = pagesize/512;

	printf("# partition table of %s\n", device);
	printf("# total size %ld sectors\n", size);
	printf("unit: sectors\n");

	for(i=0; i<size; i++) {
		if (i/BUFSECS != bufstart) {
			int len, secno;
			bufstart = i/BUFSECS;
			secno = bufstart*BUFSECS;
			len = BUFSECS;
			if (size - secno < len)
				len = size - secno;
			len = (len / 2)*2;	/* avoid reading the last (odd) sector */
			read_sectors(fd, buf, secno, len);
		}
			
		j = i % BUFSECS;

		bp = buf + 512 * j;

		if (bp[510] == 0x55 && bp[511] == 0xAA) {
			char *cp = bp+512-2-64;
			int j;

			if (i==0)
				continue; /* the MBR is supposed to be broken */

			/* Unfortunately one finds extended partition table sectors
			   that look just like a fat boot sector, except that the
			   partition table bytes have been overwritten */
			/* typical FAT32 end: "nd then press ...", followed by
			   IO.SYS and MSDOS.SYS and WINBOOT.SYS directory entries.
			   typical extd part tab end: 2 entries, 32 nul bytes */

			for(j=0; j<32; j++)
				if (cp[32+j])
					goto nonzero;
			addepts(i, bp);
			if (i > 0) {
				j = create_extended_partition(fd, i, size);
				if (j && j > i)
					i = j;	/* skip */
			}
			continue;
		nonzero:
			fat16bs = (struct fat16_boot_sector *) bp;
			if (fat16bs->s.media == 0xf8 &&
			    fat16bs->m.extd_signature == 0x29 &&
			    !strncmp(fat16bs->m.fs_name, "FAT", 3)) {
				int lth;
				lth = fat16bs->s.sectors[0] +
					fat16bs->s.sectors[1]*256;
				if (lth) {
					outmsg("small fat partition", i, i+lth, 0x1);
					addpart(i, lth, 0x1);
				} else {
					lth = fat16bs->s.total_sect;
					outmsg("fat partition", i, i+lth, 0x6);
					addpart(i, lth, 0x6);
				}
				i = i+lth-1;	/* skip */
				continue;
			}

			fat32bs = (struct fat32_boot_sector *) bp;
			if (fat32bs->s.media == 0xf8 &&
			    fat32bs->m.extd_signature == 0x29 &&
			    !strncmp(fat32bs->m.fs_name, "FAT32   ", 8)) {
				int lth = fat32bs->s.total_sect;
				outmsg("fat32 partition", i, i+lth, 0xb); /* or 0xc */
				addpart(i, lth, 0xb);
				i = i+lth-1;	/* skip */
				continue;
			}
		}

		if (!strncmp(bp+502, "SWAP-SPACE", 10)) {
			char *last;
			int ct;
			int ss = i-pagesecs+1;
			int es;
			char buf2[MAXPAGESZ];

			read_sectors(fd, buf2, ss, pagesecs);
			for (last = buf2+pagesize-10-1; last > buf2; last--)
				if (*last)
					break;
			for (ct = 7; ct >= 0; ct--)
				if (*last & (1<<ct))
					break;
			es = ((last - buf2)*8 + ct + 1)*pagesecs + ss;
			if (es <= size) {
				outmsg("old swap space", ss, es, 0x82);
				addpart(ss, es-ss, 0x82);

				i = es-1;	/* skip */
				continue;
			}
		}

		if (!strncmp(bp+502, "SWAPSPACE2", 10)) {
			int ss = i-pagesecs+1;
			int es, lth;
			char buf2[MAXPAGESZ];
			struct swap_header_v1 *p;

			read_sectors(fd, buf2, ss, pagesecs);
			p = (struct swap_header_v1 *) buf2;
			lth = (p->last_page + 1)* pagesecs;
			es = ss + lth;
			if (es <= size) {
				outmsg("new swap space", ss, es, 0x82);
				addpart(ss, lth, 0x82);

				i = es-1;       /* skip */
				continue;
			}
		}

		e2bp = (struct ext2_super_block *) bp;
		if (e2bp->s_magic == EXT2_SUPER_MAGIC && is_time(e2bp->s_mtime)
		    && is_time(e2bp->s_wtime) && is_ztime(e2bp->s_lastcheck)
		    && e2bp->s_log_block_size <= 10 /* at most 1 MB blocks */) {
			char buf[512];
			struct ext2_super_block *bp2;
			int ss, sz, es, gsz, j;

			ss = i-2;
			sz = (e2bp->s_blocks_count << (e2bp->s_log_block_size + 1));
			gsz = (e2bp->s_blocks_per_group << (e2bp->s_log_block_size + 1));
			if (e2bp->s_block_group_nr > 0)
				ss -= gsz * e2bp->s_block_group_nr;
			es = ss + sz;
			if (ss > 0 && es > i && es <= size) {
				if (e2bp->s_block_group_nr == 0) {
					outmsg("ext2 partition", ss, es, 0x83);
					addpart(ss, es-ss, 0x83);

					i = es-1;	/* skip */
					continue;
				}

				/* maybe we jumped into the middle of a partially
				   obliterated ext2 partition? */

				printf("# sector %d looks like an ext2 superblock copy #%d;\n"
				       "# in a partition covering sectors %d-%d\n",
				       i, e2bp->s_block_group_nr, ss, es-1);

				for (j=1; j<=e2bp->s_block_group_nr; j++) {
					read_sectors(fd, buf, i-j*gsz, 1);
					bp2 = (struct ext2_super_block *) buf;
					if (bp2->s_magic != EXT2_SUPER_MAGIC ||
					    bp2->s_block_group_nr !=
					      e2bp->s_block_group_nr - j)
						break;
				}
				if (j == 1)
					printf("# however, sector %d doesnt look like a sb.\n",
					       i-gsz);
				else if (j <= e2bp->s_block_group_nr)
					printf("# also the preceding %d block groups seem OK\n"
					       "# but before that things seem to be wrong.\n",
					       j-1);
				else {
					printf("# found all preceding superblocks OK\n"
					       "# Warning: overlapping partitions?\n");
					outmsg("ext2 partition", ss, es, 0x83);
					addpart(ss, es-ss, 0x83);
					i = es-1;       /* skip */
					continue;
				}
			}

		}

		if (bp[4] == 0x0d && bp[5] == 0x60 &&
		    bp[6] == 0x5e && bp[7] == 0xca &&   /* CA5E600D */
		    bp[156] == 0xee && bp[157] == 0xde &&
		    bp[158] == 0x0d && bp[159] == 0x60) /* 600DDEEE */ {
			int ss, es;
			struct unixware_slice *u;
			printf("# Unixware partition seen\n");
			u = (struct unixware_slice *)(bp + 216);
			if (u->slice_type == 5	/* entire disk */
			    && (u->slice_flags & 0x200)) /* valid */ {
				ss = u->start;
				es = u->start + u->size;
				outmsg("Unixware ptn", ss, es, 0x63);
				addpart(ss, es-ss, 0x63);
				i = es-1;
				continue;
			} else
				printf("# Unrecognized details\n");
		}

		/* bsd disk magic 0x82564557UL */
		if (bp[0] == 0x57 && bp[1] == 0x45 && bp[2] == 0x56 && bp[3] == 0x82) {
			int ss, es, npts, j;
			struct bsd_disklabel *l;
			struct bsd_partition *p;
			printf("# BSD magic seen in sector %d\n", i);
			l = (struct bsd_disklabel *) bp;
			if (l->d_magic2[0] != 0x57 || l->d_magic2[1] != 0x45 ||
			    l->d_magic2[2] != 0x56 || l->d_magic2[3] != 0x82)
				printf("# 2nd magic bad - ignored this sector\n");
			else if ((npts = l->d_npartitions) > 16)
				printf("# strange number (%d) of subpartitions - "
				       "ignored this sector\n", npts);
			else {
				for (j=0; j<npts; j++) {
					p = &(l->d_partitions[j]);
					if (p->p_size)
						printf("# part %c: size %9d, start %9d\n",
						       'a'+j, p->p_size, p->p_offset);
				}
				ss = l->d_partitions[2].p_offset;
				es = ss + l->d_partitions[2].p_size;
				if (ss != i-1)
					printf("# strange start of whole disk - "
					       "ignored this sector\n");
				else {
					/* FreeBSD 0xa5, OpenBSD 0xa6, NetBSD 0xa9, BSDI 0xb7 */
					/* How to distinguish? */
					outmsg("BSD partition", ss, es, 0xa5);
					addpart(ss, es-ss, 0xa5);
					i = es-1;
					continue;
				}
			}
		}
	}

	outparts();
	
	exit(0);
}

