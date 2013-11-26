
#include "c.h"
#include "strutils.h"

#include "fdiskP.h"

struct fdisk_partition *fdisk_new_partition(void)
{
	struct fdisk_partition *pa = calloc(1, sizeof(*pa));

	DBG(PART, dbgprint("new %p", pa));
	return pa;
}

void fdisk_reset_partition(struct fdisk_partition *pa)
{
	if (!pa)
		return;
	fdisk_free_parttype(pa->type);
	free(pa->name);
	free(pa->uuid);
	free(pa->attrs);
	memset(pa, 0, sizeof(*pa));
}

void fdisk_free_partition(struct fdisk_partition *pa)
{
	if (!pa)
		return;
	fdisk_reset_partition(pa);
	DBG(PART, dbgprint("free %p", pa));
	free(pa);
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

int fdisk_partition_set_end(struct fdisk_partition *pa, uint64_t off, int isrel)
{
	if (!pa)
		return -EINVAL;
	pa->end = off;
	pa->size = 0;
	pa->endrel = isrel ? 1 : 0;
	return 0;
}

uint64_t fdisk_partition_get_end(struct fdisk_partition *pa)
{
	return pa ? pa->start : 0;
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

int fdisk_partition_set_type(struct fdisk_partition *pa, struct fdisk_parttype *type)
{
	if (!pa)
		return -EINVAL;
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

const char *fdisk_partition_get_uuid(struct fdisk_partition *pa)
{
	return pa ? pa->uuid : NULL;
}

const char *fdisk_partition_get_attrs(struct fdisk_partition *pa)
{
	return pa ? pa->attrs : NULL;
}

/* nested partition means logical (within extended partition) */
int fdisk_partition_set_nested(struct fdisk_partition *pa, int nested)
{
	if (!pa)
		return -EINVAL;
	pa->nested = nested ? 1 : 0;
	return 0;
}

int fdisk_partition_is_nested(struct fdisk_partition *pa)
{
	return pa && pa->nested;
}

int fdisk_partition_is_used(struct fdisk_partition *pa)
{
	return pa && pa->used;
}


/**
 * fdisk_partition_to_string:
 * @pa: partition
 * @id: column (FDISK_COL_*)
 * @data: returns string with allocated data
 *
 * Returns info about partition converted to printable string.
 *
 * For exmaple
 *
 *      struct fdisk_parition *pa = fdisk_new_partition();
 *
 *      fdisk_get_partition(cxt, 0, &pa);
 *	fdisk_partition_to_string(pa, FDISK_COL_UUID, &data);
 *	printf("first partition uuid: %s\n", data);
 *	free(data);
 *	fdisk_free_partition(pa);
 *
 * returns UUID for the first partition.
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */

int fdisk_partition_to_string(struct fdisk_partition *pa,
			      int id,
			      char **data)
{
	char *p = NULL;
	int rc = 0;

	if (!pa || !pa->cxt)
		return -EINVAL;

	switch (id) {
	case FDISK_COL_DEVICE:
		if (pa->cxt->label->flags & FDISK_LABEL_FL_INCHARS_PARTNO)
			rc = asprintf(&p, "%c", (int) pa->partno + 'a');
		else
			p = fdisk_partname(pa->cxt->dev_path, pa->partno + 1);
		break;
	case FDISK_COL_START:
		rc = pa->start_post ?
				asprintf(&p, "%ju%c", pa->start, pa->start_post) :
				asprintf(&p, "%ju", pa->start);
		break;
	case FDISK_COL_END:
		rc = pa->end_post ?
				asprintf(&p, "%ju%c", pa->end, pa->end_post) :
				asprintf(&p, "%ju", pa->end);
		break;
	case FDISK_COL_SIZE:
		if (fdisk_context_display_details(pa->cxt)) {
			rc = pa->size_post ?
					asprintf(&p, "%ju%c", pa->size, pa->size_post) :
					asprintf(&p, "%ju", pa->size);
		} else {
			p = size_to_human_string(SIZE_SUFFIX_1LETTER, pa->size);
			if (!p)
				rc = -ENOMEM;
		}
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
	case FDISK_COL_UUID:
		p = pa->uuid ? strdup(pa->uuid) : NULL;
		break;
	case FDISK_COL_NAME:
		p = pa->name ? strdup(pa->name) : NULL;
		break;
	case FDISK_COL_ATTR:
		p = pa->attrs ? strdup(pa->attrs) : NULL;
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
