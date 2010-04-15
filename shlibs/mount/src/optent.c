/*
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: optent
 * @title: Parsed option
 * @short_description: the mnt_optent keeps one parsed mount option
 */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include "nls.h"
#include "mountP.h"

static int mnt_init_optent(mnt_optent *op, const char *name, size_t namelen,
	                        struct mnt_optmap const **maps, int nmaps);
static int __mnt_optent_set_value(mnt_optent *op, const char *data, size_t len);


/*
 * Returns a new optent.
 */
mnt_optent *mnt_new_optent(	const char *name, size_t namesz,
				const char *value, size_t valsz,
				struct mnt_optmap const **maps, int nmaps)
{
	mnt_optent *op;

	op = calloc(1, sizeof(struct _mnt_optent));
	if (!op)
		return NULL;

	INIT_LIST_HEAD(&op->opts);

	if (mnt_init_optent(op, name, namesz, maps, nmaps))
		goto err;

	if (value) {
		if (__mnt_optent_set_value(op, value, valsz))
			goto err;
	} else if (mnt_optent_require_value(op))
		goto err;

	return op;
err:
	free(op);
	return NULL;
}

/*
 * Deallocates the optent.
 */
void mnt_free_optent(mnt_optent *op)
{
	if (!op)
		return;

	if (!op->mapent || op->mapent->name != op->name)
		free(op->name);

	free(op->value);

	if (!list_empty(&op->opts))
		list_del(&op->opts);

	free(op);
}

/*
 * initialize or reinitialize the option entry -- note that the option
 * name is set to @name and the old name is not free()ed. If the @name
 * is NULL the already existing option name is used.
 */
static int mnt_init_optent(mnt_optent *op, const char *name, size_t namelen,
			struct mnt_optmap const **maps, int nmaps)
{
	const struct mnt_optmap *mapent = NULL, *map = NULL;

	assert(op);

	if (!op)
		return -1;

	if (!name && op->name) {
		name = op->name;
		namelen = strlen(name);
	}
	if (!name)
		return -1;

	if (nmaps && maps)
		map = mnt_optmap_get_entry(maps, nmaps, name, namelen, &mapent);

	if (mapent == NULL || mnt_optmap_get_type(mapent) != NULL) {
		/* we allocate the name for uknown options of for options with
		 * "=%<type>" argument. This is not perfect... */
		if (op->name != name)
			op->name = strndup(name, namelen);
	} else
		op->name = (char *) mapent->name;

	op->mapent = mapent;
	op->map = map;
	op->mask = mapent ? mapent->mask : 0;
	if (op->value)
		op->mask |= MNT_HASVAL;

	if (!op->name)
		return -1;	/* strdup() failed */

	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: option %s: initialized\n", op->name));

	return 0;
}

static int mnt_optent_check_value(mnt_optent *op, const char *data, size_t len)
{
	const char *type;
	char *end = NULL;

	assert(op);
	if (!op)
		return -1;

	type = mnt_optent_get_type(op);
	if (!type)
		goto err;			/* value is unexpected */

	if (!data) {
		if (mnt_optent_require_value(op))
			goto err;
	} else if (!strncmp(type, "%s", 2)) {
		/* string type */
		;
	} else if (*type == '{') {
		/* enum type */
		if (mnt_optmap_enum_to_number(op->mapent, data, len) < 0)
			goto err;
	} else {
		/* numbers */
		int n;		/* happy gcc */

		errno = 0;
		if (!strncmp(type, "%d", 2) || !strncmp(type, "%ld", 3))
			n = strtol(data, &end, 10);
		else if (!strncmp(type, "%u", 2) || !strncmp(type, "%lu", 3))
			n = strtoul(data, &end, 10);
		else if (!strncmp(type, "%lld", 4))
			n = strtoll(data, &end, 10);
		else if (!strncmp(type, "%llu", 4))
			n = strtoull(data, &end, 10);
		else if (!strncmp(type, "%o", 2))
			n = strtoul(data, &end, 8);
		else if (!strncmp(type, "%x", 2))
			n = strtoul(data, &end, 16);

		if (errno == EINVAL || errno == ERANGE || end != data + len)
			goto err;
	}
	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: option %s (type=%s): pass check\n",
		op->name, type));
	return 0;
err:
	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: option %s (type=%s): failed to check value %s\n",
		op->name, type, data));
	return -1;
}

/*
 * Parses the first mount option from @optstr and move @optstr pointer
 * to the next option.
 *
 * Returns new optent (parsed option) or NULL in case of error.
 */
mnt_optent *mnt_new_optent_from_optstr(char **optstr,
			struct mnt_optmap const **maps, int nmaps)
{
	char *name, *value;
	size_t nsz, vsz;

	if (mnt_optstr_next_option(optstr, &name, &nsz, &value, &vsz) == 0)
		return mnt_new_optent(name, nsz, value, vsz, maps, nmaps);

	return NULL;
}

/*
 * Lookups @maps and tries to found corresponding map entry for the @op option.
 * If the map is found the option value is reverified.
 *
 * Returns 0 on success, 1 if map not found, -1 in case of error (revalidation
 * failed or so).
 */
int mnt_optent_assign_map(mnt_optent *op,
			struct mnt_optmap const **maps, int nmaps)
{
	char *oldval, *oldname = NULL;
	const char *type;

	assert(op);
	assert(op->name);

	if (!op || !op->name)
		return -1;

	if (op->mapent && op->name != op->mapent->name)
		oldname = op->name;	/* old name is allocated */

	op->map = op->mapent = NULL;
	oldval = op->value;

	if (mnt_init_optent(op, NULL, 0, maps, nmaps))
		return -1;

	if (op->name != oldname)
		free(oldname);

	if (!op->map)
		return 1;	/* uknown option, it's not error */

	/* the new type */
	type = mnt_optent_get_type(op);

	if (type == NULL && oldval)
		goto err;		/* value is unexpected */
	if (mnt_optent_require_value(op) && !oldval)
		goto err;		/* value is required */
	if (oldval && mnt_optent_check_value(op, oldval, strlen(oldval)) != 0)
		goto err;		/* bad value */

	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: option %s: assigned to \n",	op->name));
	return 0;
err:
	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: option %s: assign failed\n", op->name));
	return -1;
}

/**
 * mnt_optent_get_map:
 * @op: pointer to mnt_optent instance
 *
 * Returns: pointer to the head of the map that is associated with the option or
 * NULL (for "extra options").
 */
const struct mnt_optmap *mnt_optent_get_map(mnt_optent *op)
{
	assert(op);
	return op ? op->map : NULL;
}

/**
 * mnt_optent_get_map_entry:
 * @op: pointer to mnt_optent instance
 *
 * Returns: pointer to the map entry that describes the option or NULL (for
 * "extra options").
 */
const struct mnt_optmap *mnt_optent_get_mapent(mnt_optent *op)
{
	assert(op);
	return op ? op->mapent : NULL;
}

/**
 * mnt_optent_get_type:
 * @op: mnt_optent instance
 *
 * Note that the @op has to be associated with any option map
 * or the default "%s]" is returned.
 *
 * Returns: pointer to the begin of type format string or NULL. For example:
 *
 *  "%s"  --> string, required argument (definition in the map is: "foo=%s")
 *  "%s]" --> string, optional argument (definition in the map is: "foo[=%s]")
 */
const char *mnt_optent_get_type(mnt_optent *op)
{
	assert(op);
	if (!op)
		return NULL;
	return op->mapent ? mnt_optmap_get_type(op->mapent) : "%s]";
}



/**
 * mnt_optent_set_value:
 * @op: mnt_optent instance
 * @data: option argument data or NULL
 *
 * The function unset (zeroize) the option value if the @data pointer is NULL.
 *
 * Returns: 0 on success or -1 in case of error.
 */
int mnt_optent_set_value(mnt_optent *op, const char *data)
{
	return __mnt_optent_set_value(op, data, data ? strlen(data) : 0);
}

static int __mnt_optent_set_value(mnt_optent *op, const char *data, size_t len)
{
	assert(op);
	if (!op)
		return -1;

	free(op->value);
	op->value = NULL;
	op->mask &= ~MNT_HASVAL;

	if (mnt_optent_check_value(op, data, len) != 0)
		goto err;
	if (data) {
		op->value = strndup(data, len);
		if (!op->value)
			goto err;
		op->mask |= MNT_HASVAL;
	}

	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: option %s: set argument value: %s\n",
		op->name, op->value));
	return 0;
err:
	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: option %s: set argument value failed\n",
		op->name));
	return -1;

}

/**
 * mnt_optent_has_value:
 * @option: pointer to mnt_optent instance
 *
 * Returns: 1 if the option has actually set an argument value, or 0.
 */
int mnt_optent_has_value(mnt_optent *op)
{
	return op && (op->mask & MNT_HASVAL) ? 1 : 0;
}

/**
 * mnt_optent_require_value:
 * @op: pointer to mnt_optent instance
 *
 * Note that the @op has to be associated with any option map
 * or 0 is returned.
 *
 * Returns: 1 if the option requires an argument (option=arg).
 */
int mnt_optent_require_value(mnt_optent *op)
{
	return op && op->mapent ? mnt_optmap_require_value(op->mapent) : 0;
}

/**
 * mnt_optent_is_inverted:
 * @op: pointer to mnt_optent instance
 *
 * Returns: 1 if the option has MNT_INVERT mask or 0.
 */
int mnt_optent_is_inverted(mnt_optent *op)
{
	return (op && (op->mask & MNT_INVERT));
}

static int get_number_base(const char *type)
{
	int base = 10;		/* default */

	if (!strncmp(type, "%o", 2))
		base = 8;
	else if (!strncmp(type, "%x", 16))
		base = 16;
	return base;
}

/**
 * mnt_optent_strtoul_value:
 * @op: pointer to mnt_optent instance
 * @number: resulting number
 *
 * Converts an option value to number. The strtoul() base (decimal, octan or
 * hex) is determined from (%u, %o or %x) option format type -- default is
 * decimal (for unknown options).
 *
 * The whole option value has to be possible to convert to the number
 * (e.g "123ABC" returns -1).
 *
 * This function also converts {enum0,enumN} type to number 0..N. For more
 * details see info about option maps.
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_optent_strtoul_value(mnt_optent *op, unsigned long int *number)
{
	const char *type = NULL;
	char *end;
	size_t len;

	if (!mnt_optent_has_value(op) || !number)
		goto err;;
	type = mnt_optent_get_type(op);
	if (!type)
		goto err;

	if (*type == '{') {
		int n;

		if (!op->mapent)
			goto err;
		n = mnt_optmap_enum_to_number(op->mapent, op->value,
				strlen(op->value));
		if (n < 0)
			goto err;
		*number = n;
	} else {
		errno = 0;
		*number = strtoul(op->value, &end, get_number_base(type));

		if (errno == EINVAL || errno == ERANGE)
			goto err;
		len = strlen(op->value);
		if (end != op->value + len)
			goto err;
	}
	return 0;
err:
	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: option %s (type=%s): strtoul failed\n",
		op->name, type));
	return -1;
}

/**
 * mnt_optent_strtol_value:
 * @op: pointer to mnt_optent instance
 * @number: resulting number
 *
 * Converts an option value to number. The strtol() base (decimal, octan or
 * hex) is determined from (%u, %o or %x) option format type -- default is
 * decimal.
 *
 * The whole option value has to be possible to convert to the number
 * (e.g "123ABC" returns -1).
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_optent_strtol_value(mnt_optent *op, long int *number)
{
	const char *type;
	char *end;
	size_t len;

	if (!mnt_optent_has_value(op) || !number)
		return -1;

	type = mnt_optent_get_type(op);
	if (!type)
		goto err;

	errno = 0;
	*number = strtol(op->value, &end, get_number_base(type));

	if (errno == EINVAL || errno == ERANGE)
		goto err;
	len = strlen(op->value);
	if (end != op->value + len)
		goto err;

	return 0;
err:
	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: option %s (type=%s): strtol failed\n",
		op->name, type));
	return -1;
}

/**
 * mnt_optent_strtoull_value:
 * @op: pointer to mnt_optent instance
 * @number: resulting number
 *
 * Converts an option value to number. The strtoull() base (decimal, octan or
 * hex) is determined from (%u, %o or %x) option format type -- default is
 * decimal.
 *
 * The whole option value has to be possible to convert to the number
 * (e.g "123ABC" returns -1).
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_optent_strtoull_value(mnt_optent *op, unsigned long long int *number)
{
	const char *type;
	char *end;
	size_t len;

	if (!mnt_optent_has_value(op) || !number)
		return -1;

	type = mnt_optent_get_type(op);
	if (!type)
		goto err;

	errno = 0;
	*number = strtoull(op->value, &end, get_number_base(type));

	if (errno == EINVAL || errno == ERANGE)
		goto err;
	len = strlen(op->value);
	if (end != op->value + len)
		goto err;
	return 0;
err:
	DBG(DEBUG_OPTIONS, fprintf(stderr,
		"libmount: option %s (type=%s): strtoull failed\n",
		op->name, type));
	return -1;

}

/**
 * mnt_optent_get_value:
 * @op: pointer to mnt_optent instance
 *
 * See also mnt_optent_has_value().
 *
 * Returns: pointer to value or NULL.
 */
const char *mnt_optent_get_value(mnt_optent *op)
{
	return op? op->value : NULL;
}

/**
 * mnt_optent_strlen_value:
 * @op: pointer to mnt_optent instance
 *
 * Returns: length of string that is necessary to print option value or -1 in
 * case of error.
 */
int mnt_optent_strlen_value(mnt_optent *op)
{
	assert(op);

	if (!op)
		return -1;
	if (!mnt_optent_has_value(op))
		return 0;
	return strlen(op->value);
}

/**
 * mnt_optent_snprintf_value:
 * @op: pointer to mnt_optent instance
 * @str: resulting string
 * @size: size of string
 *
 * Returns: number of printed characters or negative number in case of error.
 */
int mnt_optent_snprintf_value(mnt_optent *op, char *str, size_t size)
{
	assert(op);
	assert(str);

	if (!op || !str || !size)
		return -1;
	if (!mnt_optent_has_value(op))
		return -1;

	/* TODO: use extra quotes for SELinux contexts */
	return snprintf(str, size, "%s", op->value);
}

/**
 * mnt_optent_dup_value:
 * @op: pointer to mnt_optent instance
 *
 * Returns: duplicate a option value.
 */
char *mnt_optent_dup_value(mnt_optent *op)
{
	assert(op);

	if (mnt_optent_has_value(op))
		return strdup(op->value);
	return NULL;
}

/**
 * mnt_optent_get_name:
 * @op: pointer to mnt_optent instance
 *
 * Returns: option name or NULL in case of error.
 */
const char *mnt_optent_get_name(mnt_optent *op)
{
	assert(op);
	return op ? op->name : NULL;
}

/**
 * mnt_optent_get_mask:
 * @op: pointer to mnt_optent instance
 *
 * The initial value of the option mask is a copy from map->mask.
 * Note that the mask is NOT a mountflag/ID.
 *
 * Returns: option mask or 0.
 */
int mnt_optent_get_mask(mnt_optent *op)
{
	assert(op);
	return op ? op->mask : 0;
}

/**
 * mnt_optent_get_id:
 * @op: pointer to mnt_optent instance
 *
 * Note that the ID is also mountflag for all options with MNT_MFLAG mask.
 *
 * WARNING: the ID is usually shared between "option" (e.g. exec) and
 * "nooption" (e.g. noexec) -- you have to carefully check for MNT_INVERT in
 * the option mask. See mnt_optent_get_flag().
 *
 * Returns: option ID/mountflag or 0 for extra options (options with undefined
 * options map).
 */
int mnt_optent_get_id(mnt_optent *op)
{
	assert(op);
	return op && op->mapent ? op->mapent->id : 0;
}

/**
 * mnt_optent_get_flag:
 * @op: pointer to mnt_optent instance
 * @flags: resulting flags
 *
 * Adds option ID to @flags or removes the ID from @flags when the option
 * is an inverted option (e.g. "norelatime")
 *
 * <informalexample>
 *  <programlisting>
 *	int flags = 0;
 *
 *	while(mnt_optls_next_option(&itr, opts, map, &op) == 0)
 *		mnt_optent_get_flag(op, &flags);
 *
 *	if (flags & MS_RELATIME)
 *		printf("relatime is set\n");
 *  </programlisting>
 * </informalexample>
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_optent_get_flag(mnt_optent *op, int *flags)
{
	int id;

	assert(op);
	if (!op || !flags)
		return -1;

	id = mnt_optent_get_id(op);
	if (op->mask & MNT_INVERT)
		*flags &= ~id;
	else
		*flags |= id;
	return 0;
}

/**
 * mnt_optent_is_unknown:
 * @op: pointer to mnt_optent instance
 *
 * The "extra options" are unknown options (undefined in any option map)
 *
 * Returns: 1 or 0.
 */
int mnt_optent_is_unknown(mnt_optent *op)
{
	assert(op);
	return op && op->mapent ? 0 : 1;
}

/**
 * mnt_optent_print_debug:
 * @file: output
 * @op: pointer to mnt_optent instance
 *
 * Prints details about the option.
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_optent_print_debug(mnt_optent *op, FILE *file)
{
	const struct mnt_optmap *map;
	const char *type;

	if (!op)
		return -1;

	fprintf(file, "------ option %p (%s):\n", op, mnt_optent_get_name(op));

	fprintf(file, "\tID=0x%x\n", mnt_optent_get_id(op));
	fprintf(file, "\tMASK=%d\n", mnt_optent_get_mask(op));

	map = mnt_optent_get_map(op);
	fprintf(file, "\tMAP=%p\n", map ? map : NULL);

	map = mnt_optent_get_mapent(op);
	fprintf(file, "\tMAPENT=%s\n", map ? map->name : NULL);

	fprintf(file, "\tHAS_VALUE=%s\n",
			mnt_optent_has_value(op) ? "yes" : "not");

	type = mnt_optent_get_type(op);
	fprintf(file, "\tTYPE=%s\n", type ? : "<none>");
	fprintf(file, "\tVALUE=%s\n", op->value);
	return 0;
}
