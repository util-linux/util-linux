#include <stdio.h>
#include "partx.h"

#define BSD_DISKMAGIC	(0x82564557UL)	/* The disk magic number */
#define XBSD_MAXPARTITIONS	16
#define BSD_FS_UNUSED		0

struct bsd_disklabel {
	unsigned int	d_magic;	/* the magic number */
	short int	d_type;		/* drive type */
	short int	d_subtype;	/* controller/d_type specific */
	char	d_typename[16];		/* type name, e.g. "eagle" */
	char	d_packname[16];		/* pack identifier */ 
	unsigned int	d_secsize;	/* # of bytes per sector */
	unsigned int	d_nsectors;	/* # of data sectors per track */
	unsigned int	d_ntracks;	/* # of tracks per cylinder */
	unsigned int	d_ncylinders;	/* # of data cylinders per unit */
	unsigned int	d_secpercyl;	/* # of data sectors per cylinder */
	unsigned int	d_secperunit;	/* # of data sectors per unit */
	unsigned short	d_sparespertrack;/* # of spare sectors per track */
	unsigned short	d_sparespercyl;	/* # of spare sectors per cylinder */
	unsigned int	d_acylinders;	/* # of alt. cylinders per unit */
	unsigned short	d_rpm;		/* rotational speed */
	unsigned short	d_interleave;	/* hardware sector interleave */
	unsigned short	d_trackskew;	/* sector 0 skew, per track */
	unsigned short	d_cylskew;	/* sector 0 skew, per cylinder */
	unsigned int	d_headswitch;	/* head switch time, usec */
	unsigned int	d_trkseek;	/* track-to-track seek, usec */
	unsigned int	d_flags;	/* generic flags */
	unsigned int	d_drivedata[5];	/* drive-type specific information */
	unsigned int	d_spare[5];	/* reserved for future use */
	unsigned int	d_magic2;	/* the magic number (again) */
	unsigned short	d_checksum;	/* xor of data incl. partitions */

			/* filesystem and partition information: */
	unsigned short	d_npartitions;	/* number of partitions in following */
	unsigned int	d_bbsize;	/* size of boot area at sn0, bytes */
	unsigned int	d_sbsize;	/* max size of fs superblock, bytes */
	struct	bsd_partition {		/* the partition table */
		unsigned int	p_size;	  /* number of sectors in partition */
		unsigned int	p_offset; /* starting sector */
		unsigned int	p_fsize;  /* filesystem basic fragment size */
		unsigned char	p_fstype; /* filesystem type, see below */
		unsigned char	p_frag;	  /* filesystem fragments per block */
		unsigned short	p_cpg;	  /* filesystem cylinders per group */
	} d_partitions[XBSD_MAXPARTITIONS];/* actually may be more */
};

int
read_bsd_pt(int fd, struct slice all, struct slice *sp, int ns) {
	struct bsd_disklabel *l;
	struct bsd_partition *p;
	unsigned int offset = all.start;
	int max_partitions;
	char *bp;
	int n = 0;

	bp = getblock(fd, offset+1); 	/* 1 sector suffices */
	if (bp == NULL)
		return -1;

	l = (struct bsd_disklabel *) bp;
	if (l->d_magic != BSD_DISKMAGIC)
		return -1;

	max_partitions = 16;
	if (l->d_npartitions < max_partitions)
		max_partitions = l->d_npartitions;
	for (p = l->d_partitions; p - l->d_partitions <  max_partitions; p++) {
		if (p->p_fstype == BSD_FS_UNUSED)
			/* nothing */;
		else if (n < ns) {
			sp[n].start = p->p_offset;
			sp[n].size = p->p_size;
			n++;
		} else {
			fprintf(stderr,
				"bsd_partition: too many slices\n");
			break;
		}
	}
	return n;
}
