
#include "fdiskP.h"

/*
 * Label probing functions.
 */
extern const struct fdisk_label aix_label;
extern const struct fdisk_label dos_label;
extern const struct fdisk_label bsd_label;
extern const struct fdisk_label mac_label;
extern const struct fdisk_label sun_label;
extern const struct fdisk_label sgi_label;
extern const struct fdisk_label gpt_label;

static const struct fdisk_label *labels[] =
{
	&gpt_label,
	&dos_label,
	&sun_label,
	&sgi_label,
	&aix_label,
	&bsd_label,
	&mac_label,
};

/*
 * Don't use this function derectly, use fdisk_new_context_from_filename()
 */
int fdisk_probe_labels(struct fdisk_context *cxt)
{
	size_t i;

	cxt->disklabel = FDISK_DISKLABEL_ANY;

	for (i = 0; i < ARRAY_SIZE(labels); i++) {
		if (!labels[i]->probe || labels[i]->probe(cxt) != 1)
			continue;

		cxt->label = labels[i];

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

	cxt->label = NULL;

	if (!name) { /* use default label creation */
#ifdef __sparc__
		cxt->label = &sun_label;
#else
		cxt->label = &dos_label;
#endif
	} else {
		size_t i;

		for (i = 0; i < ARRAY_SIZE(labels); i++) {
			if (strcmp(name, labels[i]->name) != 0)
				continue;

			cxt->label = labels[i];
			DBG(LABEL, dbgprint("changing to %s label\n", cxt->label->name));
			break;
		}
	}

	if (!cxt->label)
		return -EINVAL;
	if (!cxt->label->create)
		return -ENOSYS;

	fdisk_reset_alignment(cxt);

	return cxt->label->create(cxt);
}
