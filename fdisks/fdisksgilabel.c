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

#include <endian.h>
#include "nls.h"
#include "xalloc.h"

#include "blkdev.h"

#include "common.h"
#include "fdisk.h"
#include "fdisksgilabel.h"
#include "fdiskdoslabel.h"


/*
 * in-memory fdisk SGI stuff
 */
struct fdisk_sgi_label {
	struct fdisk_label	head;		/* generic part */
};


static	int     other_endian = 0;
static  short volumes=1;

static sgiinfo *fill_sgiinfo(void);

/*
 * only dealing with free blocks here
 */

typedef struct { unsigned int first; unsigned int last; } freeblocks;
static freeblocks freelist[17]; /* 16 partitions can produce 17 vacant slots */

static void
setfreelist(int i, unsigned int f, unsigned int l) {
	if (i < 17) {
		freelist[i].first = f;
		freelist[i].last = l;
	}
}

static void
add2freelist(unsigned int f, unsigned int l) {
	int i = 0;
	for ( ; i < 17 ; i++)
		if (freelist[i].last == 0)
			break;
	setfreelist(i, f, l);
}

static void
clearfreelist(void) {
	int i;

	for (i = 0; i < 17 ; i++)
		setfreelist(i, 0, 0);
}

static unsigned int
isinfreelist(unsigned int b) {
	int i;

	for (i = 0; i < 17 ; i++)
		if (freelist[i].first <= b && freelist[i].last >= b)
			return freelist[i].last;
	return 0;
}
	/* return last vacant block of this stride (never 0). */
	/* the '>=' is not quite correct, but simplifies the code */
/*
 * end of free blocks section
 */
static struct fdisk_parttype sgi_parttypes[] = {
	{SGI_VOLHDR,	N_("SGI volhdr")},
	{0x01,		N_("SGI trkrepl")},
	{0x02,		N_("SGI secrepl")},
	{SGI_SWAP,	N_("SGI raw")},
	{0x04,		N_("SGI bsd")},
	{0x05,		N_("SGI sysv")},
	{ENTIRE_DISK,	N_("SGI volume")},
	{SGI_EFS,	N_("SGI efs")},
	{0x08,		N_("SGI lvol")},
	{0x09,		N_("SGI rlvol")},
	{SGI_XFS,	N_("SGI xfs")},
	{SGI_XFSLOG,	N_("SGI xfslog")},
	{SGI_XLV,	N_("SGI xlv")},
	{SGI_XVM,	N_("SGI xvm")},
	{LINUX_SWAP,	N_("Linux swap")},
	{LINUX_NATIVE,	N_("Linux native")},
	{LINUX_LVM,	N_("Linux LVM")},
	{LINUX_RAID,	N_("Linux RAID")},
	{0, NULL }
};

static int
sgi_get_nsect(struct fdisk_context *cxt) {
	return SSWAP16(sgilabel->devparam.nsect);
}

static int
sgi_get_ntrks(struct fdisk_context *cxt) {
	return SSWAP16(sgilabel->devparam.ntrks);
}

static unsigned int
two_s_complement_32bit_sum(unsigned int *base, int size /* in bytes */) {
	int i = 0;
	unsigned int sum = 0;

	size /= sizeof(unsigned int);
	for (i = 0; i < size; i++)
		sum -= SSWAP32(base[i]);
	return sum;
}

static size_t count_used_partitions(struct fdisk_context *cxt)
{
	size_t i, ct = 0;

	for (i = 0; i < cxt->label->nparts_max; i++)
		ct += sgi_get_num_sectors(cxt, i) > 0;

	return ct;
}

static int
sgi_probe_label(struct fdisk_context *cxt)
{
	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, SGI));

	if (sizeof(sgilabel) > 512) {
		fprintf(stderr,
			_("According to MIPS Computer Systems, Inc the "
			  "Label must not contain more than 512 bytes\n"));
		exit(1);
	}

	if (sgilabel->magic != SGI_LABEL_MAGIC &&
	    sgilabel->magic != SGI_LABEL_MAGIC_SWAPPED) {
		other_endian = 0;
		return 0;
	}

	other_endian = (sgilabel->magic == SGI_LABEL_MAGIC_SWAPPED);
	/*
	 * test for correct checksum
	 */
	if (two_s_complement_32bit_sum((unsigned int*)sgilabel,
				       sizeof(*sgilabel))) {
		fprintf(stderr,
			_("Detected sgi disklabel with wrong checksum.\n"));
	}
	cxt->label->nparts_max = 16;
	cxt->label->nparts_cur = count_used_partitions(cxt);
	volumes = 15;
	return 1;
}

void
sgi_list_table(struct fdisk_context *cxt, int xtra) {
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
		       SSWAP16(sgiparam.pcylcount),
		       (int) sgiparam.sparecyl, SSWAP16(sgiparam.ilfact),
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
/* device */              partname(cxt->dev_path, kpi, w+2),
/* flags */               (sgi_get_swappartition(cxt) == (int) i) ? "swap" :
/* flags */               (sgi_get_bootpartition(cxt) == (int) i) ? "boot" : "    ",
/* start */               (long) scround(cxt, start),
/* end */                 (long) scround(cxt, start+len)-1,
/* no odd flag on end */  (long) len,
/* type id */             t->type,
/* type name */           t->name);

			fdisk_free_parttype(t);
		}
	}
	printf(_("----- Bootinfo -----\nBootfile: %s\n"
		 "----- Directory Entries -----\n"),
	       sgilabel->boot_file);
	for (i = 0 ; i < (size_t) volumes; i++) {
		if (sgilabel->directory[i].vol_file_size) {
			uint32_t start = SSWAP32(sgilabel->directory[i].vol_file_start);
			uint32_t len = SSWAP32(sgilabel->directory[i].vol_file_size);
			unsigned char *name = sgilabel->directory[i].vol_file_name;
			printf(_("%2zd: %-10s sector%5u size%8u\n"),
			       i, name, (unsigned int) start,
			       (unsigned int) len);
		}
	}
}

unsigned int
sgi_get_start_sector(struct fdisk_context *cxt, int i) {
	return SSWAP32(sgilabel->partitions[i].start_sector);
}

unsigned int
sgi_get_num_sectors(struct fdisk_context *cxt, int i) {
	return SSWAP32(sgilabel->partitions[i].num_sectors);
}

static int
sgi_get_sysid(struct fdisk_context *cxt, int i)
{
	return SSWAP32(sgilabel->partitions[i].id);
}

int
sgi_get_bootpartition(struct fdisk_context *cxt)
{
	return (short) SSWAP16(sgilabel->boot_part);
}

int
sgi_get_swappartition(struct fdisk_context *cxt)
{
	return (short) SSWAP16(sgilabel->swap_part);
}

static unsigned int
sgi_get_lastblock(struct fdisk_context *cxt) {
	return cxt->geom.heads * cxt->geom.sectors * cxt->geom.cylinders;
}

static int
sgi_check_bootfile(struct fdisk_context *cxt, const char* aFile)
{
	if (strlen(aFile) < 3) /* "/a\n" is minimum */ {
		printf(_("\nInvalid Bootfile!\n"
			 "\tThe bootfile must be an absolute non-zero pathname,\n"
			 "\te.g. \"/unix\" or \"/unix.save\".\n"));
		return 0;
	} else {
		if (strlen(aFile) > 16) {
			printf(_("\n\tName of Bootfile too long:  "
				 "16 bytes maximum.\n"));
			return 0;
		} else {
			if (aFile[0] != '/') {
				printf(_("\n\tBootfile must have a "
					 "fully qualified pathname.\n"));
				return 0;
			}
		}
	}
	if (strncmp(aFile, (char *) sgilabel->boot_file, 16)) {
		printf(_("\n\tBe aware, that the bootfile is not checked for existence.\n\t"
			 "SGI's default is \"/unix\" and for backup \"/unix.save\".\n"));
		/* filename is correct and did change */
		return 1;
	}
	return 0;	/* filename did not change */
}

void
sgi_set_bootfile(struct fdisk_context *cxt)
{
	printf(_("\nThe current boot file is: %s\n"), sgilabel->boot_file);
	if (read_chars(cxt, _("Please enter the name of the new boot file: ")) == '\n') {
		printf(_("Boot file unchanged\n"));
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
		printf(_("\n\tBootfile is changed to \"%s\".\n"),
		       sgilabel->boot_file);
	}
}

void
create_sgiinfo(struct fdisk_context *cxt) {
	/* I keep SGI's habit to write the sgilabel to the second block */
	sgilabel->directory[0].vol_file_start = SSWAP32(2);
	sgilabel->directory[0].vol_file_size = SSWAP32(sizeof(sgiinfo));
	strncpy((char *) sgilabel->directory[0].vol_file_name, "sgilabel", 8);
}


static int sgi_write_disklabel(struct fdisk_context *cxt)
{

	sgiinfo *info = NULL;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, SGI));

	sgilabel->csum = 0;
	sgilabel->csum = SSWAP32(two_s_complement_32bit_sum(
		(unsigned int*)sgilabel,
		sizeof(*sgilabel)));
	assert(two_s_complement_32bit_sum(
		(unsigned int*)sgilabel, sizeof(*sgilabel)) == 0);
	if (lseek(cxt->dev_fd, 0, SEEK_SET) < 0)
		goto err;
	if (write(cxt->dev_fd, sgilabel, SECTOR_SIZE) != SECTOR_SIZE)
		goto err;
	if (!strncmp((char *) sgilabel->directory[0].vol_file_name, "sgilabel", 8)) {
		/*
		 * keep this habit of first writing the "sgilabel".
		 * I never tested whether it works without (AN 981002).
		 */
		int infostartblock
			= SSWAP32(sgilabel->directory[0].vol_file_start);

		if (lseek(cxt->dev_fd, (off_t) infostartblock *
						SECTOR_SIZE, SEEK_SET) < 0)
			goto err;

		info = fill_sgiinfo();
		if (!info)
			goto err;

		if (write(cxt->dev_fd, info, SECTOR_SIZE) != SECTOR_SIZE)
			goto err;
	}

	free(info);
	return 0;
err:
	free(info);
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

	clearfreelist();
	for (i=0; i<16; i++) {
		if (sgi_get_num_sectors(cxt, i) != 0) {
			Index[sortcount++]=i;
			if (sgi_get_sysid(cxt, i) == ENTIRE_DISK
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

	if (sgi_get_sysid(cxt, Index[0]) == ENTIRE_DISK) {
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
		if (sgi_get_sysid(cxt, Index[i]) == ENTIRE_DISK)
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
			add2freelist(start, sgi_get_start_sector(cxt, Index[i]));
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
		add2freelist(start, lastblock);
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

		else if (sgi_get_sysid(cxt, sgi_get_swappartition(cxt)) != SGI_SWAP
		    && sgi_get_sysid(cxt, sgi_get_swappartition(cxt)) != LINUX_SWAP)
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
		if (sgi_get_sysid(cxt, i) == SGI_VOLUME)
			return i;
	return -1;
}

static int sgi_set_partition(struct fdisk_context *cxt, size_t i,
			     unsigned int start, unsigned int length, int sys)
{
	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, SGI));

	sgilabel->partitions[i].id = SSWAP32(sys);
	sgilabel->partitions[i].num_sectors = SSWAP32(length);
	sgilabel->partitions[i].start_sector = SSWAP32(start);

	fdisk_label_set_changed(cxt->label, 1);

	if (sgi_gaps(cxt) < 0)	/* rebuild freelist */
		printf(_("Partition overlap on the disk.\n"));
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
			sgi_set_partition(cxt, n, 0, sgi_get_lastblock(cxt), SGI_VOLUME);
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
				sgi_set_partition(cxt, n, 0, 4096, SGI_VOLHDR);
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
	char mesg[256];
	unsigned int first=0, last=0;
	int sys = t ? t->type : SGI_XFS;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, SGI));

	if (n == 10)
		sys = SGI_VOLUME;
	else if (n == 8)
		sys = 0;

	if (sgi_get_num_sectors(cxt, n)) {
		printf(_("Partition %zd is already defined.  Delete "
			 "it before re-adding it.\n"), n + 1);
		return -EINVAL;
	}
	if ((sgi_entire(cxt) == -1)
	    &&  (sys != SGI_VOLUME)) {
		printf(_("Attempting to generate entire disk entry automatically.\n"));
		sgi_set_entire(cxt);
		sgi_set_volhdr(cxt);
	}
	if ((sgi_gaps(cxt) == 0) &&  (sys != SGI_VOLUME)) {
		printf(_("The entire disk is already covered with partitions.\n"));
		return -EINVAL;
	}
	if (sgi_gaps(cxt) < 0) {
		printf(_("You got a partition overlap on the disk. Fix it first!\n"));
		return -EINVAL;
	}
	snprintf(mesg, sizeof(mesg), _("First %s"),
			fdisk_context_get_unit(cxt, SINGULAR));
	for (;;) {
		if (sys == SGI_VOLUME) {
			last = sgi_get_lastblock(cxt);
			first = read_int(cxt, 0, 0, last-1, 0, mesg);
			if (first != 0) {
				printf(_("It is highly recommended that eleventh partition\n"
					 "covers the entire disk and is of type `SGI volume'\n"));
			}
		} else {
			first = freelist[0].first;
			last  = freelist[0].last;
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
			last = isinfreelist(first);
		if (last == 0) {
			printf(_("You will get a partition overlap on the disk. "
				 "Fix it first!\n"));
		} else
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
	if ((sys == SGI_VOLUME) && (first != 0 || last != sgi_get_lastblock(cxt)))
		printf(_("It is highly recommended that eleventh partition\n"
			 "covers the entire disk and is of type `SGI volume'\n"));
	sgi_set_partition(cxt, n, first, last-first, sys);

	cxt->label->nparts_cur = count_used_partitions(cxt);

	return 0;
}

static int sgi_create_disklabel(struct fdisk_context *cxt)
{
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

	fprintf(stderr,
		_("Building a new SGI disklabel.\n"));

	other_endian = (BYTE_ORDER == LITTLE_ENDIAN);

	res = blkdev_get_sectors(cxt->dev_fd, &llsectors);

#ifdef HDIO_GETGEO
	if (ioctl(cxt->dev_fd, HDIO_GETGEO, &geometry) < 0)
		err(EXIT_FAILURE, _("HDIO_GETGEO ioctl failed on %s"), cxt->dev_path);

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
		fprintf(stderr,
			_("Warning:  BLKGETSIZE ioctl failed on %s.  "
			  "Using geometry cylinder value of %llu.\n"
			  "This value may be truncated for devices"
			  " > 33.8 GB.\n"), cxt->dev_path, cxt->geom.cylinders);
	}
#endif
	/*
	 * Convert old MBR to SGI label, make it DEPRECATED, this feature
	 * has to be handled in by any top-level fdisk command.
	 *
	for (i = 0; i < 4; i++) {
		old[i].sysid = 0;
		if (mbr_is_valid_magic(cxt->firstsector)) {
			if (get_part_table(i)->sys_ind) {
				old[i].sysid = get_part_table(i)->sys_ind;
				old[i].start = get_start_sect(get_part_table(i));
				old[i].nsect = get_nr_sects(get_part_table(i));
				if (debug)
					printf(_("ID=%02x\tSTART=%d\tLENGTH=%d\n"),
					       old[i].sysid, old[i].start, old[i].nsect);
			}
		}
	}

	for (i = 0; i < 4; i++)
		if (old[i].sysid) {
			printf(_("Trying to keep parameters of partitions already set.\n"));
			break;
		}
	*/

	fdisk_zeroize_firstsector(cxt);
	sgilabel->magic = SSWAP32(SGI_LABEL_MAGIC);
	sgilabel->boot_part = SSWAP16(0);
	sgilabel->swap_part = SSWAP16(1);

	/* sizeof(sgilabel->boot_file) = 16 > 6 */
	memset(sgilabel->boot_file, 0, 16);
	strcpy((char *) sgilabel->boot_file, "/unix");

	sgilabel->devparam.skew			= (0);
	sgilabel->devparam.gap1			= (0);
	sgilabel->devparam.gap2			= (0);
	sgilabel->devparam.sparecyl			= (0);
	sgilabel->devparam.pcylcount		= SSWAP16(geometry.cylinders);
	sgilabel->devparam.head_vol0		= SSWAP16(0);
	sgilabel->devparam.ntrks			= SSWAP16(geometry.heads);
	/* tracks/cylinder (heads) */
	sgilabel->devparam.cmd_tag_queue_depth	= (0);
	sgilabel->devparam.unused0			= (0);
	sgilabel->devparam.unused1			= SSWAP16(0);
	sgilabel->devparam.nsect			= SSWAP16(geometry.sectors);
	/* sectors/track */
	sgilabel->devparam.bytes			= SSWAP16(cxt->sector_size);
	sgilabel->devparam.ilfact			= SSWAP16(1);
	sgilabel->devparam.flags			= SSWAP32(TRACK_FWD|\
								  IGNORE_ERRORS|RESEEK);
	sgilabel->devparam.datarate			= SSWAP32(0);
	sgilabel->devparam.retries_on_error		= SSWAP32(1);
	sgilabel->devparam.ms_per_word		= SSWAP32(0);
	sgilabel->devparam.xylogics_gap1		= SSWAP16(0);
	sgilabel->devparam.xylogics_syncdelay	= SSWAP16(0);
	sgilabel->devparam.xylogics_readdelay	= SSWAP16(0);
	sgilabel->devparam.xylogics_gap2		= SSWAP16(0);
	sgilabel->devparam.xylogics_readgate	= SSWAP16(0);
	sgilabel->devparam.xylogics_writecont	= SSWAP16(0);
	memset(&(sgilabel->directory), 0, sizeof(struct volume_directory)*15);
	memset(&(sgilabel->partitions), 0, sizeof(struct sgi_partition)*16);
	cxt->label->nparts_max = 16;
	volumes    = 15;
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

static sgiinfo *fill_sgiinfo(void)
{
	sgiinfo *info = xcalloc(1, sizeof(sgiinfo));

	if (!info)
		return NULL;

	info->magic=SSWAP32(SGI_INFO_MAGIC);
	info->b1=SSWAP32(-1);
	info->b2=SSWAP16(-1);
	info->b3=SSWAP16(1);
	/* You may want to replace this string !!!!!!! */
	strcpy((char *) info->scsi_string, "IBM OEM 0662S12         3 30");
	strcpy((char *) info->serial, "0000");
	info->check1816 = SSWAP16(18*256 +16);
	strcpy((char *) info->installer, "Sfx version 5.3, Oct 18, 1994");
	return info;
}

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
	if (i >= cxt->label->nparts_max || !t || t->type > UINT32_MAX)
		return -EINVAL;

	if (sgi_get_num_sectors(cxt, i) == 0)	/* caught already before, ... */ {
		printf(_("Sorry, only for non-empty partitions you can change the tag.\n"));
		return -EINVAL;
	}

	if ((i == 10 && t->type != ENTIRE_DISK) || (i == 8 && t->type != 0))
		printf(_("Consider leaving partition 9 as volume header (0), "
			 "and partition 11 as entire volume (6), as IRIX "
			 "expects it.\n\n"));

	if (((t->type != ENTIRE_DISK) && (t->type != SGI_VOLHDR))
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
	sgilabel->partitions[i].id = SSWAP32(t->type);
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
	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_disklabel(cxt, SGI));

	if (i >= cxt->label->nparts_max)
		return -EINVAL;

	switch (flag) {
	case SGI_FLAG_BOOT:
		sgilabel->boot_part =
			(uint16_t) sgilabel->boot_part == SSWAP16(i) ? 0 : SSWAP16(i);
		break;
	case SGI_FLAG_SWAP:
		sgilabel->swap_part =
			(uint16_t) sgilabel->swap_part == SSWAP16(i) ? 0 : SSWAP16(i);
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
