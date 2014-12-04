
#include "c.h"
#include "strutils.h"

#include "fdiskP.h"

/**
 * SECTION: partition
 * @title: Partition
 * @short_description: generic label independent partition abstraction
 *
 * The fdisk_partition provides label independent abstraction. The partitions
 * are not directly connected with partition table (label) data. Any change to
 * fdisk_partition does not affects in-memory or on-disk label data.
 *
 * The fdisk_partition is possible to use as a template for
 * fdisk_add_partition() or fdisk_set_partition() operations.
 */

static void init_partition(struct fdisk_partition *pa)
{
	FDISK_INIT_UNDEF(pa->size);
	FDISK_INIT_UNDEF(pa->start);
	FDISK_INIT_UNDEF(pa->partno);
	FDISK_INIT_UNDEF(pa->parent_partno);
	FDISK_INIT_UNDEF(pa->boot);

	INIT_LIST_HEAD(&pa->parts);
}

/**
 * fdisk_new_partition:
 *
 * Returns: new instance.
 */
struct fdisk_partition *fdisk_new_partition(void)
{
	struct fdisk_partition *pa = calloc(1, sizeof(*pa));

	pa->refcount = 1;
	init_partition(pa);
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

	fdisk_unref_parttype(pa->type);
	free(pa->name);
	free(pa->uuid);
	free(pa->attrs);

	memset(pa, 0, sizeof(*pa));
	pa->refcount = ref;

	init_partition(pa);
}

/**
 * fdisk_ref_partition:
 * @pa: partition pointer
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
 * @pa: partition pointer
 *
 * De-incremparts reference counter, on zero the @pa is automatically
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
 * @off: offset in sectors, maximal is UINT64_MAX-1
 *
 * Note that zero is valid offset too. Use fdisk_partition_unset_start() to
 * undefine the offset. 
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_partition_set_start(struct fdisk_partition *pa, fdisk_sector_t off)
{
	if (!pa)
		return -EINVAL;
	if (FDISK_IS_UNDEF(off))
		return -ERANGE;
	pa->start = off;
	return 0;
}

/**
 * fdisk_partition_unset_start:
 * @pa: partition
 *
 * Sets the size as undefined. See fdisk_partition_has_start().
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_partition_unset_start(struct fdisk_partition *pa)
{
	if (!pa)
		return -EINVAL;
	FDISK_INIT_UNDEF(pa->start);
	return 0;
}

/**
 * fdisk_partition_get_start:
 * @pa: partition
 *
 * The zero is also valid offset. The function may return random undefined
 * value when start offset is undefined (for example after
 * fdisk_partition_unset_start()). Always use fdisk_partition_has_start() to be
 * sure that you work with valid numbers.
 *
 * Returns: start offset in sectors
 */
fdisk_sector_t fdisk_partition_get_start(struct fdisk_partition *pa)
{
	return pa->start;
}

/**
 * fdisk_partition_has_start:
 * @pa: partition
 *
 * Returns: 1 or 0
 */
int fdisk_partition_has_start(struct fdisk_partition *pa)
{
	return pa && !FDISK_IS_UNDEF(pa->start);
}


/**
 * fdisk_partition_cmp_start:
 * @a: partition
 * @b: partition
 *
 * Compares partitons according to start offset, See fdisk_sort_table().
 *
 * Return: 0 if the same, <0 if @b greater, >0 if @a greater.
 */
int fdisk_partition_cmp_start(struct fdisk_partition *a,
			      struct fdisk_partition *b)
{
	int no_a = FDISK_IS_UNDEF(a->start),
	    no_b = FDISK_IS_UNDEF(b->start);

	if (no_a && no_b)
		return 0;
	if (no_a)
		return -1;
	if (no_b)
		return 1;

	return cmp_numbers(a->start, b->start);
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
 * See fdisk_partition_start_follow_default().
 *
 * Returns: 1 if the partition follows default
 */
int fdisk_partition_start_is_default(struct fdisk_partition *pa)
{
	assert(pa);
	return pa->start_follow_default;
}


/**
 * fdisk_partition_set_size:
 * @pa: partition
 * @sz: size in sectors, maximal is UIN64_MAX-1
 *
 * Note that zero is valid size too. Use fdisk_partition_unset_size() to
 * undefine the size.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_partition_set_size(struct fdisk_partition *pa, fdisk_sector_t sz)
{
	if (!pa)
		return -EINVAL;
	if (FDISK_IS_UNDEF(sz))
		return -ERANGE;
	pa->size = sz;
	return 0;
}

/**
 * fdisk_partition_unset_size:
 * @pa: partition
 *
 * Sets the size as undefined. See fdisk_partition_has_size().
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_partition_unset_size(struct fdisk_partition *pa)
{
	if (!pa)
		return -EINVAL;
	FDISK_INIT_UNDEF(pa->size);
	return 0;
}

/**
 * fdisk_partition_get_size:
 * @pa: partition
 *
 * The zero is also valid size. The function may return random undefined
 * value when size is undefined (for example after fdisk_partition_unset_size()).
 * Always use fdisk_partition_has_size() to be sure that you work with valid 
 * numbers.
 *
 * Returns: size offset in sectors
 */
fdisk_sector_t fdisk_partition_get_size(struct fdisk_partition *pa)
{
	return pa->size;
}

/**
 * fdisk_partition_has_size:
 * @pa: partition
 *
 * Returns: 1 or 0
 */
int fdisk_partition_has_size(struct fdisk_partition *pa)
{
	return pa && !FDISK_IS_UNDEF(pa->size);
}

/**
 * fdisk_partition_size_explicit:
 * @pa: partition
 * @enable: 0|1
 *
 * By default libfdisk aligns the size when add the new partition (by
 * fdisk_add_partrition()). If you want to disable this functionality use
 * @enable = 1.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_partition_size_explicit(struct fdisk_partition *pa, int enable)
{
	if (!pa)
		return -EINVAL;
	pa->size_explicit = enable ? 1 : 0;
	return 0;
}

/**
 * fdisk_partition_set_partno:
 * @pa: partition
 * @num: partitin number (0 is the first partition, maximal is SIZE_MAX-1)
 *
 * Note that zero is valid partno too. Use fdisk_partition_unset_partno() to
 * undefine the partno.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_partition_set_partno(struct fdisk_partition *pa, size_t num)
{
	if (!pa)
		return -EINVAL;
	if (FDISK_IS_UNDEF(num))
		return -ERANGE;
	pa->partno = num;
	return 0;
}

/**
 * fdisk_partition_unset_partno:
 * @pa: partition
 *
 * Sets the partno as undefined. See fdisk_partition_has_partno().
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_partition_unset_partno(struct fdisk_partition *pa)
{
	if (!pa)
		return -EINVAL;
	FDISK_INIT_UNDEF(pa->partno);
	return 0;
}

/**
 * fdisk_partition_get_partno:
 * @pa: partition
 *
 * The zero is also valid parition number. The function may return random
 * value when partno is undefined (for example after fdisk_partition_unset_partno()).
 * Always use fdisk_partition_has_partno() to be sure that you work with valid
 * numbers.
 *
 * Returns: partition number (0 is the first partition)
 */
size_t fdisk_partition_get_partno(struct fdisk_partition *pa)
{
	return pa->partno;
}

/**
 * fdisk_partition_has_partno:
 * @pa: partition
 *
 * Returns: 1 or 0
 */
int fdisk_partition_has_partno(struct fdisk_partition *pa)
{
	return pa && !FDISK_IS_UNDEF(pa->partno);
}


/**
 * fdisk_partition_cmp_partno:
 * @a: partition
 * @b: partition
 *
 * Compares partitons according to partition number See fdisk_sort_table().
 *
 * Return: 0 if the same, <0 if @b greater, >0 if @a greater.
 */
int fdisk_partition_cmp_partno(struct fdisk_partition *a,
			       struct fdisk_partition *b)
{
	return a->partno - b->partno;
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
 * fdisk_partition_set_type:
 * @pa: partition
 * @type: partition type
 *
 * Sets parition type.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_partition_set_type(struct fdisk_partition *pa,
			     struct fdisk_parttype *type)
{
	if (!pa)
		return -EINVAL;

	fdisk_ref_parttype(type);
	fdisk_unref_parttype(pa->type);
	pa->type = type;

	return 0;
}

/**
 * fdisk_partition_get_type:
 * @pa: partition
 *
 * Returns: pointer to partition type.
 */
struct fdisk_parttype *fdisk_partition_get_type(struct fdisk_partition *pa)
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
 * fdisk_partition_has_end:
 * @pa: partition
 *
 * Returns: 1 if the partition has defined last sector
 */
int fdisk_partition_has_end(struct fdisk_partition *pa)
{
	return pa && !FDISK_IS_UNDEF(pa->start) && !FDISK_IS_UNDEF(pa->size);
}

/**
 * fdisk_partition_get_end:
 * @pa: partition
 *
 * This function may returns absolute non-sense, always check
 * fdisk_partition_has_end().
 *
 * Note that partition end is defined by fdisk_partition_set_start() and
 * fdisk_partition_set_size().
 *
 * Returns: last partition sector LBA.
 */
fdisk_sector_t fdisk_partition_get_end(struct fdisk_partition *pa)
{
	return pa->start + pa->size - (pa->size == 0 ? 0 : 1);
}

/**
 * fdisk_partition_end_follow_default
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

int fdisk_partition_set_attrs(struct fdisk_partition *pa, const char *attrs)
{
	char *p = NULL;

	if (!pa)
		return -EINVAL;
	if (attrs) {
	       p = strdup(attrs);
	       if (!p)
		       return -ENOMEM;
	}
	free(pa->attrs);
	pa->attrs = p;
	return 0;
}

int fdisk_partition_is_nested(struct fdisk_partition *pa)
{
	return pa && !FDISK_IS_UNDEF(pa->parent_partno);
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
	return pa && pa->boot == 1;
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

	} else if (pa && fdisk_partition_has_partno(pa)) {

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
 * @cxt: context
 * @id: field (FDISK_FIELD_*)
 * @data: returns string with allocated data
 *
 * Returns info about partition converted to printable string.
 *
 * For example
 * <informalexample>
 *   <programlisting>
 *      struct fdisk_parition *pa;
 *
 *      fdisk_get_partition(cxt, 0, &pa);
 *	fdisk_partition_to_string(pa, FDISK_FIELD_UUID, &data);
 *	printf("first partition uuid: %s\n", data);
 *	free(data);
 *	fdisk_unref_partition(pa);
 *   </programlisting>
 * </informalexample>
 *
 * returns UUID for the first partition.
 *
 * Returns: 0 on success, otherwise, a corresponding error.
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
		else if (fdisk_partition_has_partno(pa) && cxt->dev_path) {
			if (cxt->label->flags & FDISK_LABEL_FL_INCHARS_PARTNO)
				rc = asprintf(&p, "%c", (int) pa->partno + 'a');
			else
				p = fdisk_partname(cxt->dev_path, pa->partno + 1);
		}
		break;
	case FDISK_FIELD_BOOT:
		if (fdisk_partition_is_bootable(pa))
			rc = asprintf(&p, "%c", pa->boot ? '*' : ' ');
		break;
	case FDISK_FIELD_START:
		if (fdisk_partition_has_start(pa)) {
			x = fdisk_cround(cxt, pa->start);
			rc = pa->start_post ?
				asprintf(&p, "%ju%c", x, pa->start_post) :
				asprintf(&p, "%ju", x);
		}
		break;
	case FDISK_FIELD_END:
		if (fdisk_partition_has_end(pa)) {
			x = fdisk_cround(cxt, fdisk_partition_get_end(pa));
			rc = pa->end_post ?
					asprintf(&p, "%ju%c", x, pa->end_post) :
					asprintf(&p, "%ju", x);
		}
		break;
	case FDISK_FIELD_SIZE:
		if (fdisk_partition_has_size(pa)) {
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
		}
		break;
	case FDISK_FIELD_CYLINDERS:
		rc = asprintf(&p, "%ju", (uintmax_t)
			fdisk_cround(cxt, fdisk_partition_has_size(pa) ? pa->size : 0));
		break;
	case FDISK_FIELD_SECTORS:
		rc = asprintf(&p, "%ju",
			fdisk_partition_has_size(pa) ? (uintmax_t) pa->size : 0);
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
		p = pa->start_chs ? strdup(pa->start_chs) : NULL;
		break;
	case FDISK_FIELD_EADDR:
		p = pa->end_chs ? strdup(pa->end_chs) : NULL;
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
 * @cxt: context
 * @partno: partition number (0 is the first partition)
 * @pa: returns data about partition
 *
 * Reads disklabel and fills in @pa with data about partition @n.
 *
 * Note that partno may address unused partition and then this function does
 * not fill anything to @pa.  See fdisk_is_partition_used(). If @pa points to
 * NULL then the function allocates a newly allocated fdisk_partition struct,
 * use fdisk_unref_partition() to deallocate.
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
	} else
		(*pa)->size_explicit = 1;
	return rc;
}

/**
 * fdisk_set_partition:
 * @cxt: context
 * @partno: partition number (0 is the first partition)
 * @pa: new partition setting
 *
 * Modifies disklabel according to setting with in @pa.
 *
 * Returns: 0 on success, <0 on error.
 */
int fdisk_set_partition(struct fdisk_context *cxt, size_t partno,
			struct fdisk_partition *pa)
{
	if (!cxt || !cxt->label || !pa)
		return -EINVAL;
	if (!cxt->label->op->set_part)
		return -ENOSYS;

	DBG(CXT, ul_debugobj(cxt, "setting partition %zu %p (start=%ju, end=%ju, size=%ju, "
		    "defaults(start=%s, end=%s, partno=%s)",
		    partno, pa,
		    (uintmax_t) fdisk_partition_get_start(pa),
		    (uintmax_t) fdisk_partition_get_end(pa),
		    (uintmax_t) fdisk_partition_get_size(pa),
		    pa->start_follow_default ? "yes" : "no",
		    pa->end_follow_default ? "yes" : "no",
		    pa->partno_follow_default ? "yes" : "no"));

	return cxt->label->op->set_part(cxt, partno, pa);
}

/**
 * fdisk_add_partition:
 * @cxt: fdisk context
 * @pa: template for the partition (or NULL)
 * @partno: NULL or returns new partition number
 *
 * If @pa is not specified or any @pa item is missiong the libfdisk will ask by
 * fdisk_ask_ API.
 *
 * Adds a new partition to disklabel.
 *
 * Returns: 0 on success, <0 on error.
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
			    (uintmax_t) fdisk_partition_get_start(pa),
			    (uintmax_t) fdisk_partition_get_end(pa),
			    (uintmax_t) fdisk_partition_get_size(pa),
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
 * @partno: partition number to delete (0 is the first partition)
 *
 * Deletes a @partno partition from disklabel.
 *
 * Returns: 0 on success, <0 on error
 */
int fdisk_delete_partition(struct fdisk_context *cxt, size_t partno)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->del_part)
		return -ENOSYS;

	DBG(CXT, ul_debugobj(cxt, "deleting %s partition number %zd",
				cxt->label->name, partno));
	return cxt->label->op->del_part(cxt, partno);
}

/**
 * fdisk_delete_all_partitions:
 * @cxt: fdisk context
 *
 * Delete all used partitions from disklabel.
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

/**
 * fdisk_is_partition_used:
 * @cxt: context
 * @n: partition number (0 is the first partition)
 *
 * This is faster than fdisk_get_partition() + fdisk_partition_is_used().
 *
 * Returns: 0 or 1
 */
int fdisk_is_partition_used(struct fdisk_context *cxt, size_t n)
{
	if (!cxt || !cxt->label)
		return -EINVAL;
	if (!cxt->label->op->part_is_used)
		return -ENOSYS;

	return cxt->label->op->part_is_used(cxt, n);
}

