
#include "c.h"
#include "strutils.h"

#include "fdiskP.h"

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

void fdisk_ref_partition(struct fdisk_partition *pa)
{
	if (pa)
		pa->refcount++;
}

void fdisk_unref_partition(struct fdisk_partition *pa)
{
	if (!pa)
		return;

	pa->refcount--;
	if (pa->refcount <= 0) {
		DBG(PART, ul_debugobj(pa, "free"));
		fdisk_reset_partition(pa);
		list_del(&pa->parts);
		free(pa);
	}
}

int fdisk_dump_partition(struct fdisk_partition *pa, FILE *f)
{
	assert(pa);
	assert(f);

	if (pa->partno == FDISK_EMPTY_PARTNO)
		fputs("#  ", f);
	else
		fprintf(f, "#%zu ", pa->partno);

	fprintf(f, "[%p] start=%ju, end=%ju, size=%ju",
		pa, pa->start, pa->end, pa->size);
	if (pa->parent_partno != FDISK_EMPTY_PARTNO)
		fprintf(f, ", parent=%zu", pa->parent_partno);
	if (fdisk_partition_is_freespace(pa))
		fputs(" freespace", f);
	if (fdisk_partition_is_container(pa))
		fputs(" container", f);
	if (fdisk_partition_is_nested(pa))
		fputs(" nested", f);
	fputc('\n', f);
	return 0;
}

int fdisk_partition_set_start(struct fdisk_partition *pa, uint64_t off)
{
	if (!pa)
		return -EINVAL;
	pa->start = off;
	return 0;
}

uint64_t fdisk_partition_get_start(struct fdisk_partition *pa)
{
	return pa ? pa->start : 0;
}

int fdisk_partition_cmp_start(struct fdisk_partition *a,
			      struct fdisk_partition *b)
{
	return a->start - b->start;
}

int fdisk_partition_set_end(struct fdisk_partition *pa, uint64_t off)
{
	if (!pa)
		return -EINVAL;
	pa->end = off;
	pa->size = 0;
	return 0;
}

uint64_t fdisk_partition_get_end(struct fdisk_partition *pa)
{
	return pa ? pa->end : 0;
}


int fdisk_partition_set_size(struct fdisk_partition *pa, uint64_t size)
{
	if (!pa)
		return -EINVAL;
	pa->size = size;
	pa->end = 0;
	return 0;
}

uint64_t fdisk_partition_get_size(struct fdisk_partition *pa)
{
	return pa ? pa->size : 0;
}

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

int fdisk_partition_partno_follow_default(struct fdisk_partition *pa, int enable)
{
	if (!pa)
		return -EINVAL;
	pa->partno_follow_default = enable ? 1 : 0;
	return 0;
}

int fdisk_partition_start_follow_default(struct fdisk_partition *pa, int enable)
{
	if (!pa)
		return -EINVAL;
	pa->start_follow_default = enable ? 1 : 0;
	return 0;
}

int fdisk_partition_end_follow_default(struct fdisk_partition *pa, int enable)
{
	if (!pa)
		return -EINVAL;
	pa->end_follow_default = enable ? 1 : 0;
	return 0;
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
 * @id: column (FDISK_COL_*)
 * @data: returns string with allocated data
 *
 * Returns info about partition converted to printable string.
 *
 * For example
 *
 *      struct fdisk_parition *pa;
 *
 *      fdisk_get_partition(cxt, 0, &pa);
 *	fdisk_partition_to_string(pa, FDISK_COL_UUID, &data);
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
	case FDISK_COL_DEVICE:
		if (pa->freespace)
			p = strdup(_("Free space"));
		else if (cxt->label->flags & FDISK_LABEL_FL_INCHARS_PARTNO)
			rc = asprintf(&p, "%c", (int) pa->partno + 'a');
		else
			p = fdisk_partname(cxt->dev_path, pa->partno + 1);
		break;
	case FDISK_COL_BOOT:
		rc = asprintf(&p, "%c", pa->boot);
		break;
	case FDISK_COL_START:
		x = fdisk_cround(cxt, pa->start);
		rc = pa->start_post ?
				asprintf(&p, "%ju%c", x, pa->start_post) :
				asprintf(&p, "%ju", x);
		break;
	case FDISK_COL_END:
		x = fdisk_cround(cxt, pa->end);
		rc = pa->end_post ?
				asprintf(&p, "%ju%c", x, pa->end_post) :
				asprintf(&p, "%ju", x);
		break;
	case FDISK_COL_SIZE:
	{
		uint64_t sz = pa->size * cxt->sector_size;

		if (fdisk_context_display_details(cxt)) {
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
	case FDISK_COL_CYLINDERS:
		rc = asprintf(&p, "%ju", (uintmax_t)
				fdisk_cround(cxt, pa->size));
		break;
	case FDISK_COL_SECTORS:
		rc = asprintf(&p, "%ju", pa->size);
		break;
	case FDISK_COL_BSIZE:
		rc = asprintf(&p, "%ju", pa->bsize);
		break;
	case FDISK_COL_FSIZE:
		rc = asprintf(&p, "%ju", pa->fsize);
		break;
	case FDISK_COL_CPG:
		rc = asprintf(&p, "%ju", pa->cpg);
		break;
	case FDISK_COL_TYPE:
		p = pa->type && pa->type->name ? strdup(pa->type->name) : NULL;
		break;
	case FDISK_COL_TYPEID:
		if (pa->type && pa->type->typestr)
			rc = asprintf(&p, "%s", pa->type->typestr);
		else if (pa->type)
			rc = asprintf(&p, "%x", pa->type->type);
		break;
	case FDISK_COL_UUID:
		p = pa->uuid ? strdup(pa->uuid) : NULL;
		break;
	case FDISK_COL_NAME:
		p = pa->name ? strdup(pa->name) : NULL;
		break;
	case FDISK_COL_ATTR:
		p = pa->attrs ? strdup(pa->attrs) : NULL;
		break;
	case FDISK_COL_SADDR:
		p = pa->start_addr ? strdup(pa->start_addr) : NULL;
		break;
	case FDISK_COL_EADDR:
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
 * Fills in @pa with data about partition @n.
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
 * @pa: template for the partition
 *
 * If @pa is not specified or any @pa item is missiong the libfdisk will ask by
 * fdisk_ask_ API.
 *
 * Creates a new partition.
 *
 * Returns 0.
 */
int fdisk_add_partition(struct fdisk_context *cxt,
			struct fdisk_partition *pa)
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

	DBG(CXT, ul_debugobj(cxt, "adding new partition (start=%ju, end=%ju, size=%ju, "
			    "defaults(start=%s, end=%s, partno=%s)",
			    pa ? pa->start : 0,
			    pa ? pa->end : 0,
			    pa ? pa->size : 0,
			    pa && pa->start_follow_default ? "yes" : "no",
			    pa && pa->end_follow_default ? "yes" : "no",
			    pa && pa->partno_follow_default ? "yes" : "no"));

	rc = cxt->label->op->add_part(cxt, pa);

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
