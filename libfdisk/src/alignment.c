
#ifdef HAVE_LIBBLKID
#include <blkid.h>
#endif
#include "blkdev.h"

#include "fdiskP.h"

/*
 * Alignment according to logical granulity (usually 1MiB)
 */
static int lba_is_aligned(struct fdisk_context *cxt, sector_t lba)
{
	unsigned long granularity = max(cxt->phy_sector_size, cxt->min_io_size);
	uintmax_t offset;

	if (cxt->grain > granularity)
		granularity = cxt->grain;
	offset = (lba * cxt->sector_size) & (granularity - 1);

	return !((granularity + cxt->alignment_offset - offset) & (granularity - 1));
}

/*
 * Alignment according to physical device topology (usually minimal i/o size)
 */
static int lba_is_phy_aligned(struct fdisk_context *cxt, sector_t lba)
{
	unsigned long granularity = max(cxt->phy_sector_size, cxt->min_io_size);
	uintmax_t offset = (lba * cxt->sector_size) & (granularity - 1);

	return !((granularity + cxt->alignment_offset - offset) & (granularity - 1));
}

/*
 * Align @lba in @direction FDISK_ALIGN_{UP,DOWN,NEAREST}
 */
sector_t fdisk_align_lba(struct fdisk_context *cxt, sector_t lba, int direction)
{
	sector_t res;

	if (lba_is_aligned(cxt, lba))
		res = lba;
	else {
		sector_t sects_in_phy = cxt->grain / cxt->sector_size;

		if (lba < cxt->first_lba)
			res = cxt->first_lba;

		else if (direction == FDISK_ALIGN_UP)
			res = ((lba + sects_in_phy) / sects_in_phy) * sects_in_phy;

		else if (direction == FDISK_ALIGN_DOWN)
			res = (lba / sects_in_phy) * sects_in_phy;

		else /* FDISK_ALIGN_NEAREST */
			res = ((lba + sects_in_phy / 2) / sects_in_phy) * sects_in_phy;

		if (cxt->alignment_offset && !lba_is_aligned(cxt, res) &&
		    res > cxt->alignment_offset / cxt->sector_size) {
			/*
			 * apply alignment_offset
			 *
			 * On disk with alignment compensation physical blocks starts
			 * at LBA < 0 (usually LBA -1). It means we have to move LBA
			 * according the offset to be on the physical boundary.
			 */
			/* fprintf(stderr, "LBA: %llu apply alignment_offset\n", res); */
			res -= (max(cxt->phy_sector_size, cxt->min_io_size) -
					cxt->alignment_offset) / cxt->sector_size;

			if (direction == FDISK_ALIGN_UP && res < lba)
				res += sects_in_phy;
		}
	}

	return res;
}

/*
 * Align @lba, the result has to be between @start and @stop
 */
sector_t fdisk_align_lba_in_range(struct fdisk_context *cxt,
				  sector_t lba, sector_t start, sector_t stop)
{
	start = fdisk_align_lba(cxt, start, FDISK_ALIGN_UP);
	stop = fdisk_align_lba(cxt, stop, FDISK_ALIGN_DOWN);
	lba = fdisk_align_lba(cxt, lba, FDISK_ALIGN_NEAREST);

	if (lba < start)
		return start;
	else if (lba > stop)
		return stop;
	return lba;
}

/*
 * Print warning if the partition @lba (start of the @partition) is not
 * aligned to physical sector boundary.
 */
void fdisk_warn_alignment(struct fdisk_context *cxt, sector_t lba, int partition)
{
	if (!lba_is_phy_aligned(cxt, lba))
		printf(_("Partition %i does not start on physical sector boundary.\n"),
			partition + 1);
}

static unsigned long get_sector_size(int fd)
{
	int sect_sz;

	if (!blkdev_get_sector_size(fd, &sect_sz))
		return (unsigned long) sect_sz;
	return DEFAULT_SECTOR_SIZE;
}

/**
 * fdisk_override_sector_size:
 * @cxt: fdisk context
 * @s: required sector size
 *
 * Overwrites logical and physical sector size. Note that the default sector
 * size is discovered by fdisk_new_context_from_device() from device topology.
 *
 * Don't use this function, rely on the default behavioer is more safe.
 *
 * Returns: 0 on success, < 0 on error.
 */
int fdisk_override_sector_size(struct fdisk_context *cxt, sector_t s)
{
	if (!cxt)
		return -EINVAL;

	cxt->phy_sector_size = cxt->sector_size = s;
	cxt->min_io_size = cxt->io_size = s;

	fdisk_reset_alignment(cxt);
	return 0;
}

static void recount_geometry(struct fdisk_context *cxt)
{
	cxt->geom.cylinders = cxt->total_sectors /
		(cxt->geom.heads * cxt->geom.sectors);
}

/**
 * fdisk_override_geometry:
 * @cxt: fdisk context
 * @cylinders: user specified cylinders
 * @heads: user specified heads
 * @sectors: user specified sectors
 *
 * Overrides autodiscovery and apply user specified geometry.
 *
 * Returns: 0 on success, < 0 on error.
 */
int fdisk_override_geometry(struct fdisk_context *cxt,
			    unsigned int cylinders,
			    unsigned int heads,
			    unsigned int sectors)
{
	if (!cxt)
		return -EINVAL;
	if (heads)
		cxt->geom.heads = heads;
	if (sectors)
		cxt->geom.sectors = sectors;

	if (cylinders)
		cxt->geom.cylinders = cylinders;
	else
		recount_geometry(cxt);

	fdisk_reset_alignment(cxt);
	return 0;
}

/*
 * Generic (label independent) geometry
 */
int fdisk_discover_geometry(struct fdisk_context *cxt)
{
	sector_t nsects;
	unsigned int h = 0, s = 0;

	assert(cxt);
	assert(cxt->geom.heads == 0);

	/* get number of 512-byte sectors, and convert it the real sectors */
	if (!blkdev_get_sectors(cxt->dev_fd, &nsects))
		cxt->total_sectors = (nsects / (cxt->sector_size >> 9));

	/* what the kernel/bios thinks the geometry is */
	blkdev_get_geometry(cxt->dev_fd, &h, &s);
	if (!h && !s) {
		/* unable to discover geometry, use default values */
		s = 63;
		h = 255;
	}

	/* obtained heads and sectors */
	cxt->geom.heads = h;
	cxt->geom.sectors = s;
	recount_geometry(cxt);

	DBG(GEOMETRY, dbgprint("geometry discovered for %s: C/H/S: %lld/%d/%lld",
			       cxt->dev_path, cxt->geom.cylinders,
			       cxt->geom.heads, cxt->geom.sectors));
	return 0;
}

int fdisk_discover_topology(struct fdisk_context *cxt)
{
#ifdef HAVE_LIBBLKID
	blkid_probe pr;
#endif
	assert(cxt);
	assert(cxt->sector_size == 0);

#ifdef HAVE_LIBBLKID
	DBG(TOPOLOGY, dbgprint("initialize libblkid prober"));

	pr = blkid_new_probe();
	if (pr && blkid_probe_set_device(pr, cxt->dev_fd, 0, 0) == 0) {
		blkid_topology tp = blkid_probe_get_topology(pr);

		if (tp) {
			cxt->min_io_size = blkid_topology_get_minimum_io_size(tp);
			cxt->optimal_io_size = blkid_topology_get_optimal_io_size(tp);
			cxt->phy_sector_size = blkid_topology_get_physical_sector_size(tp);
			cxt->alignment_offset = blkid_topology_get_alignment_offset(tp);

			/* I/O size used by fdisk */
			cxt->io_size = cxt->optimal_io_size;
			if (!cxt->io_size)
				/* optimal IO is optional, default to minimum IO */
				cxt->io_size = cxt->min_io_size;
		}
	}
	blkid_free_probe(pr);
#endif

	cxt->sector_size = get_sector_size(cxt->dev_fd);
	if (!cxt->phy_sector_size) /* could not discover physical size */
		cxt->phy_sector_size = cxt->sector_size;

	/* no blkid or error, use default values */
	if (!cxt->min_io_size)
		cxt->min_io_size = cxt->sector_size;
	if (!cxt->io_size)
		cxt->io_size = cxt->sector_size;

	DBG(TOPOLOGY, dbgprint("topology discovered for %s:\n"
			       "\tlogical/physical sector sizes: %ld/%ld\n"
			       "\tfdisk/minimal/optimal io sizes: %ld/%ld/%ld\n",
			       cxt->dev_path, cxt->sector_size, cxt->phy_sector_size,
			       cxt->io_size, cxt->optimal_io_size, cxt->min_io_size));
	return 0;
}

static int has_topology(struct fdisk_context *cxt)
{
	/*
	 * Assume that the device provides topology info if
	 * optimal_io_size is set or alignment_offset is set or
	 * minimum_io_size is not power of 2.
	 */
	if (cxt &&
	    (cxt->optimal_io_size ||
	     cxt->alignment_offset ||
	     !is_power_of_2(cxt->min_io_size)))
		return 1;
	return 0;
}

/*
 * The LBA of the first partition is based on the device geometry and topology.
 * This offset is generic (and recommended) for all labels.
 *
 * Returns: 0 on error or number of logical sectors.
 */
sector_t fdisk_topology_get_first_lba(struct fdisk_context *cxt)
{
	sector_t x = 0, res;

	if (!cxt)
		return 0;

	if (!cxt->io_size)
		fdisk_discover_topology(cxt);

	/*
	 * Align the begin of partitions to:
	 *
	 * a) topology
	 *  a2) alignment offset
	 *  a1) or physical sector (minimal_io_size, aka "grain")
	 *
	 * b) or default to 1MiB (2048 sectrors, Windows Vista default)
	 *
	 * c) or for very small devices use 1 phy.sector
	 */
	if (has_topology(cxt)) {
		if (cxt->alignment_offset)
			x = cxt->alignment_offset;
		else if (cxt->io_size > 2048 * 512)
			x = cxt->io_size;
	}
	/* default to 1MiB */
	if (!x)
		x = 2048 * 512;

	res = x / cxt->sector_size;

	/* don't use huge offset on small devices */
	if (cxt->total_sectors <= res * 4)
		res = cxt->phy_sector_size / cxt->sector_size;

	return res;
}

/*
 * The LBA of the first partition is based on the device geometry and topology.
 * This offset is generic generic (and recommended) for all labels.
 *
 * Returns: 0 on error or number of bytes.
 */
unsigned long fdisk_topology_get_grain(struct fdisk_context *cxt)
{
	unsigned long res;

	if (!cxt)
		return 0;

	if (!cxt->io_size)
		fdisk_discover_topology(cxt);

	res = cxt->io_size;

	/* use 1MiB grain always when possible */
	if (res < 2048 * 512)
		res = 2048 * 512;

	/* don't use huge grain on small devices */
	if (cxt->total_sectors <= (res * 4 / cxt->sector_size))
		res = cxt->phy_sector_size;

	return res;
}

/**
 * fdisk_reset_alignment:
 * @cxt: fdisk context
 *
 * Resets alignment setting to the default or label specific values.
 *
 * Returns: 0 on success, < 0 in case of error.
 */
int fdisk_reset_alignment(struct fdisk_context *cxt)
{
	int rc = 0;

	if (!cxt)
		return -EINVAL;

	/* default */
	cxt->grain = fdisk_topology_get_grain(cxt);
	cxt->first_lba = fdisk_topology_get_first_lba(cxt);

	/* overwrite default by label stuff */
	if (cxt->label && cxt->label->op->reset_alignment)
		rc = cxt->label->op->reset_alignment(cxt);

	DBG(LABEL, dbgprint("%s alignment reseted to: "
			    "first LBA=%ju, grain=%lu [rc=%d]",
			    cxt->label ? cxt->label->name : NULL,
			    (uintmax_t) cxt->first_lba,
			    cxt->grain,	rc));
	return rc;
}


sector_t fdisk_scround(struct fdisk_context *cxt, sector_t num)
{
	sector_t un = fdisk_context_get_units_per_sector(cxt);
	return (num + un - 1) / un;
}
