/*
 * Based on libdisk from xfsprogs and Linux fdisk.
 *
 * Copyright (c) 2000-2001 Silicon Graphics, Inc.
 * Copyright (C) 2009 Karel Zak <kzak@redhat.com>
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>

#include "blkdev.h"

/* we need to read two sectors, beacuse BSD label offset is 512 */
#define PTTYPE_BUFSIZ	(2 * DEFAULT_SECTOR_SIZE)	/* 1024 */

/*
 * SGI
 */
struct sgi_device_parameter { /* 48 bytes */
	unsigned char  skew;
	unsigned char  gap1;
	unsigned char  gap2;
	unsigned char  sparecyl;
	unsigned short pcylcount;
	unsigned short head_vol0;
	unsigned short ntrks;	/* tracks in cyl 0 or vol 0 */
	unsigned char  cmd_tag_queue_depth;
	unsigned char  unused0;
	unsigned short unused1;
	unsigned short nsect;	/* sectors/tracks in cyl 0 or vol 0 */
	unsigned short bytes;
	unsigned short ilfact;
	unsigned int   flags;		/* controller flags */
	unsigned int   datarate;
	unsigned int   retries_on_error;
	unsigned int   ms_per_word;
	unsigned short xylogics_gap1;
	unsigned short xylogics_syncdelay;
	unsigned short xylogics_readdelay;
	unsigned short xylogics_gap2;
	unsigned short xylogics_readgate;
	unsigned short xylogics_writecont;
};

#define	SGI_VOLHDR	0x00
/* 1 and 2 were used for drive types no longer supported by SGI */
#define	SGI_SWAP	0x03
/* 4 and 5 were for filesystem types SGI haven't ever supported on MIPS CPUs */
#define	SGI_VOLUME	0x06
#define	SGI_EFS		0x07
#define	SGI_LVOL	0x08
#define	SGI_RLVOL	0x09
#define	SGI_XFS		0x0a
#define	SGI_XFSLOG	0x0b
#define	SGI_XLV		0x0c
#define	SGI_XVM		0x0d
#define	ENTIRE_DISK	SGI_VOLUME
/*
 * controller flags
 */
#define	SECTOR_SLIP	0x01
#define	SECTOR_FWD	0x02
#define	TRACK_FWD	0x04
#define	TRACK_MULTIVOL	0x08
#define	IGNORE_ERRORS	0x10
#define	RESEEK		0x20
#define	CMDTAGQ_ENABLE	0x40

struct sgi_volume_header {
	unsigned int   magic;		 /* expect SGI_LABEL_MAGIC */
	unsigned short boot_part;        /* active boot partition */
	unsigned short swap_part;        /* active swap partition */
	unsigned char  boot_file[16];    /* name of the bootfile */
	struct sgi_device_parameter devparam;	/*  1 * 48 bytes */
	struct volume_directory {		/* 15 * 16 bytes */
		unsigned char vol_file_name[8];	/* a character array */
		unsigned int  vol_file_start;	/* number of logical block */
		unsigned int  vol_file_size;	/* number of bytes */
	} directory[15];
	struct sgi_partition {			/* 16 * 12 bytes */
		unsigned int num_sectors;	/* number of blocks */
		unsigned int start_sector;	/* must be cylinder aligned */
		unsigned int id;
	} partitions[16];
	unsigned int   csum;
	unsigned int   fillbytes;
};

#define	SGI_LABEL_MAGIC		0x0be5a941

static uint32_t
twos_complement_32bit_sum(u_int32_t *base, int size)
{
	int i;
	u_int32_t sum = 0;

	size = size / sizeof(u_int32_t);
	for (i = 0; i < size; i++)
		sum = sum - ntohl(base[i]);
	return sum;
}

static int
sgi_parttable(unsigned char *base)
{
	u_int32_t csum;
	struct sgi_volume_header *vh = (struct sgi_volume_header *) base;

	if (ntohl(vh->magic) != SGI_LABEL_MAGIC)
		return 0;
	csum = twos_complement_32bit_sum((uint32_t *)vh,
				sizeof(struct sgi_volume_header));
	return !csum;
}

/*
 * DOS
 */
static int
dos_parttable(unsigned char *base)
{
	return (base[510] == 0x55 && base[511] == 0xaa);
}

/*
 * AIX
 */
typedef struct {
	unsigned int   magic;        /* expect AIX_LABEL_MAGIC */
	/* ... */
} aix_partition;

#define	AIX_LABEL_MAGIC		0xc9c2d4c1
#define	AIX_LABEL_MAGIC_SWAPPED	0xc1d4c2c9
#define aixlabel(x) ((aix_partition *)x)

static int
aix_parttable(unsigned char *base)
{
	return (aixlabel(base)->magic == AIX_LABEL_MAGIC ||
		aixlabel(base)->magic == AIX_LABEL_MAGIC_SWAPPED);
}

/*
 * SUN
 */
typedef struct {
	unsigned char info[128];   /* Informative text string */
	unsigned char spare0[14];
	struct sun_info {
		unsigned char spare1;
		unsigned char id;
		unsigned char spare2;
		unsigned char flags;
	} infos[8];
	unsigned char spare1[246]; /* Boot information etc. */
	unsigned short rspeed;     /* Disk rotational speed */
	unsigned short pcylcount;  /* Physical cylinder count */
	unsigned short sparecyl;   /* extra sects per cylinder */
	unsigned char spare2[4];   /* More magic... */
	unsigned short ilfact;     /* Interleave factor */
	unsigned short ncyl;       /* Data cylinder count */
	unsigned short nacyl;      /* Alt. cylinder count */
	unsigned short ntrks;      /* Tracks per cylinder */
	unsigned short nsect;      /* Sectors per track */
	unsigned char spare3[4];   /* Even more magic... */
	struct sun_partition {
		u_int32_t start_cylinder;
		u_int32_t num_sectors;
	} partitions[8];
	unsigned short magic;      /* Magic number */
	unsigned short csum;       /* Label xor'd checksum */
} sun_partition;

#define SUN_LABEL_MAGIC          0xDABE
#define SUN_LABEL_MAGIC_SWAPPED  0xBEDA
#define sunlabel(x) ((sun_partition *)x)

static int
sun_parttable(unsigned char *base)
{
	unsigned short *ush;
	int csum = 0;

	if (sunlabel(base)->magic != SUN_LABEL_MAGIC &&
	    sunlabel(base)->magic != SUN_LABEL_MAGIC_SWAPPED)
		return csum;
	ush = ((unsigned short *) (sunlabel(base) + 1)) - 1;
	while (ush >= (unsigned short *)sunlabel(base))
		csum ^= *ush--;
	return !csum;
}

/*
 * MAC
 */
typedef struct {
	unsigned short magic;
	/* ... */
} mac_partition;

#define MAC_LABEL_MAGIC		0x4552
#define MAC_PARTITION_MAGIC	0x504d
#define MAC_OLD_PARTITION_MAGIC	0x5453
#define maclabel(x) ((mac_partition *)x)

static int
mac_parttable(unsigned char *base)
{
	return (ntohs(maclabel(base)->magic) == MAC_LABEL_MAGIC ||
		ntohs(maclabel(base)->magic) == MAC_PARTITION_MAGIC ||
		ntohs(maclabel(base)->magic) == MAC_OLD_PARTITION_MAGIC);
}

/*
 * BSD subpartitions listed in a disklabel, under a dos-like partition.
 */
#define BSD_DISKMAGIC		0x82564557UL		/* The disk magic number */
#define BSD_DISKMAGIC_SWAPED	0x57455682UL
struct bsd_disklabel {
	uint32_t	magic;		/* the magic number */
	/* ... */
};

static int
bsd_parttable(unsigned char *base)
{
	struct bsd_disklabel *l = (struct bsd_disklabel *)
					(base + (DEFAULT_SECTOR_SIZE * 1));

	return (l->magic == BSD_DISKMAGIC || l->magic == BSD_DISKMAGIC_SWAPED);
}

const char *
get_pt_type_fd(int fd)
{
	char	*type = NULL;
	unsigned char	buf[PTTYPE_BUFSIZ];

	if (read(fd, buf, PTTYPE_BUFSIZ) != PTTYPE_BUFSIZ)
		;
	else {
		if (sgi_parttable(buf))
			type = "SGI";
		else if (sun_parttable(buf))
			type = "Sun";
		else if (aix_parttable(buf))
			type = "AIX";
		else if (dos_parttable(buf))
			type = "DOS";
		else if (mac_parttable(buf))
			type = "Mac";
		else if (bsd_parttable(buf))
			type = "BSD";
	}
	return type;
}

const char *
get_pt_type(const char *device)
{
	int fd;
	const char *type;

	fd = open(device, O_RDONLY);
	if (fd == -1)
		return NULL;
	type = get_pt_type_fd(fd);
	close(fd);
	return type;
}

#ifdef TEST_PROGRAM
int
main(int argc, char **argv)
{
	const char *type;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <device>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	type = get_pt_type(argv[1]);
	if (type)
		printf("Partition type: %s\n", type);
	exit(EXIT_SUCCESS);
}
#endif
