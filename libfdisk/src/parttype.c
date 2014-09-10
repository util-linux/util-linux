
#include <ctype.h>

#include "nls.h"

#include "fdiskP.h"


/**
 * fdisk_label_get_nparttypes:
 * @lb: label
 *
 * Returns: number of types supported by label.
 */
size_t fdisk_label_get_nparttypes(struct fdisk_label *lb)
{
	if (!lb)
		return 0;
	return lb->nparttypes;
}

/**
 * fdisk_label_get_parttype:
 * @lb: label
 * @n: number
 *
 * Returns: return parttype
 */
const struct fdisk_parttype *fdisk_label_get_parttype(struct fdisk_label *lb, size_t n)
{
	if (!lb || n >= lb->nparttypes)
		return NULL;
	return &lb->parttypes[n];
}

/**
 * fdisk_label_has_code_parttypes:
 * @lb: label
 *
 * Returns: 1 if the label uses code as partition type
 *          identifiers (e.g. MBR) or 0.
 */
int fdisk_label_has_code_parttypes(struct fdisk_label *lb)
{
	assert(lb);

	if (lb->parttypes && lb->parttypes[0].typestr)
		return 0;
	return 1;
}


/**
 * fdisk_label_get_parttype_from_code:
 * @lb: label
 * @code: code to search for
 *
 * Search in lable-specific table of supported partition types by code.
 *
 * Returns: partition type or NULL upon failure or invalid @code.
 */
struct fdisk_parttype *fdisk_label_get_parttype_from_code(
				struct fdisk_label *lb,
				unsigned int code)
{
	size_t i;

	assert(lb);

	if (!lb->nparttypes)
		return NULL;

	for (i = 0; i < lb->nparttypes; i++)
		if (lb->parttypes[i].code == code)
			return &lb->parttypes[i];

	return NULL;
}

/**
 * fdisk_label_get_parttype_from_string:
 * @lb: label
 * @str: string to search for
 *
 * Search in lable-specific table of supported partition types by typestr.
 *
 * Returns: partition type or NULL upon failure or invalid @str.
 */
struct fdisk_parttype *fdisk_label_get_parttype_from_string(
				struct fdisk_label *lb,
				const char *str)
{
	size_t i;

	assert(lb);

	if (!lb->nparttypes)
		return NULL;

	for (i = 0; i < lb->nparttypes; i++)
		if (lb->parttypes[i].typestr
		    && strcasecmp(lb->parttypes[i].typestr, str) == 0)
			return &lb->parttypes[i];

	return NULL;
}

static struct fdisk_parttype *new_parttype(unsigned int code,
				    const char *typestr,
				    const char *name)
{
	struct fdisk_parttype *t= calloc(1, sizeof(*t));

	if (!t)
		return NULL;
	if (typestr) {
		t->typestr = strdup(typestr);
		if (!t->typestr) {
			free(t);
			return NULL;
		}
	}
	t->name = name;
	t->code = code;
	t->flags |= FDISK_PARTTYPE_ALLOCATED;

	DBG(PARTTYPE, ul_debugobj(t, "allocated new %s type", name));
	return t;
}

/**
 * fdisk_new_unknown_parttype:
 * @code: type as number
 * @typestr: type as string

 * Allocates new 'unknown' partition type. Use fdisk_free_parttype() to
 * deallocate.
 *
 * Returns: newly allocated partition type, or NULL upon failure.
 */
struct fdisk_parttype *fdisk_new_unknown_parttype(unsigned int code,
						  const char *typestr)
{
	struct fdisk_parttype *t = new_parttype(code, typestr, _("unknown"));

	if (!t)
		return NULL;
	t->flags |= FDISK_PARTTYPE_UNKNOWN;
	return t;
}

/**
 * fdisk_copy_parttype:
 * @type: type to copy
 *
 * Use fdisk_free_parttype() to deallocate.
 *
 * Returns: newly allocated partition type, or NULL upon failure.
 */
struct fdisk_parttype *fdisk_copy_parttype(const struct fdisk_parttype *type)
{
	return new_parttype(type->code, type->typestr, type->name);
}

/**
 * fdisk_label_parse_parttype:
 * @lb: label
 * @str: string to parse from
 *
 * Returns: pointer to static table of the partition types, or newly allocated
 * partition type for unknown types. It's safe to call fdisk_free_parttype()
 * for all results.
 */
struct fdisk_parttype *fdisk_label_parse_parttype(
				struct fdisk_label *lb,
				const char *str)
{
	struct fdisk_parttype *types, *ret;
	unsigned int code = 0;
	char *typestr = NULL, *end = NULL;

	assert(lb);

	if (!lb->nparttypes)
		return NULL;

	DBG(LABEL, ul_debugobj(lb, "parsing '%s' (%s) partition type",
				str, lb->name));

	types = lb->parttypes;

	if (types[0].typestr == NULL && isxdigit(*str)) {

		errno = 0;
		code = strtol(str, &end, 16);

		if (errno || *end != '\0') {
			DBG(LABEL, ul_debugobj(lb, "parsing failed: %m"));
			return NULL;
		}
		ret = fdisk_label_get_parttype_from_code(lb, code);
		if (ret)
			goto done;
	} else {
		int i;

		/* maybe specified by type string (e.g. UUID) */
		ret = fdisk_label_get_parttype_from_string(lb, str);
		if (ret)
			goto done;

		/* maybe specified by order number */
		errno = 0;
		i = strtol(str, &end, 0);
		if (errno == 0 && *end == '\0' && i > 0
		    && i - 1 < (int) lb->nparttypes) {
			ret = &types[i - 1];
			goto done;
		}
	}

	ret = fdisk_new_unknown_parttype(code, typestr);
done:
	DBG(PARTTYPE, ul_debugobj(ret, "returns parsed '%s' partition type", ret->name));
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
		DBG(PARTTYPE, ul_debugobj(t, "free"));
		free(t->typestr);
		free(t);
	}
}

const char *fdisk_parttype_get_string(const struct fdisk_parttype *t)
{
	assert(t);
	return t->typestr && *t->typestr ? t->typestr : NULL;
}

unsigned int fdisk_parttype_get_code(const struct fdisk_parttype *t)
{
	assert(t);
	return t->code;
}

const char *fdisk_parttype_get_name(const struct fdisk_parttype *t)
{
	assert(t);
	return t->name;
}

/**
 * fdisk_parttype_is_unknown:
 * @t: type
 *
 * Checks for example result from fdisk_label_parse_parttype().
 *
 * Returns: 1 is type is "unknonw" or 0.
 */
int fdisk_parttype_is_unknown(const struct fdisk_parttype *t)
{
	return t && (t->flags & FDISK_PARTTYPE_UNKNOWN) ? 1 : 0;
}
