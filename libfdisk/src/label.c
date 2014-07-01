
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
		if (lb->disabled) {
			DBG(CXT, ul_debugobj(cxt, "%s: disabled -- ignore", lb->name));
			continue;
		}
		DBG(CXT, ul_debugobj(cxt, "probing for %s", lb->name));

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

	DBG(CXT, ul_debugobj(cxt, "no label found"));
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
	if (!cxt || !cxt->label || cxt->readonly)
		return -EINVAL;
	if (!cxt->label->op->write)
		return -ENOSYS;
	return cxt->label->op->write(cxt);
}

int fdisk_require_geometry(struct fdisk_context *cxt)
{
	assert(cxt);

	return cxt->label
	       && cxt->label->flags & FDISK_LABEL_FL_REQUIRE_GEOMETRY ? 1 : 0;
}

int fdisk_missing_geometry(struct fdisk_context *cxt)
{
	int rc;

	assert(cxt);

	rc = (fdisk_require_geometry(cxt) &&
		    (!cxt->geom.heads || !cxt->geom.sectors
				      || !cxt->geom.cylinders));

	if (rc && !fdisk_context_listonly(cxt))
		fdisk_warnx(cxt, _("Incomplete geometry setting."));

	return rc;
}

/**
 * fdisk_get_columns:
 * @cxt: fdisk context
 * @all: 1 or 0
 * @cols: returns allocated array with FDISK_COL_* IDs
 * @ncols: returns number of items in cols
 *
 * This function returns the default or all columns for the current label.  The
 * library uses the columns for list operations (see fdisk_list_disklabel() and
 * fdisk_list_partitions()). Note that the set of the default columns depends
 * on fdisk_context_enable_details() function. If the details are eanable then
 * this function usually returns more columns.
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_get_columns(struct fdisk_context *cxt, int all, int **cols, size_t *ncols)
{
	size_t i, n;
	int *c;

	assert(cxt);

	if (!cxt->label)
		return -EINVAL;
	if (!cxt->label->columns || !cxt->label->ncolumns)
		return -ENOSYS;
	c = calloc(cxt->label->ncolumns, sizeof(int));
	if (!c)
		return -ENOMEM;
	for (n = 0, i = 0; i < cxt->label->ncolumns; i++) {
		int id = cxt->label->columns[i].id;

		if (!all &&
		    ((fdisk_context_display_details(cxt) &&
				(cxt->label->columns[i].flags & FDISK_COLFL_EYECANDY))
		     || (!fdisk_context_display_details(cxt) &&
				(cxt->label->columns[i].flags & FDISK_COLFL_DETAIL))
		     || (id == FDISK_COL_SECTORS &&
			         fdisk_context_use_cylinders(cxt))
		     || (id == FDISK_COL_CYLINDERS &&
			         !fdisk_context_use_cylinders(cxt))))
			continue;

		c[n++] = id;
	}
	if (cols)
		*cols = c;
	else
		free(c);
	if (ncols)
		*ncols = n;
	return 0;
}

const struct fdisk_column *fdisk_label_get_column(
					struct fdisk_label *lb, int id)
{
	size_t i;

	assert(lb);
	assert(id > 0);

	for (i = 0; i < lb->ncolumns; i++) {
		if (lb->columns[i].id == id)
			return &lb->columns[i];
	}

	return NULL;
}

/**
 * fdisk_verify_disklabel:
 * @cxt: fdisk context
 *
 * Verifies the partition table.
 *
 * Returns: 0 on success, otherwise, a corresponding error.
 */
int fdisk_verify_disklabel(struct fdisk_context *cxt)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->verify)
		return -ENOSYS;
	if (fdisk_missing_geometry(cxt))
		return -EINVAL;

	return cxt->label->op->verify(cxt);
}



/**
 * fdisk_list_disklabel:
 * @cxt: fdisk context
 *
 * Lists details about disklabel, but no partitions.
 *
 * This function uses libfdisk ASK interface to print data. The details about
 * partitions table are printed by FDISK_ASKTYPE_INFO.
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
	struct fdisk_label *lb;

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

	lb = fdisk_context_get_label(cxt, name);
	if (!lb || lb->disabled)
		return -EINVAL;
	if (!lb->op->create)
		return -ENOSYS;

	__fdisk_context_switch_label(cxt, lb);

	if (haslabel && !cxt->parent)
		fdisk_reset_device_properties(cxt);

	DBG(CXT, ul_debugobj(cxt, "create a new %s label", lb->name));
	return cxt->label->op->create(cxt);
}


int fdisk_locate_disklabel(struct fdisk_context *cxt, int n, const char **name,
			   off_t *offset, size_t *size)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->locate)
		return -ENOSYS;

	DBG(CXT, ul_debugobj(cxt, "locating %d chunk of %s.", n, cxt->label->name));
	return cxt->label->op->locate(cxt, n, name, offset, size);
}


/**
 * fdisk_get_disklabel_id:
 * @cxt: fdisk context
 * @id: returns pointer to allocated string
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_get_disklabel_id(struct fdisk_context *cxt, char **id)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->get_id)
		return -ENOSYS;

	DBG(CXT, ul_debugobj(cxt, "asking for disk %s ID", cxt->label->name));
	return cxt->label->op->get_id(cxt, id);
}

/**
 * fdisk_get_disklabel_id:
 * @cxt: fdisk context
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_set_disklabel_id(struct fdisk_context *cxt)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->set_id)
		return -ENOSYS;

	DBG(CXT, ul_debugobj(cxt, "setting %s disk ID", cxt->label->name));
	return cxt->label->op->set_id(cxt);
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

	DBG(CXT, ul_debugobj(cxt, "partition: %zd: set type", partnum));
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

	DBG(CXT, ul_debugobj(cxt, "partition: %zd: toggle: 0x%04lx [rc=%d]", partnum, flag, rc));
	return rc;
}

/**
 * fdisk_reorder_partitions
 * @cxt: fdisk context
 *
 * Sort partitions according to the partition start sector.
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_reorder_partitions(struct fdisk_context *cxt)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->reorder)
		return -ENOSYS;

	return cxt->label->op->reorder(cxt);
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

void fdisk_label_set_disabled(struct fdisk_label *lb, int disabled)
{
	assert(lb);

	DBG(LABEL, ul_debug("%s label %s",
				lb->name,
				disabled ? "DISABLED" : "ENABLED"));
	lb->disabled = disabled ? 1 : 0;
}

int fdisk_label_is_disabled(struct fdisk_label *lb)
{
	assert(lb);
	return lb ? lb->disabled : 0;
}
