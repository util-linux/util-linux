
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
			DBG(LABEL, dbgprint("%s disabled -- ignore", lb->name));
			continue;
		}
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
 * @cols: returns allocated array with FDISK_COL_*
 * @ncols: returns number of items in cols
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_get_columns(struct fdisk_context *cxt, int **cols, size_t *ncols)
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
		if (cxt->label->columns[i].detail
		    && !fdisk_context_display_details(cxt))
			continue;
		c[n++] = cxt->label->columns[i].id;
	}
	if (cols)
		*cols = c;
	if (ncols)
		*ncols = n;
	return 0;
}

static const struct fdisk_column *fdisk_label_get_column(
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
 * fdisk_get_partition:
 * @cxt:
 * @partno:
 * @pa: pointer to partition struct
 *
 * Fills in @pa with data about partition @n.
 *
 * Returns: 0 on success, otherwise, a corresponding error.
 */
int fdisk_get_partition(struct fdisk_context *cxt, size_t partno,
			struct fdisk_partition *pa)
{
	int rc;

	if (!cxt || !cxt->label || !pa)
		return -EINVAL;
	if (!cxt->label->op->get_part)
		return -ENOSYS;

	fdisk_reset_partition(pa);
	pa->cxt = cxt;
	pa->partno = partno;

	rc = cxt->label->op->get_part(cxt, partno, pa);
	if (rc == 0 && fdisk_partition_is_used(pa))
		DBG(LABEL, dbgprint("get partition %zu", partno));
	return rc;
}

/*
 * This is faster than fdisk_get_partition() + fdisk_partition_is_used()
 */
int fdisk_is_partition_used(struct fdisk_context *cxt, size_t n)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->part_is_used)
		return -ENOSYS;

	return cxt->label->op->part_is_used(cxt, n);
}

/**
 * fdisk_list_disklabel:
 * @cxt: fdisk context
 *
 * Lists in-memory partition table and all related details.
 *
 * This function uses libfdisk ASK interface to print data. The details about
 * partitions table are printed by FDISK_ASKTYPE_INFO and partitions by
 * FDISK_ASKTYPE_TABLE. The default columns are printed.
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
 * fdisk_list_partitions
 * @cxt: fdisk context
 * @cols: array with wanted FDISK_COL_* columns
 * @ncols: number of items in the cols array
 *
 * This is subset of fdisk_list_disklabel(), this function lists really
 * only partitons by FDISK_ASKTYPE_TABLE interface.
 *
 * If no @cols are specified then the default is printed (see
 * fdisk_label_get_columns() for the default columns).

 * Returns 0 on success, otherwise, a corresponding error.
 */

int fdisk_list_partitions(struct fdisk_context *cxt, int *cols, size_t ncols)
{
	int *org = cols, rc = 0;
	struct tt *tb = NULL;
	const struct fdisk_column *col;
	struct fdisk_partition *pa = NULL;
	size_t i, j;

	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->get_part)
		return -ENOSYS;

	DBG(LABEL, dbgprint("list partitions"));

	if (!cols || !ncols) {
		rc = fdisk_get_columns(cxt, &cols, &ncols);
		if (rc)
			return rc;
	}

	tb = tt_new_table(TT_FL_FREEDATA);
	if (!tb) {
		rc = -ENOMEM;
		goto done;
	}
	pa = fdisk_new_partition();
	if (!pa) {
		rc = -ENOMEM;
		goto done;
	}

	/* define table columns */
	for (j = 0; j < ncols; j++) {
		col = fdisk_label_get_column(cxt->label, cols[j]);
		if (!col)
			continue;
		tt_define_column(tb, col->name, col->width, col->flags);
	}

	/* generate per-partition lines into table */
	for (i = 0; i < cxt->label->nparts_max; i++) {
		struct tt_line *ln;

		rc = fdisk_get_partition(cxt, i, pa);
		if (rc)
			continue;
		if (!fdisk_partition_is_used(pa))
			continue;
		ln = tt_add_line(tb, NULL);
		if (!ln)
			continue;

		/* set data for the columns */
		for (j = 0; j < ncols; j++) {
			char *data = NULL;

			col = fdisk_label_get_column(cxt->label, cols[j]);
			if (!col)
				continue;
			rc = fdisk_partition_to_string(pa, col->id, &data);
			if (rc)
				continue;
			tt_line_set_data(ln, j, data);
		}
	}

	if (!tt_is_empty(tb))
		rc = fdisk_print_table(cxt, tb);
	else
		DBG(LABEL, dbgprint("table empty, not list"));
done:
	if (org != cols)
		free(cols);
	tt_free_table(tb);
	fdisk_free_partition(pa);
	return rc;
}

/**
 * fdisk_add_partition:
 * @cxt: fdisk context
 * @t: partition type to create or NULL for label-specific default
 *
 * Creates a new partition with type @parttype.
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
	if (fdisk_missing_geometry(cxt))
		return -EINVAL;

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

	DBG(LABEL, dbgprint("create a new %s label", lb->name));
	return cxt->label->op->create(cxt);
}


int fdisk_locate_disklabel(struct fdisk_context *cxt, int n, const char **name,
			   off_t *offset, size_t *size)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->locate)
		return -ENOSYS;

	DBG(LABEL, dbgprint("locating %d chunk of %s.", n, cxt->label->name));
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

	DBG(LABEL, dbgprint("asking for disk %s ID", cxt->label->name));
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

	DBG(LABEL, dbgprint("setting %s disk ID", cxt->label->name));
	return cxt->label->op->set_id(cxt);
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

void fdisk_label_set_disabled(struct fdisk_label *lb, int disabled)
{
	assert(lb);

	DBG(LABEL, dbgprint("%s label %s",
				lb->name,
				disabled ? "DISABLED" : "ENABLED"));
	lb->disabled = disabled ? 1 : 0;
}

int fdisk_label_is_disabled(struct fdisk_label *lb)
{
	assert(lb);
	return lb ? lb->disabled : 0;
}
