
#include "fdiskP.h"

/**
 * fdisk_dev_has_disklabel:
 * @cxt: fdisk context
 *
 * Returns: return 1 if there is label on the device.
 */
int fdisk_dev_has_disklabel(struct fdisk_context *cxt)
{
	return cxt && cxt->disklabel != FDISK_DISKLABEL_ANY;
}

/**
 * fdisk_dev_is_disklabel:
 * @cxt: fdisk context
 * @l: disklabel type
 *
 * Returns: return 1 if there is @l disklabel on the device.
 */
int fdisk_dev_is_disklabel(struct fdisk_context *cxt, enum fdisk_labeltype l)
{
	return cxt && cxt->disklabel == l;
}

/**
 * fdisk_write_disklabel:
 * @cxt: fdisk context
 *
 * Write in-memory changes to disk
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_write_disklabel(struct fdisk_context *cxt)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->write)
		return -ENOSYS;

	return cxt->label->write(cxt);
}

/**
 * fdisk_verify_disklabel:
 * @cxt: fdisk context
 *
 * Verifies the partition table.
 *
 * Returns 0.
 */
int fdisk_verify_disklabel(struct fdisk_context *cxt)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->verify)
		return -ENOSYS;

	return cxt->label->verify(cxt);
}

/**
 * fdisk_add_partition:
 * @cxt: fdisk context
 * @partnum: partition number to create
 * @t: partition type to create or NULL for label-specific default
 *
 * Creates a new partition, with number @partnum and type @parttype.
 *
 * Returns 0.
 */
int fdisk_add_partition(struct fdisk_context *cxt, int partnum,
			struct fdisk_parttype *t)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->part_add)
		return -ENOSYS;

	DBG(LABEL, dbgprint("adding new partition number %d", partnum));
	cxt->label->part_add(cxt, partnum, t);
	return 0;
}

/**
 * fdisk_delete_partition:
 * @cxt: fdisk context
 * @partnum: partition number to delete
 *
 * Deletes a @partnum partition.
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_delete_partition(struct fdisk_context *cxt, int partnum)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->part_delete)
		return -ENOSYS;

	DBG(LABEL, dbgprint("deleting %s partition number %d",
				cxt->label->name, partnum));
	return cxt->label->part_delete(cxt, partnum);
}
