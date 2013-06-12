
#include <ctype.h>

#include "nls.h"

#include "fdiskP.h"

/**
 * fdisk_get_parttype_from_code:
 * @cxt: fdisk context
 * @code: code to search for
 *
 * Search in lable-specific table of supported partition types by code.
 *
 * Returns partition type or NULL upon failure or invalid @code.
 */
struct fdisk_parttype *fdisk_get_parttype_from_code(
				struct fdisk_context *cxt,
				unsigned int code)
{
	size_t i;

	if (!fdisk_get_nparttypes(cxt))
		return NULL;

	for (i = 0; i < cxt->label->nparttypes; i++)
		if (cxt->label->parttypes[i].type == code)
			return &cxt->label->parttypes[i];

	return NULL;
}

/**
 * fdisk_get_parttype_from_string:
 * @cxt: fdisk context
 * @str: string to search for
 *
 * Search in lable-specific table of supported partition types by typestr.
 *
 * Returns partition type or NULL upon failure or invalid @str.
 */
struct fdisk_parttype *fdisk_get_parttype_from_string(
				struct fdisk_context *cxt,
				const char *str)
{
	size_t i;

	if (!fdisk_get_nparttypes(cxt))
		return NULL;

	for (i = 0; i < cxt->label->nparttypes; i++)
		if (cxt->label->parttypes[i].typestr
		    &&strcasecmp(cxt->label->parttypes[i].typestr, str) == 0)
			return &cxt->label->parttypes[i];

	return NULL;
}

/**
 * fdisk_new_unknown_parttype:
 * @type: type as number
 * @typestr: type as string

 * Allocates new 'unknown' partition type. Use fdisk_free_parttype() to
 * deallocate.
 *
 * Returns newly allocated partition type, or NULL upon failure.
 */
struct fdisk_parttype *fdisk_new_unknown_parttype(unsigned int type,
						  const char *typestr)
{
	struct fdisk_parttype *t;

	t = calloc(1, sizeof(*t));
	if (!t)
		return NULL;

	if (typestr) {
		t->typestr = strdup(typestr);
		if (!t->typestr) {
			free(t);
			return NULL;
		}
	}
	t->name = _("unknown");
	t->type = type;
	t->flags |= FDISK_PARTTYPE_UNKNOWN | FDISK_PARTTYPE_ALLOCATED;

	DBG(LABEL, dbgprint("allocated new unknown type [%p]", t));
	return t;
}

/**
 * fdisk_parse_parttype:
 * @cxt: fdisk context
 * @str: string to parse from
 *
 * Returns pointer to static table of the partition types, or newly allocated
 * partition type for unknown types. It's safe to call fdisk_free_parttype()
 * for all results.
 */
struct fdisk_parttype *fdisk_parse_parttype(
				struct fdisk_context *cxt,
				const char *str)
{
	struct fdisk_parttype *types, *ret;
	unsigned int code = 0;
	char *typestr = NULL, *end = NULL;

	if (!fdisk_get_nparttypes(cxt))
		return NULL;

	DBG(LABEL, dbgprint("parsing '%s' partition type", str));

	types = cxt->label->parttypes;

	if (types[0].typestr == NULL && isxdigit(*str)) {

		errno = 0;
		code = strtol(str, &end, 16);

		if (errno || *end != '\0') {
			DBG(LABEL, dbgprint("parsing failed: %m"));
			return NULL;
		}
		ret = fdisk_get_parttype_from_code(cxt, code);
		if (ret)
			goto done;
	} else {
		int i;

		/* maybe specified by type string (e.g. UUID) */
		ret = fdisk_get_parttype_from_string(cxt, str);
		if (ret)
			goto done;

		/* maybe specified by order number */
		errno = 0;
		i = strtol(str, &end, 0);
		if (errno == 0 && *end == '\0' && i > 0
		    && i - 1 < (int) fdisk_get_nparttypes(cxt)) {
			ret = &types[i - 1];
			goto done;
		}
	}

	ret = fdisk_new_unknown_parttype(code, typestr);
done:
	DBG(LABEL, dbgprint("returns '%s' partition type", ret->name));
	return ret;
}

/**
 * fdisk_free_parttype:
 * @t: new type
 *
 * Free the @type.
 */
void fdisk_free_parttype(struct fdisk_parttype *t)
{
	if (t && (t->flags & FDISK_PARTTYPE_ALLOCATED)) {
		DBG(LABEL, dbgprint("freeing %p partition type", t));
		free(t->typestr);
		free(t);
	}
}

/**
 * fdisk_is_parttype_string:
 * @cxt: context
 *
 * Returns: 1 if the current label uses strings as partition type
 *          identifiers (e.g. GPT UUIDS) or 0.
 */
int fdisk_is_parttype_string(struct fdisk_context *cxt)
{
	assert(cxt);
	assert(cxt->label);

	if (cxt->label->parttypes && cxt->label->parttypes[0].typestr)
		return 1;
	return 0;
}
