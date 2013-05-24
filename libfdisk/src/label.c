
#include "fdiskP.h"

/*
 * Don't use this function derectly, use fdisk_new_context_from_filename()
 */
int fdisk_probe_labels(struct fdisk_context *cxt)
{
	size_t i;

	cxt->label = NULL;

	for (i = 0; i < cxt->nlabels; i++) {
		struct fdisk_label *lb = cxt->labels[i];
		struct fdisk_label *org = cxt->label;
		int rc;

		if (!lb->op->probe)
			continue;

		DBG(LABEL, dbgprint("probing for %s", lb->name));

		cxt->label = lb;
		rc = lb->op->probe(cxt);
		cxt->label = org;

		if (rc != 1) {
			if (lb->op->deinit)
				lb->op->deinit(lb);	/* for sure */
			continue;
		}

		__fdisk_context_switch_label(cxt, lb);
		return 0;
	}

	DBG(LABEL, dbgprint("no label found"));
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
	return cxt && cxt->label;
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
	return cxt && cxt->label && cxt->label->id == l;
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
 * fdisk_list_disklabel:
 * @cxt: fdisk context
 *
 * Lists in-memory partition table
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_list_disklabel(struct fdisk_context *cxt)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->list)
		return -ENOSYS;

	return cxt->label->op->list(cxt);
}

/**
 * fdisk_add_partition:
 * @cxt: fdisk context
 * @t: partition type to create or NULL for label-specific default
 *
 * Creates a new partition, with number @partnum and type @parttype.
 *
 * Returns 0.
 */
int fdisk_add_partition(struct fdisk_context *cxt,
			struct fdisk_parttype *t)
{
	size_t partnum = 0;

	assert(cxt);
	assert(cxt->label);

	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->part_add)
		return -ENOSYS;

	if (!(cxt->label->flags & FDISK_LABEL_FL_ADDPART_NOPARTNO)) {
		int rc = fdisk_ask_partnum(cxt, &partnum, 1);
		if (rc)
			return rc;
	}

	DBG(LABEL, dbgprint("adding new partition number %zd", partnum));
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
int fdisk_delete_partition(struct fdisk_context *cxt, size_t partnum)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->part_delete)
		return -ENOSYS;

	DBG(LABEL, dbgprint("deleting %s partition number %zd",
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
	int haslabel = 0;

	if (!cxt)
		return -EINVAL;

	if (!name) { /* use default label creation */
#ifdef __sparc__
		name = "sun";
#else
		name = "dos";
#endif
	}

	if (cxt->label) {
		fdisk_deinit_label(cxt->label);
		haslabel = 1;
	}

	cxt->label = fdisk_context_get_label(cxt, name);
	if (!cxt->label)
		return -EINVAL;

	DBG(LABEL, dbgprint("changing to %s label\n", cxt->label->name));
	if (!cxt->label->op->create)
		return -ENOSYS;

	if (haslabel)
		fdisk_reset_device_properties(cxt);
	return cxt->label->op->create(cxt);
}

/**
 * fdisk_get_partition_type:
 * @cxt: fdisk context
 * @partnum: partition number
 *
 * Returns partition type or NULL upon failure.
 */
struct fdisk_parttype *fdisk_get_partition_type(struct fdisk_context *cxt,
						size_t partnum)
{
	if (!cxt || !cxt->label || !cxt->label->op->part_get_type)
		return NULL;

	DBG(LABEL, dbgprint("partition: %zd: get type", partnum));
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
int fdisk_set_partition_type(struct fdisk_context *cxt,
			     size_t partnum,
			     struct fdisk_parttype *t)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->part_set_type)
		return -ENOSYS;

	DBG(LABEL, dbgprint("partition: %zd: set type", partnum));
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

/**
 * fdisk_partition_is_used:
 * @cxt: fdisk context
 * @partnum: partition number
 * @status: returns FDISK_PARTSTAT_* flags
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_partition_get_status(struct fdisk_context *cxt,
			       size_t partnum,
			       int *status)
{
	int rc;

	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->part_get_status)
		return -ENOSYS;

	rc = cxt->label->op->part_get_status(cxt, partnum, status);

	DBG(LABEL, dbgprint("partition: %zd: status: 0x%04x [rc=%d]", partnum, *status, rc));
	return rc;
}

/**
 * @cxt: fdisk context
 * @partnum: partition number
 *
 * Returns: 1 on success if partition used otherwise 0.
 */
int fdisk_partition_is_used(struct fdisk_context *cxt, size_t partnum)
{
	int status, rc;

	rc = fdisk_partition_get_status(cxt, partnum, &status);
	if (rc)
		return 0;

	return status & FDISK_PARTSTAT_USED;
}

/**
 * fdisk_partition_taggle_flag:
 * @cxt: fdisk context
 * @partnum: partition number
 * @status: flags
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_partition_toggle_flag(struct fdisk_context *cxt,
			       size_t partnum,
			       unsigned long flag)
{
	int rc;

	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->part_toggle_flag)
		return -ENOSYS;

	rc = cxt->label->op->part_toggle_flag(cxt, partnum, flag);

	DBG(LABEL, dbgprint("partition: %zd: toggle: 0x%04lx [rc=%d]", partnum, flag, rc));
	return rc;
}


/*
 * Resets the current used label driver to initial state
 */
void fdisk_deinit_label(struct fdisk_label *lb)
{
	assert(lb);

	/* private label information */
	if (lb->op->deinit)
		lb->op->deinit(lb);
}

void fdisk_label_set_changed(struct fdisk_label *lb, int changed)
{
	assert(lb);
	lb->changed = changed ? 1 : 0;
}

int fdisk_label_is_changed(struct fdisk_label *lb)
{
	assert(lb);
	return lb ? lb->changed : 0;
}
