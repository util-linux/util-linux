#include <stdio.h>
#include "partx.h"

#define UNIXWARE_FS_UNUSED     0
#define UNIXWARE_NUMSLICE      16
#define UNIXWARE_DISKMAGIC     (0xCA5E600D)
#define UNIXWARE_DISKMAGIC2    (0x600DDEEE)

struct unixware_slice {
	unsigned short s_label;		/* label */
	unsigned short s_flags;		/* permission flags */
	unsigned int   start_sect;	/* starting sector */
	unsigned int   nr_sects;	/* number of sectors in slice */
};

struct unixware_disklabel {
	unsigned int   d_type;		/* drive type */
	unsigned char  d_magic[4];	/* the magic number */
	unsigned int   d_version;	/* version number */
	char    d_serial[12];	   	/* serial number of the device */
	unsigned int   d_ncylinders;	/* # of data cylinders per device */
	unsigned int   d_ntracks;	/* # of tracks per cylinder */
	unsigned int   d_nsectors;	/* # of data sectors per track */
	unsigned int   d_secsize;	/* # of bytes per sector */
	unsigned int   d_part_start;	/* # of first sector of this partition */
	unsigned int   d_unknown1[12];	/* ? */
	unsigned int   d_alt_tbl;	/* byte offset of alternate table */
	unsigned int   d_alt_len;	/* byte length of alternate table */
	unsigned int   d_phys_cyl;	/* # of physical cylinders per device */
	unsigned int   d_phys_trk;	/* # of physical tracks per cylinder */
	unsigned int   d_phys_sec;	/* # of physical sectors per track */
	unsigned int   d_phys_bytes;	/* # of physical bytes per sector */
	unsigned int   d_unknown2;	/* ? */
	unsigned int   d_unknown3;	/* ? */
	unsigned int   d_pad[8];	/* pad */

	struct unixware_vtoc {
		unsigned char   v_magic[4];	/* the magic number */
		unsigned int    v_version;	/* version number */
		char    v_name[8];	      	/* volume name */
		unsigned short  v_nslices;	/* # of slices */
		unsigned short  v_unknown1;	/* ? */
		unsigned int    v_reserved[10];	/* reserved */
		struct unixware_slice
		    v_slice[UNIXWARE_NUMSLICE]; /* slice headers */
	} vtoc;

};  /* 408 */

int
read_unixware_pt(int fd, struct slice all, struct slice *sp, int ns) {
	struct unixware_disklabel *l;
	struct unixware_slice *p;
	unsigned int offset = all.start;
	char *bp;
	int n = 0;

	bp = getblock(fd, offset+29); 	/* 1 sector suffices */
	if (bp == NULL)
		return -1;

	l = (struct unixware_disklabel *) bp;
	if (four2int(l->d_magic) != UNIXWARE_DISKMAGIC ||
	    four2int(l->vtoc.v_magic) != UNIXWARE_DISKMAGIC2)
		return -1;

	p = &l->vtoc.v_slice[1];	/* slice 0 is the whole disk. */
	while (p - &l->vtoc.v_slice[0] < UNIXWARE_NUMSLICE) {
		if (p->s_label == UNIXWARE_FS_UNUSED)
			/* nothing */;
		else if (n < ns) {
			sp[n].start = p->start_sect;
			sp[n].size = p->nr_sects;
			n++;
		} else {
			fprintf(stderr,
				"unixware_partition: too many slices\n");
			break;
		}
		p++;
	}
	return n;
}
