/*
 * Copyright (C) 2013 Karel Zak <kzak@redhat.com>
 *
 * Based on original code from fdisk:
 *   Jakub Jelinek (jj@sunsite.mff.cuni.cz), July 1996
 *   Merged with fdisk for other architectures, aeb, June 1998.
 *   Arnaldo Carvalho de Melo <acme@conectiva.com.br> Mar 1999, Internationalization
 */
#include <stdio.h>		/* stderr */
#include <stdlib.h>		/* qsort */
#include <string.h>		/* strstr */
#include <unistd.h>		/* write */
#include <sys/ioctl.h>		/* ioctl */

#include "nls.h"
#include "blkdev.h"
#include "bitops.h"

#include "fdiskP.h"
#include "pt-sun.h"
#include "all-io.h"


/**
 * SECTION: sun
 * @title: SUN
 * @short_description: disk label specific functions
 *
 */

/*
 * in-memory fdisk SUN stuff
 */
struct fdisk_sun_label {
	struct fdisk_label	head;		/* generic part */
	struct sun_disklabel   *header;		/* on-disk data (pointer to cxt->firstsector) */
};

static struct fdisk_parttype sun_parttypes[] = {
	{SUN_TAG_UNASSIGNED, N_("Unassigned")},
	{SUN_TAG_BOOT, N_("Boot")},
	{SUN_TAG_ROOT, N_("SunOS root")},
	{SUN_TAG_SWAP, N_("SunOS swap")},
	{SUN_TAG_USR, N_("SunOS usr")},
	{SUN_TAG_WHOLEDISK, N_("Whole disk")},
	{SUN_TAG_STAND, N_("SunOS stand")},
	{SUN_TAG_VAR, N_("SunOS var")},
	{SUN_TAG_HOME, N_("SunOS home")},
	{SUN_TAG_ALTSCTR, N_("SunOS alt sectors")},
	{SUN_TAG_CACHE, N_("SunOS cachefs")},
	{SUN_TAG_RESERVED, N_("SunOS reserved")},
	{SUN_TAG_LINUX_SWAP, N_("Linux swap")},
	{SUN_TAG_LINUX_NATIVE, N_("Linux native")},
	{SUN_TAG_LINUX_LVM, N_("Linux LVM")},
	{SUN_TAG_LINUX_RAID, N_("Linux raid autodetect")},
	{ 0, NULL }
};

/* return poiter buffer with on-disk data */
static inline struct sun_disklabel *self_disklabel(struct fdisk_context *cxt)
{
	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, SUN));

	return ((struct fdisk_sun_label *) cxt->label)->header;
}

/* return in-memory sun fdisk data */
static inline struct fdisk_sun_label *self_label(struct fdisk_context *cxt)
{
	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, SUN));

	return (struct fdisk_sun_label *) cxt->label;
}

static void set_partition(struct fdisk_context *cxt, size_t i,
		uint32_t start,uint32_t stop, uint16_t sysid)
{
	struct sun_disklabel *sunlabel = self_disklabel(cxt);
	struct fdisk_parttype *t =
			fdisk_label_get_parttype_from_code(cxt->label, sysid);

	sunlabel->vtoc.infos[i].id = cpu_to_be16(sysid);
	sunlabel->vtoc.infos[i].flags = cpu_to_be16(0);
	sunlabel->partitions[i].start_cylinder =
		cpu_to_be32(start / (cxt->geom.heads * cxt->geom.sectors));
	sunlabel->partitions[i].num_sectors = cpu_to_be32(stop - start);
	fdisk_label_set_changed(cxt->label, 1);

	fdisk_info_new_partition(cxt, i + 1, start, stop, t);
}

static size_t count_used_partitions(struct fdisk_context *cxt)
{
	struct sun_disklabel *sunlabel = self_disklabel(cxt);
	size_t ct = 0, i;

	assert(sunlabel);

	for (i = 0; i < cxt->label->nparts_max; i++) {
		if (sunlabel->partitions[i].num_sectors)
			ct++;
	}
	return ct;
}

static int sun_probe_label(struct fdisk_context *cxt)
{
	struct fdisk_sun_label *sun;
	struct sun_disklabel *sunlabel;
	unsigned short *ush;
	int csum;
	int need_fixing = 0;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, SUN));

	/* map first sector to header */
	sun = (struct fdisk_sun_label *) cxt->label;
	sun->header = (struct sun_disklabel *) cxt->firstsector;
	sunlabel = sun->header;

	if (be16_to_cpu(sunlabel->magic) != SUN_LABEL_MAGIC) {
		sun->header = NULL;
		return 0;		/* failed */
	}

	ush = ((unsigned short *) (sunlabel + 1)) - 1;
	for (csum = 0; ush >= (unsigned short *)sunlabel;)
		csum ^= *ush--;

	if (csum) {
		fdisk_warnx(cxt, _("Detected sun disklabel with wrong checksum. "
			      "Probably you'll have to set all the values, "
			      "e.g. heads, sectors, cylinders and partitions "
			      "or force a fresh label (s command in main menu)"));
		return 1;
	}

	cxt->label->nparts_max = SUN_MAXPARTITIONS;
	cxt->geom.heads = be16_to_cpu(sunlabel->nhead);
	cxt->geom.cylinders = be16_to_cpu(sunlabel->ncyl);
	cxt->geom.sectors = be16_to_cpu(sunlabel->nsect);

	if (be32_to_cpu(sunlabel->vtoc.version) != SUN_VTOC_VERSION) {
		fdisk_warnx(cxt, _("Detected sun disklabel with wrong version [%d]."),
			be32_to_cpu(sunlabel->vtoc.version));
		need_fixing = 1;
	}
	if (be32_to_cpu(sunlabel->vtoc.sanity) != SUN_VTOC_SANITY) {
		fdisk_warnx(cxt, _("Detected sun disklabel with wrong vtoc.sanity [0x%08x]."),
			be32_to_cpu(sunlabel->vtoc.sanity));
		need_fixing = 1;
	}
	if (be16_to_cpu(sunlabel->vtoc.nparts) != SUN_MAXPARTITIONS) {
		fdisk_warnx(cxt, _("Detected sun disklabel with wrong vtoc.nparts [%u]."),
			be16_to_cpu(sunlabel->vtoc.nparts));
		need_fixing = 1;
	}
	if (need_fixing) {
		fdisk_warnx(cxt, _("Warning: Wrong values need to be fixed up and "
			           "will be corrected by w(rite)"));

		sunlabel->vtoc.version = cpu_to_be32(SUN_VTOC_VERSION);
		sunlabel->vtoc.sanity = cpu_to_be32(SUN_VTOC_SANITY);
		sunlabel->vtoc.nparts = cpu_to_be16(SUN_MAXPARTITIONS);

		ush = (unsigned short *)sunlabel;
		csum = 0;
		while(ush < (unsigned short *)(&sunlabel->csum))
			csum ^= *ush++;
		sunlabel->csum = csum;

		fdisk_label_set_changed(cxt->label, 1);
	}

	cxt->label->nparts_cur = count_used_partitions(cxt);

	return 1;
}

static void ask_geom(struct fdisk_context *cxt)
{
	uintmax_t res;

	assert(cxt);

	if (fdisk_ask_number(cxt, 1, 1, 1024, _("Heads"), &res) == 0)
		cxt->geom.heads = res;
	if (fdisk_ask_number(cxt, 1, 1, 1024, _("Sectors/track"), &res) == 0)
		cxt->geom.sectors = res;
	if (fdisk_ask_number(cxt, 1, 1, USHRT_MAX, _("Cylinders"), &res) == 0)
		cxt->geom.cylinders = res;
}

static int sun_create_disklabel(struct fdisk_context *cxt)
{
	unsigned int ndiv;
	struct fdisk_sun_label *sun;		/* libfdisk sun handler */
	struct sun_disklabel *sunlabel;	/* on disk data */
	int rc = 0;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, SUN));

	/* map first sector to header */
	rc = fdisk_init_firstsector_buffer(cxt);
	if (rc)
		return rc;

	sun = (struct fdisk_sun_label *) cxt->label;
	sun->header = (struct sun_disklabel *) cxt->firstsector;

	sunlabel = sun->header;

	cxt->label->nparts_max = SUN_MAXPARTITIONS;

	sunlabel->magic = cpu_to_be16(SUN_LABEL_MAGIC);
	sunlabel->vtoc.version = cpu_to_be32(SUN_VTOC_VERSION);
	sunlabel->vtoc.sanity = cpu_to_be32(SUN_VTOC_SANITY);
	sunlabel->vtoc.nparts = cpu_to_be16(SUN_MAXPARTITIONS);

#ifdef HDIO_GETGEO
	if (cxt->geom.heads && cxt->geom.sectors) {
		fdisk_sector_t llsectors;

		if (blkdev_get_sectors(cxt->dev_fd, (unsigned long long *) &llsectors) == 0) {
			int sec_fac = cxt->sector_size / 512;
			fdisk_sector_t llcyls;

			llcyls = llsectors / (cxt->geom.heads * cxt->geom.sectors * sec_fac);
			cxt->geom.cylinders = llcyls;
			if (cxt->geom.cylinders != llcyls)
				cxt->geom.cylinders = ~0;
		} else {
			fdisk_warnx(cxt,
				_("BLKGETSIZE ioctl failed on %s. "
				  "Using geometry cylinder value of %llu. "
				  "This value may be truncated for devices "
				  "> 33.8 GB."),
				cxt->dev_path, cxt->geom.cylinders);
		}
	} else
#endif
		ask_geom(cxt);

	sunlabel->acyl   = cpu_to_be16(0);
	sunlabel->pcyl   = cpu_to_be16(cxt->geom.cylinders);
	sunlabel->rpm    = cpu_to_be16(5400);
	sunlabel->intrlv = cpu_to_be16(1);
	sunlabel->apc    = cpu_to_be16(0);

	sunlabel->nhead  = cpu_to_be16(cxt->geom.heads);
	sunlabel->nsect  = cpu_to_be16(cxt->geom.sectors);
	sunlabel->ncyl   = cpu_to_be16(cxt->geom.cylinders);

	snprintf((char *) sunlabel->label_id, sizeof(sunlabel->label_id),
		 "Linux cyl %ju alt %u hd %u sec %ju",
		 (uintmax_t) cxt->geom.cylinders,
		 be16_to_cpu(sunlabel->acyl),
		 cxt->geom.heads,
		 (uintmax_t) cxt->geom.sectors);

	if (cxt->geom.cylinders * cxt->geom.heads * cxt->geom.sectors >= 150 * 2048) {
	        ndiv = cxt->geom.cylinders - (50 * 2048 / (cxt->geom.heads * cxt->geom.sectors)); /* 50M swap */
	} else
	        ndiv = cxt->geom.cylinders * 2 / 3;

	/* create the default layout only if no-script defined */
	if (!cxt->script) {
		set_partition(cxt, 0, 0, ndiv * cxt->geom.heads * cxt->geom.sectors,
			  SUN_TAG_LINUX_NATIVE);
		set_partition(cxt, 1, ndiv * cxt->geom.heads * cxt->geom.sectors,
			  cxt->geom.cylinders * cxt->geom.heads * cxt->geom.sectors,
			  SUN_TAG_LINUX_SWAP);
		sunlabel->vtoc.infos[1].flags |= cpu_to_be16(SUN_FLAG_UNMNT);

		set_partition(cxt, 2, 0,
			  cxt->geom.cylinders * cxt->geom.heads * cxt->geom.sectors,
			  SUN_TAG_WHOLEDISK);
	}

	{
		unsigned short *ush = (unsigned short *)sunlabel;
		unsigned short csum = 0;
		while(ush < (unsigned short *)(&sunlabel->csum))
			csum ^= *ush++;
		sunlabel->csum = csum;
	}

	fdisk_label_set_changed(cxt->label, 1);
	cxt->label->nparts_cur = count_used_partitions(cxt);

	fdisk_info(cxt, _("Created a new Sun disklabel."));
	return 0;
}

static int sun_toggle_partition_flag(struct fdisk_context *cxt, size_t i, unsigned long flag)
{
	struct sun_disklabel *sunlabel;
	struct sun_info *p;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, SUN));

	if (i >= cxt->label->nparts_max)
		return -EINVAL;

	sunlabel = self_disklabel(cxt);
	p = &sunlabel->vtoc.infos[i];

	switch (flag) {
	case SUN_FLAG_UNMNT:
		p->flags ^= cpu_to_be16(SUN_FLAG_UNMNT);
		fdisk_label_set_changed(cxt->label, 1);
		break;
	case SUN_FLAG_RONLY:
		p->flags ^= cpu_to_be16(SUN_FLAG_RONLY);
		fdisk_label_set_changed(cxt->label, 1);
		break;
	default:
		return 1;
	}

	return 0;
}

static void fetch_sun(struct fdisk_context *cxt,
		      uint32_t *starts,
		      uint32_t *lens,
		      uint32_t *start,
		      uint32_t *stop)
{
	struct sun_disklabel *sunlabel;
	int continuous = 1;
	size_t i;

	assert(cxt);
	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, SUN));

	sunlabel = self_disklabel(cxt);

	*start = 0;
	*stop = cxt->geom.cylinders * cxt->geom.heads * cxt->geom.sectors;

	for (i = 0; i < cxt->label->nparts_max; i++) {
		struct sun_partition *part = &sunlabel->partitions[i];
		struct sun_info *info = &sunlabel->vtoc.infos[i];

		if (part->num_sectors &&
		    be16_to_cpu(info->id) != SUN_TAG_UNASSIGNED &&
		    be16_to_cpu(info->id) != SUN_TAG_WHOLEDISK) {
			starts[i] = be32_to_cpu(part->start_cylinder) *
				     cxt->geom.heads * cxt->geom.sectors;
			lens[i] = be32_to_cpu(part->num_sectors);
			if (continuous) {
				if (starts[i] == *start)
					*start += lens[i];
				else if (starts[i] + lens[i] >= *stop)
					*stop = starts[i];
				else
					continuous = 0;
				        /* There will be probably more gaps
					  than one, so lets check afterwards */
			}
		} else {
			starts[i] = 0;
			lens[i] = 0;
		}
	}
}

#ifdef HAVE_QSORT_R
static int verify_sun_cmp(int *a, int *b, void *data)
{
    unsigned int *verify_sun_starts = (unsigned int *) data;

    if (*a == -1)
	    return 1;
    if (*b == -1)
	    return -1;
    if (verify_sun_starts[*a] > verify_sun_starts[*b])
	    return 1;
    return -1;
}
#endif

static int sun_verify_disklabel(struct fdisk_context *cxt)
{
    uint32_t starts[SUN_MAXPARTITIONS], lens[SUN_MAXPARTITIONS], start, stop;
    uint32_t i,j,k,starto,endo;
#ifdef HAVE_QSORT_R
    int array[SUN_MAXPARTITIONS];
    unsigned int *verify_sun_starts;
#endif
    assert(cxt);
    assert(cxt->label);
    assert(fdisk_is_label(cxt, SUN));

    fetch_sun(cxt, starts, lens, &start, &stop);

    for (k = 0; k < 7; k++) {
	for (i = 0; i < SUN_MAXPARTITIONS; i++) {
	    if (k && (lens[i] % (cxt->geom.heads * cxt->geom.sectors)))
	        fdisk_warnx(cxt, _("Partition %u doesn't end on cylinder boundary."), i+1);
	    if (lens[i]) {
	        for (j = 0; j < i; j++)
	            if (lens[j]) {
	                if (starts[j] == starts[i]+lens[i]) {
	                    starts[j] = starts[i]; lens[j] += lens[i];
	                    lens[i] = 0;
	                } else if (starts[i] == starts[j]+lens[j]){
	                    lens[j] += lens[i];
	                    lens[i] = 0;
	                } else if (!k) {
	                    if (starts[i] < starts[j]+lens[j] &&
				starts[j] < starts[i]+lens[i]) {
	                        starto = starts[i];
	                        if (starts[j] > starto)
					starto = starts[j];
	                        endo = starts[i]+lens[i];
	                        if (starts[j]+lens[j] < endo)
					endo = starts[j]+lens[j];
	                        fdisk_warnx(cxt, _("Partition %u overlaps with others in "
				       "sectors %u-%u."), i+1, starto, endo);
	                    }
	                }
	            }
	    }
	}
    }

#ifdef HAVE_QSORT_R
    for (i = 0; i < SUN_MAXPARTITIONS; i++) {
        if (lens[i])
            array[i] = i;
        else
            array[i] = -1;
    }
    verify_sun_starts = starts;

    qsort_r(array,ARRAY_SIZE(array),sizeof(array[0]),
	  (int (*)(const void *,const void *,void *)) verify_sun_cmp,
	  verify_sun_starts);

    if (array[0] == -1) {
	fdisk_info(cxt, _("No partitions defined."));
	return 0;
    }
    stop = cxt->geom.cylinders * cxt->geom.heads * cxt->geom.sectors;
    if (starts[array[0]])
        fdisk_warnx(cxt, _("Unused gap - sectors 0-%u."), starts[array[0]]);
    for (i = 0; i < 7 && array[i+1] != -1; i++) {
        fdisk_warnx(cxt, _("Unused gap - sectors %u-%u."),
	       (starts[array[i]] + lens[array[i]]),
	       starts[array[i+1]]);
    }
    start = (starts[array[i]] + lens[array[i]]);
    if (start < stop)
        fdisk_warnx(cxt, _("Unused gap - sectors %u-%u."), start, stop);
#endif
    return 0;
}


static int is_free_sector(struct fdisk_context *cxt,
		fdisk_sector_t s, uint32_t starts[], uint32_t lens[])
{
	size_t i;

	for (i = 0; i < cxt->label->nparts_max; i++) {
		if (lens[i] && starts[i] <= s
		    && starts[i] + lens[i] > s)
			return 0;
	}
	return 1;
}

static int sun_add_partition(
		struct fdisk_context *cxt,
		struct fdisk_partition *pa,
		size_t *partno)
{
	struct sun_disklabel *sunlabel = self_disklabel(cxt);
	uint32_t starts[SUN_MAXPARTITIONS], lens[SUN_MAXPARTITIONS];
	struct sun_partition *part;
	struct sun_info *info;
	uint32_t start, stop, stop2;
	int whole_disk = 0;
	int sys = pa && pa->type ? pa->type->code : SUN_TAG_LINUX_NATIVE;
	int rc;
	size_t n;

	char mesg[256];
	size_t i;
	unsigned int first, last;

	rc = fdisk_partition_next_partno(pa, cxt, &n);
	if (rc)
		return rc;

	part = &sunlabel->partitions[n];
	info = &sunlabel->vtoc.infos[n];

	if (part->num_sectors && be16_to_cpu(info->id) != SUN_TAG_UNASSIGNED) {
		fdisk_info(cxt, _("Partition %zu is already defined.  Delete "
			"it before re-adding it."), n + 1);
		return -EINVAL;
	}

	fetch_sun(cxt, starts, lens, &start, &stop);

	if (stop <= start) {
		if (n == 2)
			whole_disk = 1;
		else {
			fdisk_info(cxt, _("Other partitions already cover the "
				"whole disk. Delete some/shrink them before retry."));
			return -EINVAL;
		}
	}

	if (pa && pa->start_follow_default)
		first = start;
	else if (pa && fdisk_partition_has_start(pa)) {
		first = pa->start;

		if (!whole_disk && !is_free_sector(cxt, first, starts, lens))
			return -ERANGE;
	} else {
		struct fdisk_ask *ask;

		snprintf(mesg, sizeof(mesg), _("First %s"),
				fdisk_get_unit(cxt, FDISK_SINGULAR));
		for (;;) {
			ask = fdisk_new_ask();
			if (!ask)
				return -ENOMEM;

			fdisk_ask_set_query(ask, mesg);
			fdisk_ask_set_type(ask, FDISK_ASKTYPE_NUMBER);

			if (whole_disk) {
				fdisk_ask_number_set_low(ask,     0);	/* minimal */
				fdisk_ask_number_set_default(ask, 0);	/* default */
				fdisk_ask_number_set_high(ask,    0);	/* maximal */
			} else {
				fdisk_ask_number_set_low(ask,     fdisk_scround(cxt, start));	/* minimal */
				fdisk_ask_number_set_default(ask, fdisk_scround(cxt, start));	/* default */
				fdisk_ask_number_set_high(ask,    fdisk_scround(cxt, stop));	/* maximal */
			}
			rc = fdisk_do_ask(cxt, ask);
			first = fdisk_ask_number_get_result(ask);
			fdisk_unref_ask(ask);
			if (rc)
				return rc;

			if (fdisk_use_cylinders(cxt))
				first *= fdisk_get_units_per_sector(cxt);

			/* ewt asks to add: "don't start a partition at cyl 0"
			   However, edmundo@rano.demon.co.uk writes:
			   "In addition to having a Sun partition table, to be able to
			   boot from the disc, the first partition, /dev/sdX1, must
			   start at cylinder 0. This means that /dev/sdX1 contains
			   the partition table and the boot block, as these are the
			   first two sectors of the disc. Therefore you must be
			   careful what you use /dev/sdX1 for. In particular, you must
			   not use a partition starting at cylinder 0 for Linux swap,
			   as that would overwrite the partition table and the boot
			   block. You may, however, use such a partition for a UFS
			   or EXT2 file system, as these file systems leave the first
			   1024 bytes undisturbed. */
			/* On the other hand, one should not use partitions
			   starting at block 0 in an md, or the label will
			   be trashed. */
			if (!is_free_sector(cxt, first, starts,  lens) && !whole_disk) {
				if (n == 2 && !first) {
				    whole_disk = 1;
				    break;
				}
				fdisk_warnx(cxt, _("Sector %d is already allocated"), first);
			} else
				break;
		}
	}

	if (n == 2 && first != 0)
		fdisk_warnx(cxt, _("It is highly recommended that the "
				   "third partition covers the whole disk "
				   "and is of type `Whole disk'"));

	if (!fdisk_use_cylinders(cxt)) {
		/* Starting sector has to be properly aligned */
		int cs = cxt->geom.heads * cxt->geom.sectors;
		int x = first % cs;

		if (x) {
			fdisk_info(cxt, _("Aligning the first sector from %u to %u "
					  "to be on cylinder boundary."),
					first, first + cs - x);
			first += cs - x;
		}
	}

	stop = cxt->geom.cylinders * cxt->geom.heads * cxt->geom.sectors;	/* ancient */
	stop2 = stop;
	for (i = 0; i < cxt->label->nparts_max; i++) {
		if (starts[i] > first && starts[i] < stop)
			stop = starts[i];
	}

	/* last */
	if (pa && pa->end_follow_default)
		last = whole_disk || (n == 2 && !first) ? stop2 : stop;
	else if (pa && fdisk_partition_has_size(pa)) {
		last = first + pa->size - 1ULL;

		if (!whole_disk && last > stop)
			return -ERANGE;
	} else {
		struct fdisk_ask *ask = fdisk_new_ask();

		if (!ask)
			return -ENOMEM;

		snprintf(mesg, sizeof(mesg),
			 _("Last %s or +%s or +size{K,M,G,T,P}"),
			 fdisk_get_unit(cxt, FDISK_SINGULAR),
			 fdisk_get_unit(cxt, FDISK_PLURAL));
		fdisk_ask_set_query(ask, mesg);
		fdisk_ask_set_type(ask, FDISK_ASKTYPE_OFFSET);

		if (whole_disk) {
			fdisk_ask_number_set_low(ask,     fdisk_scround(cxt, stop2));	/* minimal */
			fdisk_ask_number_set_default(ask, fdisk_scround(cxt, stop2));	/* default */
			fdisk_ask_number_set_high(ask,    fdisk_scround(cxt, stop2));	/* maximal */
			fdisk_ask_number_set_base(ask,    0);
		} else if (n == 2 && !first) {
			fdisk_ask_number_set_low(ask,     fdisk_scround(cxt, first));	/* minimal */
			fdisk_ask_number_set_default(ask, fdisk_scround(cxt, stop2));	/* default */
			fdisk_ask_number_set_high(ask,    fdisk_scround(cxt, stop2));	/* maximal */
			fdisk_ask_number_set_base(ask,	  fdisk_scround(cxt, first));
		} else {
			fdisk_ask_number_set_low(ask,     fdisk_scround(cxt, first));	/* minimal */
			fdisk_ask_number_set_default(ask, fdisk_scround(cxt, stop));	/* default */
			fdisk_ask_number_set_high(ask,    fdisk_scround(cxt, stop));	/* maximal */
			fdisk_ask_number_set_base(ask,    fdisk_scround(cxt, first));
		}

		if (fdisk_use_cylinders(cxt))
			fdisk_ask_number_set_unit(ask,
				     cxt->sector_size *
				     fdisk_get_units_per_sector(cxt));
		else
			fdisk_ask_number_set_unit(ask,	cxt->sector_size);

		rc = fdisk_do_ask(cxt, ask);
		last = fdisk_ask_number_get_result(ask);

		fdisk_unref_ask(ask);
		if (rc)
			return rc;
		if (fdisk_use_cylinders(cxt))
			last *= fdisk_get_units_per_sector(cxt);
	}

	if (n == 2 && !first) {
		if (last >= stop2) {
		    whole_disk = 1;
		    last = stop2;
		} else if (last > stop) {
		    fdisk_warnx(cxt,
   _("You haven't covered the whole disk with the 3rd partition, but your value\n"
     "%lu %s covers some other partition. Your entry has been changed\n"
     "to %lu %s"),
			(unsigned long) fdisk_scround(cxt, last), fdisk_get_unit(cxt, FDISK_SINGULAR),
			(unsigned long) fdisk_scround(cxt, stop), fdisk_get_unit(cxt, FDISK_SINGULAR));
		    last = stop;
		}
	} else if (!whole_disk && last > stop)
		last = stop;

	if (whole_disk)
		sys = SUN_TAG_WHOLEDISK;

	set_partition(cxt, n, first, last, sys);
	cxt->label->nparts_cur = count_used_partitions(cxt);
	if (partno)
		*partno = n;
	return 0;
}

static int sun_delete_partition(struct fdisk_context *cxt,
		size_t partnum)
{
	struct sun_disklabel *sunlabel;
	struct sun_partition *part;
	struct sun_info *info;
	unsigned int nsec;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, SUN));

	sunlabel = self_disklabel(cxt);
	part = &sunlabel->partitions[partnum];
	info = &sunlabel->vtoc.infos[partnum];

	if (partnum == 2 &&
	    be16_to_cpu(info->id) == SUN_TAG_WHOLEDISK &&
	    !part->start_cylinder &&
	    (nsec = be32_to_cpu(part->num_sectors))
	    == cxt->geom.heads * cxt->geom.sectors * cxt->geom.cylinders)
		fdisk_info(cxt, _("If you want to maintain SunOS/Solaris compatibility, "
			 "consider leaving this "
			 "partition as Whole disk (5), starting at 0, with %u "
			 "sectors"), nsec);
	info->id = cpu_to_be16(SUN_TAG_UNASSIGNED);
	part->num_sectors = 0;
	cxt->label->nparts_cur = count_used_partitions(cxt);
	fdisk_label_set_changed(cxt->label, 1);
	return 0;
}


static int sun_list_disklabel(struct fdisk_context *cxt)
{
	struct sun_disklabel *sunlabel;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, SUN));

	sunlabel = self_disklabel(cxt);

	if (fdisk_is_details(cxt)) {
		fdisk_info(cxt,
		_("Label geometry: %d rpm, %d alternate and %d physical cylinders,\n"
		  "                %d extra sects/cyl, interleave %d:1"),
		       be16_to_cpu(sunlabel->rpm),
		       be16_to_cpu(sunlabel->acyl),
		       be16_to_cpu(sunlabel->pcyl),
		       be16_to_cpu(sunlabel->apc),
		       be16_to_cpu(sunlabel->intrlv));
		fdisk_info(cxt, _("Label ID: %s"), sunlabel->label_id);
		fdisk_info(cxt, _("Volume ID: %s"),
		       *sunlabel->vtoc.volume_id ? sunlabel->vtoc.volume_id : _("<none>"));
	}

	return 0;
}

static struct fdisk_parttype *sun_get_parttype(
		struct fdisk_context *cxt,
		size_t n)
{
	struct sun_disklabel *sunlabel = self_disklabel(cxt);
	struct fdisk_parttype *t;

	if (n >= cxt->label->nparts_max)
		return NULL;

	t = fdisk_label_get_parttype_from_code(cxt->label,
			be16_to_cpu(sunlabel->vtoc.infos[n].id));
	return t ? : fdisk_new_unknown_parttype(be16_to_cpu(sunlabel->vtoc.infos[n].id), NULL);
}


static int sun_get_partition(struct fdisk_context *cxt, size_t n,
			      struct fdisk_partition *pa)
{
	struct sun_disklabel *sunlabel;
	struct sun_partition *part;
	uint16_t flags;
	uint32_t start, len;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, SUN));

	if (n >= cxt->label->nparts_max)
		return -EINVAL;

	sunlabel = self_disklabel(cxt);
	part = &sunlabel->partitions[n];

	pa->used = part->num_sectors ? 1 : 0;
	if (!pa->used)
		return 0;

	flags = be16_to_cpu(sunlabel->vtoc.infos[n].flags);
	start = be32_to_cpu(part->start_cylinder)
			* cxt->geom.heads * cxt->geom.sectors;
	len = be32_to_cpu(part->num_sectors);

	pa->type = sun_get_parttype(cxt, n);
	if (pa->type && pa->type->code == SUN_TAG_WHOLEDISK)
		pa->wholedisk = 1;

	if (flags & SUN_FLAG_UNMNT || flags & SUN_FLAG_RONLY) {
		if (asprintf(&pa->attrs, "%c%c",
				flags & SUN_FLAG_UNMNT ? 'u' : ' ',
				flags & SUN_FLAG_RONLY ? 'r' : ' ') < 0)
			return -ENOMEM;
	}

	pa->start = start;
	pa->size = len;

	return 0;
}

/**
 * fdisk_sun_set_alt_cyl:
 * @cxt: context
 *
 * Sets number of alternative cylinders. This function uses libfdisk Ask API
 * for dialog with user.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_sun_set_alt_cyl(struct fdisk_context *cxt)
{
	struct sun_disklabel *sunlabel = self_disklabel(cxt);
	uintmax_t res;
	int rc = fdisk_ask_number(cxt, 0,			/* low */
			be16_to_cpu(sunlabel->acyl),		/* default */
			65535,					/* high */
			_("Number of alternate cylinders"),	/* query */
			&res);					/* result */
	if (rc)
		return rc;

	sunlabel->acyl = cpu_to_be16(res);
	return 0;
}

/**
 * fdisk_sun_set_xcyl:
 * @cxt: context
 *
 * Sets number of extra sectors per cylinder. This function uses libfdisk Ask API
 * for dialog with user.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_sun_set_xcyl(struct fdisk_context *cxt)
{
	struct sun_disklabel *sunlabel = self_disklabel(cxt);
	uintmax_t res;
	int rc = fdisk_ask_number(cxt, 0,			/* low */
			be16_to_cpu(sunlabel->apc),		/* default */
			cxt->geom.sectors,			/* high */
			_("Extra sectors per cylinder"),	/* query */
			&res);					/* result */
	if (rc)
		return rc;
	sunlabel->apc = cpu_to_be16(res);
	return 0;
}

/**
 * fdisk_sun_set_ilfact:
 * @cxt: context
 *
 * Sets interleave factor. This function uses libfdisk Ask API for dialog with
 * user.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_sun_set_ilfact(struct fdisk_context *cxt)
{
	struct sun_disklabel *sunlabel = self_disklabel(cxt);
	uintmax_t res;
	int rc = fdisk_ask_number(cxt, 1,			/* low */
			be16_to_cpu(sunlabel->intrlv),		/* default */
			32,					/* high */
			_("Interleave factor"),	/* query */
			&res);					/* result */
	if (rc)
		return rc;
	sunlabel->intrlv = cpu_to_be16(res);
	return 0;
}

/**
 * fdisk_sun_set_rspeed
 * @cxt: context
 *
 * Sets rotation speed. This function uses libfdisk Ask API for dialog with
 * user.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_sun_set_rspeed(struct fdisk_context *cxt)
{
	struct sun_disklabel *sunlabel = self_disklabel(cxt);
	uintmax_t res;
	int rc = fdisk_ask_number(cxt, 1,			/* low */
			be16_to_cpu(sunlabel->rpm),		/* default */
			USHRT_MAX,				/* high */
			_("Rotation speed (rpm)"),		/* query */
			&res);					/* result */
	if (rc)
		return rc;
	sunlabel->rpm = cpu_to_be16(res);
	return 0;
}

/**
 * fdisk_sun_set_pcylcount
 * @cxt: context
 *
 * Sets number of physical cylinders. This function uses libfdisk Ask API for
 * dialog with user.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_sun_set_pcylcount(struct fdisk_context *cxt)
{
	struct sun_disklabel *sunlabel = self_disklabel(cxt);
	uintmax_t res;
	int rc = fdisk_ask_number(cxt, 0,			/* low */
			be16_to_cpu(sunlabel->pcyl),		/* default */
			USHRT_MAX,				/* high */
			_("Number of physical cylinders"),	/* query */
			&res);					/* result */
	if (!rc)
		return rc;
	sunlabel->pcyl = cpu_to_be16(res);
	return 0;
}

static int sun_write_disklabel(struct fdisk_context *cxt)
{
	struct sun_disklabel *sunlabel;
	unsigned short *ush;
	unsigned short csum = 0;
	const size_t sz = sizeof(struct sun_disklabel);

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, SUN));

	sunlabel = self_disklabel(cxt);

	/* Maybe geometry has been modified */
	sunlabel->nhead = cpu_to_be16(cxt->geom.heads);
	sunlabel->nsect = cpu_to_be16(cxt->geom.sectors);

	if (cxt->geom.cylinders != be16_to_cpu(sunlabel->ncyl)) {
		int a = cpu_to_be16(cxt->geom.cylinders);
		int b = be16_to_cpu(sunlabel->acyl);
		sunlabel->ncyl = a - b;
	}

	ush = (unsigned short *) sunlabel;

	while(ush < (unsigned short *)(&sunlabel->csum))
		csum ^= *ush++;
	sunlabel->csum = csum;
	if (lseek(cxt->dev_fd, 0, SEEK_SET) < 0)
		return -errno;
	if (write_all(cxt->dev_fd, sunlabel, sz) != 0)
		return -errno;

	return 0;
}

static int sun_set_partition(
		struct fdisk_context *cxt,
		size_t i,
		struct fdisk_partition *pa)
{
	struct sun_disklabel *sunlabel;
	struct sun_partition *part;
	struct sun_info *info;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, SUN));

	sunlabel = self_disklabel(cxt);

	if (i >= cxt->label->nparts_max)
		return -EINVAL;

	if (pa->type) {
		struct fdisk_parttype *t = pa->type;

		if (t->code > UINT16_MAX)
			return -EINVAL;

		if (i == 2 && t->code != SUN_TAG_WHOLEDISK)
			fdisk_info(cxt, _("Consider leaving partition 3 as Whole disk (5),\n"
			         "as SunOS/Solaris expects it and even Linux likes it.\n"));

		part = &sunlabel->partitions[i];
		info = &sunlabel->vtoc.infos[i];

		if (cxt->script == NULL &&
		    t->code == SUN_TAG_LINUX_SWAP && !part->start_cylinder) {
			int yes, rc;

			rc = fdisk_ask_yesno(cxt,
			      _("It is highly recommended that the partition at offset 0\n"
			      "is UFS, EXT2FS filesystem or SunOS swap. Putting Linux swap\n"
			      "there may destroy your partition table and bootblock.\n"
			      "Are you sure you want to tag the partition as Linux swap?"), &yes);
			if (rc)
				return rc;
			if (!yes)
				return 1;
		}

		switch (t->code) {
		case SUN_TAG_SWAP:
		case SUN_TAG_LINUX_SWAP:
			/* swaps are not mountable by default */
			info->flags |= cpu_to_be16(SUN_FLAG_UNMNT);
			break;
		default:
			/* assume other types are mountable;
			   user can change it anyway */
			info->flags &= ~cpu_to_be16(SUN_FLAG_UNMNT);
			break;
		}
		info->id = cpu_to_be16(t->code);
	}

	if (fdisk_partition_has_start(pa))
		sunlabel->partitions[i].start_cylinder =
			cpu_to_be32(pa->start / (cxt->geom.heads * cxt->geom.sectors));
	if (fdisk_partition_has_size(pa))
		sunlabel->partitions[i].num_sectors = cpu_to_be32(pa->size);

	fdisk_label_set_changed(cxt->label, 1);
	return 0;
}


static int sun_reset_alignment(struct fdisk_context *cxt __attribute__((__unused__)))
{
	return 0;
}


static int sun_partition_is_used(
		struct fdisk_context *cxt,
		size_t i)
{
	struct sun_disklabel *sunlabel;

	assert(cxt);
	assert(cxt->label);
	assert(fdisk_is_label(cxt, SUN));

	if (i >= cxt->label->nparts_max)
		return 0;

	sunlabel = self_disklabel(cxt);
	return sunlabel->partitions[i].num_sectors ? 1 : 0;
}

static const struct fdisk_field sun_fields[] =
{
	{ FDISK_FIELD_DEVICE,	N_("Device"),	 10,	0 },
	{ FDISK_FIELD_START,	N_("Start"),	  5,	FDISK_FIELDFL_NUMBER },
	{ FDISK_FIELD_END,	N_("End"),	  5,	FDISK_FIELDFL_NUMBER },
	{ FDISK_FIELD_SECTORS,	N_("Sectors"),	  5,	FDISK_FIELDFL_NUMBER },
	{ FDISK_FIELD_CYLINDERS,N_("Cylinders"),  5,	FDISK_FIELDFL_NUMBER },
	{ FDISK_FIELD_SIZE,	N_("Size"),	  5,	FDISK_FIELDFL_NUMBER },
	{ FDISK_FIELD_TYPEID,	N_("Id"),	  2,	FDISK_FIELDFL_NUMBER },
	{ FDISK_FIELD_TYPE,	N_("Type"),	0.1,	0 },
	{ FDISK_FIELD_ATTR,	N_("Flags"),	  0,	FDISK_FIELDFL_NUMBER }
};

const struct fdisk_label_operations sun_operations =
{
	.probe		= sun_probe_label,
	.write		= sun_write_disklabel,
	.verify		= sun_verify_disklabel,
	.create		= sun_create_disklabel,
	.list		= sun_list_disklabel,

	.get_part	= sun_get_partition,
	.set_part	= sun_set_partition,
	.add_part	= sun_add_partition,
	.del_part	= sun_delete_partition,

	.part_is_used	= sun_partition_is_used,
	.part_toggle_flag = sun_toggle_partition_flag,

	.reset_alignment = sun_reset_alignment,
};

/*
 * allocates SUN label driver
 */
struct fdisk_label *fdisk_new_sun_label(struct fdisk_context *cxt)
{
	struct fdisk_label *lb;
	struct fdisk_sun_label *sun;

	assert(cxt);

	sun = calloc(1, sizeof(*sun));
	if (!sun)
		return NULL;

	/* initialize generic part of the driver */
	lb = (struct fdisk_label *) sun;
	lb->name = "sun";
	lb->id = FDISK_DISKLABEL_SUN;
	lb->op = &sun_operations;
	lb->parttypes = sun_parttypes;
	lb->nparttypes = ARRAY_SIZE(sun_parttypes) - 1;
	lb->fields = sun_fields;
	lb->nfields = ARRAY_SIZE(sun_fields);
	lb->flags |= FDISK_LABEL_FL_REQUIRE_GEOMETRY;

	return lb;
}
