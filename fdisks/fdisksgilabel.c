/*
 *
 * fdisksgilabel.c
 *
 * Copyright (C) Andreas Neuper, Sep 1998.
 *	This file may be modified and redistributed under
 *	the terms of the GNU Public License.
 *
 * 1999-03-20 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *	Internationalization
 *
 * 2003-03-20 Phillip Kesling <pkesling@sgi.com>
 *      Some fixes
 *
 * 2012-06-16 Davidlohr Bueso <dave@gnu.org>
 *      Adapt to fdisk context and add heap sort for partitions
 */

#include <stdio.h>              /* stderr */
#include <stdlib.h>		/* exit */
#include <string.h>             /* strstr */
#include <unistd.h>             /* write */
#include <sys/ioctl.h>          /* ioctl */
#include <sys/stat.h>           /* stat */
#include <assert.h>             /* assert */

#include "nls.h"
#include "all-io.h"
#include "xalloc.h"

#include "blkdev.h"

#include "bitops.h"
#include "pt-sgi.h"
#include "pt-mbr.h"
#include "common.h"
#include "fdisk.h"
#include "fdisksgilabel.h"

/*
 * in-memory fdisk SGI stuff
 */
struct fdisk_sgi_label {
	struct fdisk_label	head;		/* generic fdisk part */
	struct sgi_disklabel	*header;	/* on-disk data (pointer to cxt->firstsector) */

	struct sgi_freeblocks {
		unsigned int first;
		unsigned int last;
	} freelist[17];
};

static struct fdisk_parttype sgi_parttypes[] =
{
	{SGI_TYPE_VOLHDR,	N_("SGI volhdr")},
	{SGI_TYPE_TRKREPL,	N_("SGI trkrepl")},
	{SGI_TYPE_SECREPL,	N_("SGI secrepl")},
	{SGI_TYPE_SWAP,		N_("SGI raw")},
	{SGI_TYPE_BSD,		N_("SGI bsd")},
	{SGI_TYPE_SYSV,		N_("SGI sysv")},
	{SGI_TYPE_ENTIRE_DISK,	N_("SGI volume")},
	{SGI_TYPE_EFS,		N_("SGI efs")},
	{SGI_TYPE_LVOL,		N_("SGI lvol")},
	{SGI_TYPE_RLVOL,	N_("SGI rlvol")},
	{SGI_TYPE_XFS,		N_("SGI xfs")},
	{SGI_TYPE_XFSLOG,	N_("SGI xfslog")},
	{SGI_TYPE_XLV,		N_("SGI xlv")},
	{SGI_TYPE_XVM,		N_("SGI xvm")},
	{MBR_LINUX_SWAP_PARTITION, N_("Linux swap")},
	{MBR_LINUX_DATA_PARTITION, N_("Linux native")},
	{MBR_LINUX_LVM_PARTITION, N_("Linux LVM")},
	{MBR_LINUX_RAID_PARTITION, N_("Linux RAID")},
	{0, NULL }
};

/* return poiter buffer with on-disk data */
static inline struct sgi_disklabel *self_disklabel(struct fdisk_context *cxt)
{
	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, SGI));

	return ((struct fdisk_sgi_label *) cxt->label)->header;
}

/* return in-memory fdisk data */
static inline struct fdisk_sgi_label *self_label(struct fdisk_context *cxt)
{
	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, SGI));

	return (struct fdisk_sgi_label *) cxt->label;
}

/*
 * Information within second on-disk block
 */
#define	SGI_INFO_MAGIC		0x00072959

struct sgi_info {
	unsigned int   magic;		/* looks like a magic number */
	unsigned int   a2;
	unsigned int   a3;
	unsigned int   a4;
	unsigned int   b1;
	unsigned short b2;
	unsigned short b3;
	unsigned int   c[16];
	unsigned short d[3];
	unsigned char  scsi_string[50];
	unsigned char  serial[137];
	unsigned short check1816;
	unsigned char  installer[225];
};

static struct sgi_info *sgi_new_info(void)
{
	struct sgi_info *info = calloc(1, sizeof(struct sgi_info));

	if (!info)
		return NULL;

	info->magic = cpu_to_be32(SGI_INFO_MAGIC);
	info->b1 = cpu_to_be32(-1);
	info->b2 = cpu_to_be16(-1);
	info->b3 = cpu_to_be16(1);

	/* You may want to replace this string !!!!!!! */
	strcpy((char *) info->scsi_string, "IBM OEM 0662S12         3 30");
	strcpy((char *) info->serial, "0000");
	info->check1816 = cpu_to_be16(18 * 256 + 16);
	strcpy((char *) info->installer, "Sfx version 5.3, Oct 18, 1994");

	return info;
}

static void sgi_free_info(struct sgi_info *info)
{
	free(info);
}

int sgi_create_info(struct fdisk_context *cxt)
{
	struct sgi_disklabel *sgilabel = self_disklabel(cxt);

	/* I keep SGI's habit to write the sgilabel to the second block */
	sgilabel->volume[0].block_num = cpu_to_be32(2);
	sgilabel->volume[0].num_bytes = cpu_to_be32(sizeof(struct sgi_info));
	strncpy((char *) sgilabel->volume[0].name, "sgilabel", 8);
	return 0;
}


/*
 * only dealing with free blocks here
 */
static void set_freelist(struct fdisk_context *cxt,
		size_t i, unsigned int f, unsigned int l)
{
	struct fdisk_sgi_label *sgi = self_label(cxt);

	if (i < ARRAY_SIZE(sgi->freelist)) {
		sgi->freelist[i].first = f;
		sgi->freelist[i].last = l;
	}
}

static void add_to_freelist(struct fdisk_context *cxt,
		unsigned int f, unsigned int l)
{
	struct fdisk_sgi_label *sgi = self_label(cxt);
	size_t i = 0;

	for ( ; i < ARRAY_SIZE(sgi->freelist); i++) {
		if (sgi->freelist[i].last == 0)
			break;
	}
	set_freelist(cxt, i, f, l);
}

static void clear_freelist(struct fdisk_context *cxt)
{
	struct fdisk_sgi_label *sgi = self_label(cxt);

	memset(sgi->freelist, 0, sizeof(sgi->freelist));
}

static unsigned int is_in_freelist(struct fdisk_context *cxt, unsigned int b)
{
	struct fdisk_sgi_label *sgi = self_label(cxt);
	size_t i;

	for (i = 0; i < ARRAY_SIZE(sgi->freelist); i++) {
		if (sgi->freelist[i].first <= b
		    && sgi->freelist[i].last >= b)
			return sgi->freelist[i].last;
	}

	return 0;
}


static int sgi_get_nsect(struct fdisk_context *cxt)
{
	struct sgi_disklabel *sgilabel = self_disklabel(cxt);
	return be16_to_cpu(sgilabel->devparam.nsect);
}

static int sgi_get_ntrks(struct fdisk_context *cxt)
{
	struct sgi_disklabel *sgilabel = self_disklabel(cxt);
	return be16_to_cpu(sgilabel->devparam.ntrks);
}

static size_t count_used_partitions(struct fdisk_context *cxt)
{
	size_t i, ct = 0;

	for (i = 0; i < cxt->label->nparts_max; i++)
		ct += sgi_get_num_sectors(cxt, i) > 0;

	return ct;
}

static int sgi_probe_label(struct fdisk_context *cxt)
{
	struct fdisk_sgi_label *sgi;
	struct sgi_disklabel *sgilabel;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, SGI));
	assert(sizeof(struct sgi_disklabel) <= 512);

	/* map first sector to header */
	sgi = (struct fdisk_sgi_label *) cxt->label;
	sgi->header = (struct sgi_disklabel *) cxt->firstsector;
	sgilabel = sgi->header;

	if (be32_to_cpu(sgilabel->magic) != SGI_LABEL_MAGIC) {
		sgi->header = NULL;
		return 0;
	}

	/*
	 * test for correct checksum
	 */
	if (sgi_pt_checksum(sgilabel) != 0)
		fdisk_warnx(cxt, _("Detected sgi disklabel with wrong checksum."));

	clear_freelist(cxt);
	cxt->label->nparts_max = SGI_MAXPARTITIONS;
	cxt->label->nparts_cur = count_used_partitions(cxt);
	return 1;
}

void
sgi_list_table(struct fdisk_context *cxt, int xtra)
{
	struct sgi_disklabel *sgilabel = self_disklabel(cxt);
	struct sgi_device_parameter *sgiparam = &sgilabel->devparam;
	size_t i, w;
	int kpi = 0;		/* kernel partition ID */

	w = strlen(cxt->dev_path);

	if (xtra) {
		printf(_("\nDisk %s (SGI disk label): %d heads, %llu sectors\n"
			 "%llu cylinders, %d physical cylinders\n"
			 "%d extra sects/cyl, interleave %d:1\n"
			 "%s\n"
			 "Units = %s of %d * %ld bytes\n\n"),
			       cxt->dev_path, cxt->geom.heads, cxt->geom.sectors, cxt->geom.cylinders,
			       be16_to_cpu(sgiparam->pcylcount),
		       (int) sgiparam->sparecyl, be16_to_cpu(sgiparam->ilfact),
		       (char *)sgilabel,
		       fdisk_context_get_unit(cxt, PLURAL),
		       fdisk_context_get_units_per_sector(cxt),
                       cxt->sector_size);
	} else {
		printf(_("\nDisk %s (SGI disk label): "
			 "%d heads, %llu sectors, %llu cylinders\n"
			 "Units = %s of %d * %ld bytes\n\n"),
		       cxt->dev_path, cxt->geom.heads, cxt->geom.sectors, cxt->geom.cylinders,
		       fdisk_context_get_unit(cxt, PLURAL),
		       fdisk_context_get_units_per_sector(cxt),
                       cxt->sector_size);
	}
	printf(_("----- partitions -----\n"
		 "Pt# %*s  Info     Start       End   Sectors  Id  System\n"),
	       (int) w + 1, _("Device"));
	for (i = 0 ; i < cxt->label->nparts_max; i++) {
		if (sgi_get_num_sectors(cxt, i)) {
			uint32_t start = sgi_get_start_sector(cxt, i);
			uint32_t len = sgi_get_num_sectors(cxt, i);
			struct fdisk_parttype *t = fdisk_get_partition_type(cxt, i);

			kpi++;		/* only count nonempty partitions */
			printf(
				"%2zd: %s %4s %9ld %9ld %9ld  %2x  %s\n",
/* fdisk part number */   i+1,
/* device */              fdisk_partname(cxt->dev_path, kpi),
/* flags */               (sgi_get_swappartition(cxt) == (int) i) ? "swap" :
/* flags */               (sgi_get_bootpartition(cxt) == (int) i) ? "boot" : "    ",
/* start */               (long) fdisk_scround(cxt, start),
/* end */                 (long) fdisk_scround(cxt, start+len)-1,
/* no odd flag on end */  (long) len,
/* type id */             t->type,
/* type name */           t->name);

			fdisk_free_parttype(t);
		}
	}
	printf(_("----- Bootinfo -----\nBootfile: %s\n"
		 "----- Directory Entries -----\n"),
	       sgilabel->boot_file);
	for (i = 0 ; i < SGI_MAXVOLUMES; i++) {
		if (sgilabel->volume[i].num_bytes) {
			uint32_t start = be32_to_cpu(sgilabel->volume[i].block_num);
			uint32_t len = be32_to_cpu(sgilabel->volume[i].num_bytes);
			unsigned char *name = sgilabel->volume[i].name;
			printf(_("%2zd: %-10s sector%5u size%8u\n"),
			       i, name, (unsigned int) start,
			       (unsigned int) len);
		}
	}
}

unsigned int sgi_get_start_sector(struct fdisk_context *cxt, int i)
{
	struct sgi_disklabel *sgilabel = self_disklabel(cxt);
	return be32_to_cpu(sgilabel->partitions[i].first_block);
}

unsigned int sgi_get_num_sectors(struct fdisk_context *cxt, int i)
{
	struct sgi_disklabel *sgilabel = self_disklabel(cxt);
	return be32_to_cpu(sgilabel->partitions[i].num_blocks);
}

static int sgi_get_sysid(struct fdisk_context *cxt, int i)
{
	struct sgi_disklabel *sgilabel = self_disklabel(cxt);
	return be32_to_cpu(sgilabel->partitions[i].type);
}

int sgi_get_bootpartition(struct fdisk_context *cxt)
{
	struct sgi_disklabel *sgilabel = self_disklabel(cxt);
	return be16_to_cpu(sgilabel->root_part_num);
}

int sgi_get_swappartition(struct fdisk_context *cxt)
{
	struct sgi_disklabel *sgilabel = self_disklabel(cxt);
	return be16_to_cpu(sgilabel->swap_part_num);
}

static unsigned int
sgi_get_lastblock(struct fdisk_context *cxt) {
	return cxt->geom.heads * cxt->geom.sectors * cxt->geom.cylinders;
}

static int
sgi_check_bootfile(struct fdisk_context *cxt, const char* aFile)
{
	struct sgi_disklabel *sgilabel = self_disklabel(cxt);

	if (strlen(aFile) < 3) /* "/a\n" is minimum */ {
		fdisk_warnx(cxt, _("Invalid Bootfile! "
			 "The bootfile must be an absolute non-zero pathname,"
			 "e.g. \"/unix\" or \"/unix.save\"."));
		return 0;
	} else {
		if (strlen(aFile) > 16) {
			fdisk_warnx(cxt, _("Name of Bootfile too long:  "
				 "16 bytes maximum."));
			return 0;
		} else {
			if (aFile[0] != '/') {
				fdisk_warnx(cxt, _("Bootfile must have a "
					 "fully qualified pathname."));
				return 0;
			}
		}
	}
	if (strncmp(aFile, (char *) sgilabel->boot_file, 16)) {
		fdisk_warnx(cxt, _("Be aware, that the bootfile is not checked "
			"for existence. SGI's default is \"/unix\" and for "
			"backup \"/unix.save\"."));
		/* filename is correct and did change */
		return 1;
	}
	return 0;	/* filename did not change */
}

void
sgi_set_bootfile(struct fdisk_context *cxt)
{
	struct sgi_disklabel *sgilabel = self_disklabel(cxt);

	fdisk_info(cxt, _("The current boot file is: %s"), sgilabel->boot_file);

	if (read_chars(cxt, _("Please enter the name of the new boot file: ")) == '\n') {
		fdisk_info(cxt, _("Boot file unchanged"));
		return;
	}

	if (sgi_check_bootfile(cxt, line_ptr)) {
		size_t i = 0;
		while (i < 16) {
			if ((line_ptr[i] != '\n')	/* in principle caught again by next line */
			    &&  (strlen(line_ptr) > i))
				sgilabel->boot_file[i] = line_ptr[i];
			else
				sgilabel->boot_file[i] = 0;
			i++;
		}
		fdisk_info(cxt,_("Bootfile is changed to \"%s\"."),
		       sgilabel->boot_file);
	}
}

static int sgi_write_disklabel(struct fdisk_context *cxt)
{
	struct sgi_disklabel *sgilabel;
	struct sgi_info *info = NULL;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, SGI));

	sgilabel = self_disklabel(cxt);
	sgilabel->csum = 0;
	sgilabel->csum = cpu_to_be32(sgi_pt_checksum(sgilabel));

	assert(sgi_pt_checksum(sgilabel) == 0);

	if (lseek(cxt->dev_fd, 0, SEEK_SET) < 0)
		goto err;
	if (write_all(cxt->dev_fd, sgilabel, DEFAULT_SECTOR_SIZE))
		goto err;
	if (!strncmp((char *) sgilabel->volume[0].name, "sgilabel", 8)) {
		/*
		 * keep this habit of first writing the "sgilabel".
		 * I never tested whether it works without (AN 981002).
		 */
		int infostartblock
			= be32_to_cpu(sgilabel->volume[0].block_num);

		if (lseek(cxt->dev_fd, (off_t) infostartblock *
					DEFAULT_SECTOR_SIZE, SEEK_SET) < 0)
			goto err;
		info = sgi_new_info();
		if (!info)
			goto err;
		if (write_all(cxt->dev_fd, info, sizeof(*info)))
			goto err;
	}

	sgi_free_info(info);
	return 0;
err:
	sgi_free_info(info);
	return -errno;
}

static int
compare_start(struct fdisk_context *cxt, const void *x, const void *y) {
	/*
	 * sort according to start sectors
	 * and prefers largest partition:
	 * entry zero is entire disk entry
	 */
	unsigned int i = *(int *) x;
	unsigned int j = *(int *) y;
	unsigned int a = sgi_get_start_sector(cxt, i);
	unsigned int b = sgi_get_start_sector(cxt, j);
	unsigned int c = sgi_get_num_sectors(cxt, i);
	unsigned int d = sgi_get_num_sectors(cxt, j);

	if (a == b)
		return (d > c) ? 1 : (d == c) ? 0 : -1;
	return (a > b) ? 1 : -1;
}

static void generic_swap(void *a0, void *b0, int size)
{
	char *a = a0, *b = b0;

	for (; size > 0; --size, a++, b++) {
		char t = *a;
		*a = *b;
		*b = t;
	}
}


/* heap sort, based on Matt Mackall's linux kernel version */
static void sort(void *base0, size_t num, size_t size, struct fdisk_context *cxt,
		 int (*cmp_func)(struct fdisk_context *, const void *, const void *))
{
	/* pre-scale counters for performance */
	int i = (num/2 - 1) * size;
	size_t n = num * size, c, r;
	char *base = base0;

	/* heapify */
	for ( ; i >= 0; i -= size) {
		for (r = i; r * 2 + size < n; r  = c) {
			c = r * 2 + size;
			if (c < n - size &&
			    cmp_func(cxt, base + c, base + c + size) < 0)
				c += size;
			if (cmp_func(cxt, base + r, base + c) >= 0)
				break;
			generic_swap(base + r, base + c, size);
		}
	}

	/* sort */
	for (i = n - size; i > 0; i -= size) {
		generic_swap(base, base + i, size);
		for (r = 0; r * 2 + size < (size_t) i; r = c) {
			c = r * 2 + size;
			if (c < i - size &&
			    cmp_func(cxt, base + c, base + c + size) < 0)
				c += size;
			if (cmp_func(cxt, base + r, base + c) >= 0)
				break;
			generic_swap(base + r, base + c, size);
		}
	}
}

static int verify_disklabel(struct fdisk_context *cxt, int verbose)
{
	int Index[16];		/* list of valid partitions */
	int sortcount = 0;	/* number of used partitions, i.e. non-zero lengths */
	int entire = 0, i = 0;
	unsigned int start = 0;
	long long gap = 0;	/* count unused blocks */
	unsigned int lastblock = sgi_get_lastblock(cxt);

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, SGI));

	clear_freelist(cxt);
	for (i=0; i<16; i++) {
		if (sgi_get_num_sectors(cxt, i) != 0) {
			Index[sortcount++]=i;
			if (sgi_get_sysid(cxt, i) == SGI_TYPE_ENTIRE_DISK
			    && entire++ == 1) {
				if (verbose)
					fdisk_info(cxt, _("More than one entire "
						"disk entry present."));
			}
		}
	}
	if (sortcount == 0) {
		if (verbose)
			fdisk_info(cxt, _("No partitions defined"));
		return (lastblock > 0) ? 1 : (lastblock == 0) ? 0 : -1;
	}

	sort(Index, sortcount, sizeof(Index[0]), cxt, compare_start);

	if (sgi_get_sysid(cxt, Index[0]) == SGI_TYPE_ENTIRE_DISK) {
		if (verbose && Index[0] != 10)
			fdisk_info(cxt, _("IRIX likes when Partition 11 "
					  "covers the entire disk."));

		if (verbose && sgi_get_start_sector(cxt, Index[0]) != 0)
			fdisk_info(cxt, _("The entire disk partition should "
				"start at block 0, not at diskblock %d."),
			       sgi_get_start_sector(cxt, Index[0]));

		if (verbose && sgi_get_num_sectors(cxt, Index[0]) != lastblock)
			DBG(LABEL, dbgprint(
				"entire disk partition=%ds, but disk=%ds",
				sgi_get_num_sectors(cxt, Index[0]),
				lastblock));
		lastblock = sgi_get_num_sectors(cxt, Index[0]);
	} else if (verbose) {
		fdisk_info(cxt, _("Partition 11 should cover the entire disk."));
		DBG(LABEL, dbgprint("sysid=%d\tpartition=%d",
			       sgi_get_sysid(cxt, Index[0]), Index[0]+1));
	}
	for (i=1, start=0; i<sortcount; i++) {
		int cylsize = sgi_get_nsect(cxt) * sgi_get_ntrks(cxt);

		if (verbose && cylsize
		    && (sgi_get_start_sector(cxt, Index[i]) % cylsize) != 0)
			DBG(LABEL, dbgprint("partition %d does not start on "
					"cylinder boundary.", Index[i]+1));

		if (verbose && cylsize
		    && sgi_get_num_sectors(cxt, Index[i]) % cylsize != 0)
			DBG(LABEL, dbgprint("partition %d does not end on "
					"cylinder boundary.", Index[i]+1));

		/* We cannot handle several "entire disk" entries. */
		if (sgi_get_sysid(cxt, Index[i]) == SGI_TYPE_ENTIRE_DISK)
			continue;
		if (start > sgi_get_start_sector(cxt, Index[i])) {
			if (verbose)
				fdisk_info(cxt, _("The Partition %d and %d overlap "
					  "by %d sectors."),
				       Index[i-1]+1, Index[i]+1,
				       start - sgi_get_start_sector(cxt, Index[i]));
			if (gap >  0) gap = -gap;
			if (gap == 0) gap = -1;
		}
		if (start < sgi_get_start_sector(cxt, Index[i])) {
			if (verbose)
				fdisk_info(cxt, _("Unused gap of %8u sectors "
					    "- sectors %8u-%u"),
				       sgi_get_start_sector(cxt, Index[i]) - start,
				       start, sgi_get_start_sector(cxt, Index[i])-1);
			gap += sgi_get_start_sector(cxt, Index[i]) - start;
			add_to_freelist(cxt, start,
					sgi_get_start_sector(cxt, Index[i]));
		}
		start = sgi_get_start_sector(cxt, Index[i])
			+ sgi_get_num_sectors(cxt, Index[i]);
		/* Align free space on cylinder boundary */
		if (cylsize && start % cylsize)
			start += cylsize - (start % cylsize);

		DBG(LABEL, dbgprint("%2d:%12d\t%12d\t%12d", Index[i],
				       sgi_get_start_sector(cxt, Index[i]),
				       sgi_get_num_sectors(cxt, Index[i]),
				       sgi_get_sysid(cxt, Index[i])));
	}
	if (start < lastblock) {
		if (verbose)
			fdisk_info(cxt, _("Unused gap of %8u sectors - sectors %8u-%u"),
				lastblock - start, start, lastblock-1);
		gap += lastblock - start;
		add_to_freelist(cxt, start, lastblock);
	}
	/*
	 * Done with arithmetics. Go for details now
	 */
	if (verbose) {
		if (sgi_get_bootpartition(cxt) < 0
		    || !sgi_get_num_sectors(cxt, sgi_get_bootpartition(cxt)))
			fdisk_info(cxt, _("The boot partition does not exist."));

		if (sgi_get_swappartition(cxt) < 0
		    || !sgi_get_num_sectors(cxt, sgi_get_swappartition(cxt)))
			fdisk_info(cxt, _("The swap partition does not exist."));

		else if (sgi_get_sysid(cxt, sgi_get_swappartition(cxt)) != SGI_TYPE_SWAP
		    && sgi_get_sysid(cxt, sgi_get_swappartition(cxt)) != MBR_LINUX_SWAP_PARTITION)
			fdisk_info(cxt, _("The swap partition has no swap type."));

		if (sgi_check_bootfile(cxt, "/unix"))
			fdisk_info(cxt, _("You have chosen an unusual boot "
					  "file name."));
	}

	return (gap > 0) ? 1 : (gap == 0) ? 0 : -1;
}

static int sgi_verify_disklabel(struct fdisk_context *cxt)
{
	return verify_disklabel(cxt, 1);
}

static int
sgi_gaps(struct fdisk_context *cxt) {
	/*
	 * returned value is:
	 *  = 0 : disk is properly filled to the rim
	 *  < 0 : there is an overlap
	 *  > 0 : there is still some vacant space
	 */
	return verify_disklabel(cxt, 0);
}


/* returns partition index of first entry marked as entire disk */
static int
sgi_entire(struct fdisk_context *cxt) {
	int i;

	for (i=0; i<16; i++)
		if (sgi_get_sysid(cxt, i) == SGI_TYPE_ENTIRE_DISK)
			return i;
	return -1;
}

static int sgi_set_partition(struct fdisk_context *cxt, size_t i,
			     unsigned int start, unsigned int length, int sys)
{
	struct sgi_disklabel *sgilabel;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, SGI));

	sgilabel = self_disklabel(cxt);
	sgilabel->partitions[i].type = cpu_to_be32(sys);
	sgilabel->partitions[i].num_blocks = cpu_to_be32(length);
	sgilabel->partitions[i].first_block = cpu_to_be32(start);

	fdisk_label_set_changed(cxt->label, 1);

	if (sgi_gaps(cxt) < 0)	/* rebuild freelist */
		fdisk_warnx(cxt, _("Partition overlap on the disk."));
	if (length) {
		struct fdisk_parttype *t = fdisk_get_parttype_from_code(cxt, sys);
		fdisk_info_new_partition(cxt, i + 1, start, start + length, t);
	}

	return 0;
}

static void
sgi_set_entire(struct fdisk_context *cxt) {
	size_t n;

	for (n = 10; n < cxt->label->nparts_max; n++) {
		if (!sgi_get_num_sectors(cxt, n)) {
			sgi_set_partition(cxt, n, 0, sgi_get_lastblock(cxt), SGI_TYPE_ENTIRE_DISK);
			break;
		}
	}
}

static
void
sgi_set_volhdr(struct fdisk_context *cxt)
{
	size_t n;

	for (n = 8; n < cxt->label->nparts_max; n++) {
		if (!sgi_get_num_sectors(cxt, n)) {
			/*
			 * Choose same default volume header size
			 * as IRIX fx uses.
			 */
			if (4096 < sgi_get_lastblock(cxt))
				sgi_set_partition(cxt, n, 0, 4096, SGI_TYPE_VOLHDR);
			break;
		}
	}
}

static int sgi_delete_partition(struct fdisk_context *cxt, size_t partnum)
{
	int rc;

	assert(cxt);
	assert(cxt->label);

	if (partnum > cxt->label->nparts_max)
		return -EINVAL;

	rc = sgi_set_partition(cxt, partnum, 0, 0, 0);

	cxt->label->nparts_cur = count_used_partitions(cxt);

	return rc;
}

static int sgi_add_partition(struct fdisk_context *cxt,
		size_t n,
		struct fdisk_parttype *t)
{
	struct fdisk_sgi_label *sgi;
	char mesg[256];
	unsigned int first=0, last=0;
	int sys = t ? t->type : SGI_TYPE_XFS;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, SGI));

	if (n == 10)
		sys = SGI_TYPE_ENTIRE_DISK;
	else if (n == 8)
		sys = 0;

	sgi = self_label(cxt);

	if (sgi_get_num_sectors(cxt, n)) {
		fdisk_warnx(cxt, _("Partition %zd is already defined.  Delete "
			 "it before re-adding it."), n + 1);
		return -EINVAL;
	}
	if (sgi_entire(cxt) == -1 &&  sys != SGI_TYPE_ENTIRE_DISK) {
		fdisk_info(cxt, _("Attempting to generate entire disk entry automatically."));
		sgi_set_entire(cxt);
		sgi_set_volhdr(cxt);
	}
	if (sgi_gaps(cxt) == 0 && sys != SGI_TYPE_ENTIRE_DISK) {
		fdisk_warnx(cxt, _("The entire disk is already covered with partitions."));
		return -EINVAL;
	}
	if (sgi_gaps(cxt) < 0) {
		fdisk_warnx(cxt, _("You got a partition overlap on the disk. Fix it first!"));
		return -EINVAL;
	}
	snprintf(mesg, sizeof(mesg), _("First %s"),
			fdisk_context_get_unit(cxt, SINGULAR));
	for (;;) {
		if (sys == SGI_TYPE_ENTIRE_DISK) {
			last = sgi_get_lastblock(cxt);
			first = read_int(cxt, 0, 0, last-1, 0, mesg);
			if (first != 0)
				fdisk_info(cxt, _("It is highly recommended that "
					"eleventh partition covers the entire "
					"disk and is of type `SGI volume'"));
		} else {
			first = sgi->freelist[0].first;
			last  = sgi->freelist[0].last;
			first = read_int(cxt, scround(cxt, first),
					      scround(cxt, first),
					      scround(cxt, last) - 1,
					 0, mesg);
		}
		if (fdisk_context_use_cylinders(cxt))
			first *= fdisk_context_get_units_per_sector(cxt);
		/*else
			first = first; * align to cylinder if you know how ... */
		if (!last)
			last = is_in_freelist(cxt, first);
		if (last == 0)
			fdisk_warnx(cxt, _("You will get a partition overlap "
				"on the disk. Fix it first!"));
		else
			break;
	}
	snprintf(mesg, sizeof(mesg), _(" Last %s"),
			fdisk_context_get_unit(cxt, SINGULAR));
	last = read_int(cxt, scround(cxt, first),
			scround(cxt, last)-1,
			scround(cxt, last)-1,
			scround(cxt, first), mesg)+1;
	if (fdisk_context_use_cylinders(cxt))
		last *= fdisk_context_get_units_per_sector(cxt);
	/*else
		last = last; * align to cylinder if You know how ... */
	if (sys == SGI_TYPE_ENTIRE_DISK && (first != 0 || last != sgi_get_lastblock(cxt)))
		fdisk_info(cxt, _("It is highly recommended that eleventh "
			"partition covers the entire disk and is of type "
			"`SGI volume'"));

	sgi_set_partition(cxt, n, first, last-first, sys);
	cxt->label->nparts_cur = count_used_partitions(cxt);

	return 0;
}

static int sgi_create_disklabel(struct fdisk_context *cxt)
{
	struct fdisk_sgi_label *sgi;
	struct sgi_disklabel *sgilabel;
	struct hd_geometry geometry;
	struct {
		unsigned int start;
		unsigned int nsect;
		int sysid;
	} old[4];
	int i=0;
	sector_t llsectors;
	int res; 		/* the result from the ioctl */
	int sec_fac; 		/* the sector factor */

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, SGI));

	sec_fac = cxt->sector_size / 512;	/* determine the sector factor */

	fdisk_info(cxt,	_("Building a new SGI disklabel."));

	res = blkdev_get_sectors(cxt->dev_fd, &llsectors);

#ifdef HDIO_GETGEO
	/* TODO: it seems unnecessary, geometry is already set in the context */
	if (ioctl(cxt->dev_fd, HDIO_GETGEO, &geometry) < 0) {
		fdisk_warn(cxt, _("HDIO_GETGEO ioctl failed on %s"), cxt->dev_path);
		return -1;
	}
	cxt->geom.heads = geometry.heads;
	cxt->geom.sectors = geometry.sectors;
	if (res == 0) {
		/* the get device size ioctl was successful */
	        sector_t llcyls;
		llcyls = llsectors / (cxt->geom.heads * cxt->geom.sectors * sec_fac);
		cxt->geom.cylinders = llcyls;
		if (cxt->geom.cylinders != llcyls)	/* truncated? */
			cxt->geom.cylinders = ~0;
	} else {
		/* otherwise print error and use truncated version */
		cxt->geom.cylinders = geometry.cylinders;
		fdisk_warnx(cxt,
			_("Warning:  BLKGETSIZE ioctl failed on %s.  "
			  "Using geometry cylinder value of %llu."
			  "This value may be truncated for devices"
			  " > 33.8 GB."), cxt->dev_path, cxt->geom.cylinders);
	}
#endif
	fdisk_zeroize_firstsector(cxt);
	sgi = (struct fdisk_sgi_label *) cxt->label;
	sgi->header = (struct sgi_disklabel *) cxt->firstsector;

	sgilabel = sgi->header;

	sgilabel->magic = cpu_to_be32(SGI_LABEL_MAGIC);
	sgilabel->root_part_num = cpu_to_be16(0);
	sgilabel->swap_part_num = cpu_to_be16(1);

	/* sizeof(sgilabel->boot_file) = 16 > 6 */
	memset(sgilabel->boot_file, 0, 16);
	strcpy((char *) sgilabel->boot_file, "/unix");

	sgilabel->devparam.skew			= (0);
	sgilabel->devparam.gap1			= (0);
	sgilabel->devparam.gap2			= (0);
	sgilabel->devparam.sparecyl			= (0);
	sgilabel->devparam.pcylcount	= cpu_to_be16(geometry.cylinders);
	sgilabel->devparam.head_vol0	= cpu_to_be16(0);
	sgilabel->devparam.ntrks	= cpu_to_be16(geometry.heads);
	/* tracks/cylinder (heads) */
	sgilabel->devparam.cmd_tag_queue_depth	= (0);
	sgilabel->devparam.unused0			= (0);
	sgilabel->devparam.unused1	= cpu_to_be16(0);
	sgilabel->devparam.nsect	= cpu_to_be16(geometry.sectors);
	/* sectors/track */
	sgilabel->devparam.bytes	= cpu_to_be16(cxt->sector_size);
	sgilabel->devparam.ilfact	= cpu_to_be16(1);
	sgilabel->devparam.flags	= cpu_to_be32(
			SGI_DEVPARAM_TRACK_FWD
			| SGI_DEVPARAM_IGNORE_ERRORS
			| SGI_DEVPARAM_RESEEK);
	sgilabel->devparam.datarate	= cpu_to_be32(0);
	sgilabel->devparam.retries_on_error	= cpu_to_be32(1);
	sgilabel->devparam.ms_per_word		= cpu_to_be32(0);
	sgilabel->devparam.xylogics_gap1	= cpu_to_be16(0);
	sgilabel->devparam.xylogics_syncdelay	= cpu_to_be16(0);
	sgilabel->devparam.xylogics_readdelay	= cpu_to_be16(0);
	sgilabel->devparam.xylogics_gap2	= cpu_to_be16(0);
	sgilabel->devparam.xylogics_readgate	= cpu_to_be16(0);
	sgilabel->devparam.xylogics_writecont	= cpu_to_be16(0);

	memset(&(sgilabel->volume), 0,
			sizeof(struct sgi_volume) * SGI_MAXVOLUMES);
	memset(&(sgilabel->partitions), 0,
			sizeof(struct sgi_partition) * SGI_MAXPARTITIONS);
	cxt->label->nparts_max = SGI_MAXPARTITIONS;
	sgi_set_entire(cxt);
	sgi_set_volhdr(cxt);

	for (i = 0; i < 4; i++) {
		if (old[i].sysid) {
			sgi_set_partition(cxt, i, old[i].start, old[i].nsect, old[i].sysid);
		}
	}

	cxt->label->nparts_cur = count_used_partitions(cxt);
	return 0;
}

void
sgi_set_ilfact(void)
{
	/* do nothing in the beginning */
}

void
sgi_set_rspeed(void)
{
	/* do nothing in the beginning */
}

void
sgi_set_pcylcount(void)
{
	/* do nothing in the beginning */
}

void
sgi_set_xcyl(void)
{
	/* do nothing in the beginning */
}

void
sgi_set_ncyl(void)
{
	/* do nothing in the beginning */
}

/* _____________________________________________________________
 */

static struct fdisk_parttype *sgi_get_parttype(struct fdisk_context *cxt, size_t n)
{
	struct fdisk_parttype *t;

	if (n >= cxt->label->nparts_max)
		return NULL;

	t = fdisk_get_parttype_from_code(cxt, sgi_get_sysid(cxt, n));
	if (!t)
		t = fdisk_new_unknown_parttype(sgi_get_sysid(cxt, n), NULL);
	return t;
}

static int sgi_set_parttype(struct fdisk_context *cxt,
		size_t i,
		struct fdisk_parttype *t)
{
	struct sgi_disklabel *sgilabel;

	if (i >= cxt->label->nparts_max || !t || t->type > UINT32_MAX)
		return -EINVAL;

	if (sgi_get_num_sectors(cxt, i) == 0)	/* caught already before, ... */ {
		fdisk_warnx(cxt, _("Sorry, only for non-empty partitions you can change the tag."));
		return -EINVAL;
	}

	if ((i == 10 && t->type != SGI_TYPE_ENTIRE_DISK)
	    || (i == 8 && t->type != 0))
		fdisk_info(cxt, _("Consider leaving partition 9 as volume header (0), "
			 "and partition 11 as entire volume (6), as IRIX "
			 "expects it."));

	if (((t->type != SGI_TYPE_ENTIRE_DISK) && (t->type != SGI_TYPE_VOLHDR))
	    && (sgi_get_start_sector(cxt, i) < 1)) {
		read_chars(cxt,
			_("It is highly recommended that the partition at offset 0\n"
			  "is of type \"SGI volhdr\", the IRIX system will rely on it to\n"
			  "retrieve from its directory standalone tools like sash and fx.\n"
			  "Only the \"SGI volume\" entire disk section may violate this.\n"
			  "Type YES if you are sure about tagging this partition differently.\n"));
		if (strcmp (line_ptr, _("YES\n")))
			return 1;
	}

	sgilabel = self_disklabel(cxt);
	sgilabel->partitions[i].type = cpu_to_be32(t->type);
	return 0;
}


static int sgi_get_partition_status(
		struct fdisk_context *cxt,
		size_t i,
		int *status)
{

	assert(cxt);
	assert(fdisk_is_disklabel(cxt, SGI));

	if (!status || i >= cxt->label->nparts_max)
		return -EINVAL;

	*status = FDISK_PARTSTAT_NONE;

	if (sgi_get_num_sectors(cxt, i))
		*status = FDISK_PARTSTAT_USED;

	return 0;
}

static int sgi_toggle_partition_flag(struct fdisk_context *cxt, size_t i, unsigned long flag)
{
	struct sgi_disklabel *sgilabel;
	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, SGI));

	if (i >= cxt->label->nparts_max)
		return -EINVAL;

	sgilabel = self_disklabel(cxt);

	switch (flag) {
	case SGI_FLAG_BOOT:
		sgilabel->root_part_num =
			be16_to_cpu(sgilabel->root_part_num) == i ?
			0 : cpu_to_be16(i);
		fdisk_label_set_changed(cxt->label, 1);
		break;
	case SGI_FLAG_SWAP:
		sgilabel->swap_part_num =
			be16_to_cpu(sgilabel->swap_part_num) == i ?
			0 : cpu_to_be16(i);
		fdisk_label_set_changed(cxt->label, 1);
		break;
	default:
		return 1;
	}

	return 0;
}

static const struct fdisk_label_operations sgi_operations =
{
	.probe		= sgi_probe_label,
	.write		= sgi_write_disklabel,
	.verify		= sgi_verify_disklabel,
	.create		= sgi_create_disklabel,
	.part_add	= sgi_add_partition,
	.part_delete	= sgi_delete_partition,
	.part_get_type	= sgi_get_parttype,
	.part_set_type	= sgi_set_parttype,

	.part_get_status = sgi_get_partition_status,
	.part_toggle_flag = sgi_toggle_partition_flag
};

/*
 * allocates SGI label driver
 */
struct fdisk_label *fdisk_new_sgi_label(struct fdisk_context *cxt)
{
	struct fdisk_label *lb;
	struct fdisk_sgi_label *sgi;

	assert(cxt);

	sgi = calloc(1, sizeof(*sgi));
	if (!sgi)
		return NULL;

	/* initialize generic part of the driver */
	lb = (struct fdisk_label *) sgi;
	lb->name = "sgi";
	lb->id = FDISK_DISKLABEL_SGI;
	lb->op = &sgi_operations;
	lb->parttypes = sgi_parttypes;
	lb->nparttypes = ARRAY_SIZE(sgi_parttypes);

	return lb;
}
