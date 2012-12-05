
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
