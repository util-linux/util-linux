
#include "c.h"
#include "strutils.h"

#include "fdiskP.h"

/**
 * fdisk_new_partition:
 *
 * Returns: new instance.
 */
struct fdisk_partition *fdisk_new_partition(void)
{
	struct fdisk_partition *pa = calloc(1, sizeof(*pa));

	pa->refcount = 1;
	INIT_LIST_HEAD(&pa->parts);
	pa->partno = FDISK_EMPTY_PARTNO;
	pa->parent_partno = FDISK_EMPTY_PARTNO;
	DBG(PART, ul_debugobj(pa, "alloc"));
	return pa;
}

/**
 * fdisk_reset_partition:
 * @pa: partition
 *
 * Resets partition content.
 */
void fdisk_reset_partition(struct fdisk_partition *pa)
{
	int ref;

	if (!pa)
		return;

	DBG(PART, ul_debugobj(pa, "reset"));
	ref = pa->refcount;
	fdisk_free_parttype(pa->type);
	free(pa->name);
	free(pa->uuid);
	free(pa->attrs);
	memset(pa, 0, sizeof(*pa));
	pa->partno = FDISK_EMPTY_PARTNO;
	pa->parent_partno = FDISK_EMPTY_PARTNO;
	pa->refcount = ref;
	INIT_LIST_HEAD(&pa->parts);
}

/**
 * fdisk_ref_partition:
 * @tb: partition pointer
 *
 * Incremparts reference counter.
 */
void fdisk_ref_partition(struct fdisk_partition *pa)
{
	if (pa)
		pa->refcount++;
}

/**
 * fdisk_unref_partition:
 * @tb: partition pointer
 *
 * De-incremparts reference counter, on zero the @tb is automatically
 * deallocated.
 */
void fdisk_unref_partition(struct fdisk_partition *pa)
{
	if (!pa)
		return;

	pa->refcount--;
	if (pa->refcount <= 0) {
		fdisk_reset_partition(pa);
		list_del(&pa->parts);
		DBG(PART, ul_debugobj(pa, "free"));
		free(pa);
	}
}

/**
 * fdisk_partition_set_start:
 * @pa: partition
 * @off: offset in sectors
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_partition_set_start(struct fdisk_partition *pa, uint64_t off)
{
	if (!pa)
		return -EINVAL;
	pa->start = off;
	return 0;
}

/**
 * fdisk_partition_get_start:
 * @pa: partition
 *
 * Returns: start offset in sectors
 */
uint64_t fdisk_partition_get_start(struct fdisk_partition *pa)
{
	return pa ? pa->start : 0;
}

/**
 * fdisk_partition_cmp_start:
 * @a: partition
 * @b: partition
 * See fdisk_sort_table().
 */
int fdisk_partition_cmp_start(struct fdisk_partition *a,
			      struct fdisk_partition *b)
{
	return a->start - b->start;
}

/**
 * fdisk_partition_set_end:
 * @pa: partition
 * @off: offset in sectors
 *
 * Sets end offset, and zeroize size.
 *
 * The usual way is to address end of the partition by fdisk_partition_set_size().
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_partition_set_end(struct fdisk_partition *pa, uint64_t off)
{
	if (!pa)
		return -EINVAL;
	pa->end = off;
	pa->size = 0;
	return 0;
}

/**
 * fdisk_partition_get_start:
 * @pa: partition
 *
 * Returns: start offset in sectors
 */
uint64_t fdisk_partition_get_end(struct fdisk_partition *pa)
{
	return pa ? pa->end : 0;
}

/**
 * fdisk_partition_set_size
 * @pa: partition
 * @size: in bytes
 *
 * Sets size, zeroize end offset. See also fdisk_partition_set_end().
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_partition_set_size(struct fdisk_partition *pa, uint64_t size)
{
	if (!pa)
		return -EINVAL;
	pa->size = size;
	pa->end = 0;
	return 0;
}

/**
 * fdisk_partition_get_start:
 * @pa: partition
 *
 * Returns: size in sectors
 */
uint64_t fdisk_partition_get_size(struct fdisk_partition *pa)
{
	return pa ? pa->size : 0;
}

/**
 * fdisk_partition_set_partno
 * @pa: partition
 * @n: partitiion number
 *
 * When @pa used as a tempalate for fdisk_add_partition() when infor label driver 
 * about wanted partition position.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_partition_set_partno(struct fdisk_partition *pa, size_t n)
{
	if (!pa)
		return -EINVAL;
	pa->partno = n;
	return 0;
}

size_t fdisk_partition_get_partno(struct fdisk_partition *pa)
{
	return pa ? pa->partno : (size_t) -1;
}

int fdisk_partition_cmp_partno(struct fdisk_partition *a,
			       struct fdisk_partition *b)
{
	return a->partno - b->partno;
}

int fdisk_partition_set_type(struct fdisk_partition *pa, struct fdisk_parttype *type)
{
	if (!pa)
		return -EINVAL;
	fdisk_free_parttype(pa->type);
	pa->type = type;
	return 0;
}

const struct fdisk_parttype *fdisk_partition_get_type(struct fdisk_partition *pa)
{
	return pa ? pa->type : NULL;
}

int fdisk_partition_set_name(struct fdisk_partition *pa, const char *name)
{
	char *p = NULL;

	if (!pa)
		return -EINVAL;
	if (name) {
	       p = strdup(name);
	       if (!p)
		       return -ENOMEM;
	}
	free(pa->name);
	pa->name = p;
	return 0;
}

const char *fdisk_partition_get_name(struct fdisk_partition *pa)
{
	return pa ? pa->name : NULL;
}

int fdisk_partition_set_uuid(struct fdisk_partition *pa, const char *uuid)
{
	char *p = NULL;

	if (!pa)
		return -EINVAL;
	if (uuid) {
	       p = strdup(uuid);
	       if (!p)
		       return -ENOMEM;
	}
	free(pa->uuid);
	pa->uuid = p;
	return 0;
}

/**
 * fdisk_partition_partno_follow_default
 * @pa: partition
 * @enable: 0|1
 *
 * When @pa used as a tempalate for fdisk_add_partition() when force label driver
 * to add a new partition to the default (next) position.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_partition_partno_follow_default(struct fdisk_partition *pa, int enable)
{
	if (!pa)
		return -EINVAL;
	pa->partno_follow_default = enable ? 1 : 0;
	return 0;
}

/**
 * fdisk_partition_start_follow_default
 * @pa: partition
 * @enable: 0|1
 *
 * When @pa used as a tempalate for fdisk_add_partition() when force label driver
 * to use the first possible space for the new partition.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_partition_start_follow_default(struct fdisk_partition *pa, int enable)
{
	if (!pa)
		return -EINVAL;
	pa->start_follow_default = enable ? 1 : 0;
	return 0;
}

/**
 * fdisk_partition_start_is_default:
 * @pa: partition
 *
 * Returns: 1 if the partition follows default
 */
int fdisk_partition_start_is_default(struct fdisk_partition *pa)
{
	assert(pa);
	return pa->start_follow_default;
}

/**
 * fdisk_partition_start_follow_default
 * @pa: partition
 * @enable: 0|1
 *
 * When @pa used as a tempalate for fdisk_add_partition() when force label driver
 * to use all the possible space for the new partition.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_partition_end_follow_default(struct fdisk_partition *pa, int enable)
{
	if (!pa)
		return -EINVAL;
	pa->end_follow_default = enable ? 1 : 0;
	return 0;
}

/**
 * fdisk_partition_end_is_default:
 * @pa: partition
 *
 * Returns: 1 if the partition follows default
 */
int fdisk_partition_end_is_default(struct fdisk_partition *pa)
{
	assert(pa);
	return pa->end_follow_default;
}

const char *fdisk_partition_get_uuid(struct fdisk_partition *pa)
{
	return pa ? pa->uuid : NULL;
}

const char *fdisk_partition_get_attrs(struct fdisk_partition *pa)
{
	return pa ? pa->attrs : NULL;
}

int fdisk_partition_is_nested(struct fdisk_partition *pa)
{
	return pa && pa->parent_partno != FDISK_EMPTY_PARTNO;
}

int fdisk_partition_is_container(struct fdisk_partition *pa)
{
	return pa && pa->container;
}

int fdisk_partition_get_parent(struct fdisk_partition *pa, size_t *parent)
{
	if (pa && parent)
		*parent = pa->parent_partno;
	else
		return -EINVAL;
	return 0;
}

int fdisk_partition_is_used(struct fdisk_partition *pa)
{
	return pa && pa->used;
}

int fdisk_partition_is_bootable(struct fdisk_partition *pa)
{
	return pa && pa->boot;
}

int fdisk_partition_is_freespace(struct fdisk_partition *pa)
{
	return pa && pa->freespace;
}

int fdisk_partition_next_partno(
		struct fdisk_partition *pa,
		struct fdisk_context *cxt,
		size_t *n)
{
	assert(cxt);
	assert(n);

	if (pa && pa->partno_follow_default) {
		size_t i;

		DBG(PART, ul_debugobj(pa, "next partno (follow default)"));

		for (i = 0; i < cxt->label->nparts_max; i++) {
			if (!fdisk_is_partition_used(cxt, i)) {
				*n = i;
				return 0;
			}
		}
		return -ERANGE;

	} else if (pa && pa->partno != FDISK_EMPTY_PARTNO) {

		DBG(PART, ul_debugobj(pa, "next partno (specified=%zu)", pa->partno));

		if (pa->partno >= cxt->label->nparts_max)
			return -ERANGE;
		*n = pa->partno;
	} else
		return fdisk_ask_partnum(cxt, n, 1);

	return 0;
}

/**
 * fdisk_partition_to_string:
 * @pa: partition
 * @id: field (FDISK_FIELD_*)
 * @data: returns string with allocated data
 *
 * Returns info about partition converted to printable string.
 *
 * For example
 *
 *      struct fdisk_parition *pa;
 *
 *      fdisk_get_partition(cxt, 0, &pa);
 *	fdisk_partition_to_string(pa, FDISK_FIELD_UUID, &data);
 *	printf("first partition uuid: %s\n", data);
 *	free(data);
 *	fdisk_unref_partition(pa);
 *
 * returns UUID for the first partition.
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */

int fdisk_partition_to_string(struct fdisk_partition *pa,
			      struct fdisk_context *cxt,
			      int id,
			      char **data)
{
	char *p = NULL;
	int rc = 0;
	uint64_t x;

	if (!pa || !cxt)
		return -EINVAL;

	switch (id) {
	case FDISK_FIELD_DEVICE:
		if (pa->freespace)
			p = strdup(_("Free space"));
		else if (cxt->label->flags & FDISK_LABEL_FL_INCHARS_PARTNO)
			rc = asprintf(&p, "%c", (int) pa->partno + 'a');
		else
			p = fdisk_partname(cxt->dev_path, pa->partno + 1);
		break;
	case FDISK_FIELD_BOOT:
		rc = asprintf(&p, "%c", pa->boot ? '*' : ' ');
		break;
	case FDISK_FIELD_START:
		x = fdisk_cround(cxt, pa->start);
		rc = pa->start_post ?
				asprintf(&p, "%ju%c", x, pa->start_post) :
				asprintf(&p, "%ju", x);
		break;
	case FDISK_FIELD_END:
		x = fdisk_cround(cxt, pa->end);
		rc = pa->end_post ?
				asprintf(&p, "%ju%c", x, pa->end_post) :
				asprintf(&p, "%ju", x);
		break;
	case FDISK_FIELD_SIZE:
	{
		uint64_t sz = pa->size * cxt->sector_size;

		if (fdisk_is_details(cxt)) {
			rc = pa->size_post ?
					asprintf(&p, "%ju%c", sz, pa->size_post) :
					asprintf(&p, "%ju", sz);
		} else {
			p = size_to_human_string(SIZE_SUFFIX_1LETTER, sz);
			if (!p)
				rc = -ENOMEM;
		}
		break;
	}
	case FDISK_FIELD_CYLINDERS:
		rc = asprintf(&p, "%ju", (uintmax_t)
				fdisk_cround(cxt, pa->size));
		break;
	case FDISK_FIELD_SECTORS:
		rc = asprintf(&p, "%ju", pa->size);
		break;
	case FDISK_FIELD_BSIZE:
		rc = asprintf(&p, "%ju", pa->bsize);
		break;
	case FDISK_FIELD_FSIZE:
		rc = asprintf(&p, "%ju", pa->fsize);
		break;
	case FDISK_FIELD_CPG:
		rc = asprintf(&p, "%ju", pa->cpg);
		break;
	case FDISK_FIELD_TYPE:
		p = pa->type && pa->type->name ? strdup(pa->type->name) : NULL;
		break;
	case FDISK_FIELD_TYPEID:
		if (pa->type && fdisk_parttype_get_string(pa->type))
			rc = asprintf(&p, "%s", fdisk_parttype_get_string(pa->type));
		else if (pa->type)
			rc = asprintf(&p, "%x", fdisk_parttype_get_code(pa->type));
		break;
	case FDISK_FIELD_UUID:
		p = pa->uuid ? strdup(pa->uuid) : NULL;
		break;
	case FDISK_FIELD_NAME:
		p = pa->name ? strdup(pa->name) : NULL;
		break;
	case FDISK_FIELD_ATTR:
		p = pa->attrs ? strdup(pa->attrs) : NULL;
		break;
	case FDISK_FIELD_SADDR:
		p = pa->start_addr ? strdup(pa->start_addr) : NULL;
		break;
	case FDISK_FIELD_EADDR:
		p = pa->end_addr ? strdup(pa->end_addr) : NULL;
		break;
	default:
		return -EINVAL;
	}

	if (rc < 0)
		rc = -ENOMEM;
	else if (rc > 0)
		rc = 0;

	if (data)
		*data = p;
	return rc;
}

/**
 * fdisk_get_partition:
 * @cxt:
 * @partno:
 * @pa: pointer to partition struct
 *
 * Fills in @pa with data about partition @n. Note that partno may address
 * unused partition and then this function does not fill anything to @pa.
 * See fdisk_is_partition_used().
 *
 * Returns: 0 on success, otherwise, a corresponding error.
 */
int fdisk_get_partition(struct fdisk_context *cxt, size_t partno,
			struct fdisk_partition **pa)
{
	int rc;
	struct fdisk_partition *np = NULL;

	if (!cxt || !cxt->label || !pa)
		return -EINVAL;
	if (!cxt->label->op->get_part)
		return -ENOSYS;
	if (!fdisk_is_partition_used(cxt, partno))
		return -EINVAL;

	if (!*pa) {
		np = *pa = fdisk_new_partition();
		if (!*pa)
			return -ENOMEM;
	} else
		fdisk_reset_partition(*pa);

	(*pa)->partno = partno;
	rc = cxt->label->op->get_part(cxt, partno, *pa);

	if (rc) {
		if (np) {
			fdisk_unref_partition(np);
			*pa = NULL;
		} else
			fdisk_reset_partition(*pa);
	}
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
 * fdisk_add_partition:
 * @cxt: fdisk context
 * @pa: template for the partition (or NULL)
 * @partno: returns new partition number (optional)
 *
 * If @pa is not specified or any @pa item is missiong the libfdisk will ask by
 * fdisk_ask_ API.
 *
 * Creates a new partition.
 *
 * Returns 0.
 */
int fdisk_add_partition(struct fdisk_context *cxt,
			struct fdisk_partition *pa,
			size_t *partno)
{
	int rc;

	assert(cxt);
	assert(cxt->label);

	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->add_part)
		return -ENOSYS;
	if (fdisk_missing_geometry(cxt))
		return -EINVAL;

	if (pa)
		DBG(CXT, ul_debugobj(cxt, "adding new partition %p (start=%ju, end=%ju, size=%ju, "
			    "defaults(start=%s, end=%s, partno=%s)",
			    pa,
			    pa->start,
			    pa->end,
			    pa->size,
			    pa->start_follow_default ? "yes" : "no",
			    pa->end_follow_default ? "yes" : "no",
			    pa->partno_follow_default ? "yes" : "no"));
	else
		DBG(CXT, ul_debugobj(cxt, "adding partition"));

	rc = cxt->label->op->add_part(cxt, pa, partno);

	DBG(CXT, ul_debugobj(cxt, "add partition done (rc=%d)", rc));
	return rc;
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

	DBG(CXT, ul_debugobj(cxt, "deleting %s partition number %zd",
				cxt->label->name, partnum));
	return cxt->label->op->part_delete(cxt, partnum);
}

/**
 * fdisk_delete_all_partitions:
 * @cxt: fdisk context
 *
 * Delete all used partitions.
 *
 * Returns: 0 on success, otherwise, a corresponding error.
 */
int fdisk_delete_all_partitions(struct fdisk_context *cxt)
{
	size_t i;
	int rc;

	if (!cxt || !cxt->label)
		return -EINVAL;

	for (i = 0; i < cxt->label->nparts_max; i++) {

		if (!fdisk_is_partition_used(cxt, i))
			continue;
		rc = fdisk_delete_partition(cxt, i);
		if (rc)
			break;
	}

	return rc;
}

