
#include "fdiskP.h"

/*
 * Don't use this function derectly, use fdisk_new_context_from_filename()
 */
int fdisk_probe_labels(struct fdisk_context *cxt)
{
	size_t i;

	cxt->disklabel = FDISK_DISKLABEL_ANY;

	for (i = 0; i < cxt->nlabels; i++) {
		if (!cxt->labels[i]->op->probe ||
		     cxt->labels[i]->op->probe(cxt) != 1)
			continue;

		cxt->label = cxt->labels[i];

		DBG(LABEL, dbgprint("detected a %s label", cxt->label->name));
		return 0;
	}

	return 1; /* not found */
}


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
	if (!cxt->label->op->write)
		return -ENOSYS;

	return cxt->label->op->write(cxt);
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
	if (!cxt->label->op->verify)
		return -ENOSYS;

	return cxt->label->op->verify(cxt);
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
	if (!cxt->label->op->part_add)
		return -ENOSYS;

	DBG(LABEL, dbgprint("adding new partition number %d", partnum));
	cxt->label->op->part_add(cxt, partnum, t);
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
	if (!cxt->label->op->part_delete)
		return -ENOSYS;

	DBG(LABEL, dbgprint("deleting %s partition number %d",
				cxt->label->name, partnum));
	return cxt->label->op->part_delete(cxt, partnum);
}

/**
 * fdisk_create_disklabel:
 * @cxt: fdisk context
 * @name: label name
 *
 * Creates a new disk label of type @name. If @name is NULL, then it
 * will create a default system label type, either SUN or DOS.
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_create_disklabel(struct fdisk_context *cxt, const char *name)
{
	if (!cxt)
		return -EINVAL;

	if (!name) { /* use default label creation */
#ifdef __sparc__
		name = "sun";
#else
		name = "dos";
#endif
	}

	cxt->label = fdisk_context_get_label(cxt, name);
	if (!cxt->label)
		return -EINVAL;

	DBG(LABEL, dbgprint("changing to %s label\n", cxt->label->name));
	if (!cxt->label->op->create)
		return -ENOSYS;

	fdisk_reset_alignment(cxt);
	return cxt->label->op->create(cxt);
}

/**
 * fdisk_get_partition_type:
 * @cxt: fdisk context
 * @partnum: partition number
 *
 * Returns partition type or NULL upon failure.
 */
struct fdisk_parttype *fdisk_get_partition_type(struct fdisk_context *cxt, int partnum)
{
	if (!cxt || !cxt->label || !cxt->label->op->part_get_type)
		return NULL;

	DBG(LABEL, dbgprint("partition: %d: get type", partnum));
	return cxt->label->op->part_get_type(cxt, partnum);
}

/**
 * fdisk_set_partition_type:
 * @cxt: fdisk context
 * @partnum: partition number
 * @t: new type
 *
 * Returns 0 on success, < 0 on error.
 */
int fdisk_set_partition_type(struct fdisk_context *cxt, int partnum,
			     struct fdisk_parttype *t)
{
	if (!cxt || !cxt->label || !cxt->label->op->part_set_type)
		return -EINVAL;

	DBG(LABEL, dbgprint("partition: %d: set type", partnum));
	return cxt->label->op->part_set_type(cxt, partnum, t);
}

/**
 * fdisk_get_nparttypes:
 * @cxt: fdisk context
 *
 * Returns: number of partition types supported by the current label
 */
size_t fdisk_get_nparttypes(struct fdisk_context *cxt)
{
	if (!cxt || !cxt->label)
		return 0;

	return cxt->label->nparttypes;
}
