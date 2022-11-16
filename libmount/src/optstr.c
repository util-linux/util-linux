/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * This file is part of libmount from util-linux project.
 *
 * Copyright (C) 2009-2018 Karel Zak <kzak@redhat.com>
 *
 * libmount is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 */

/**
 * SECTION: optstr
 * @title: Options string
 * @short_description: low-level API for working with mount options
 *
 * This is a simple and low-level API to working with mount options that are stored
 * in a string.
 */
#include <ctype.h>

#ifdef HAVE_LIBSELINUX
#include <selinux/selinux.h>
#include <selinux/context.h>
#endif

#include "strutils.h"
#include "mountP.h"
#include "buffer.h"

/*
 * Option location
 */
struct libmnt_optloc {
	char	*begin;
	char	*end;
	char	*value;
	size_t	valsz;
	size_t  namesz;
};

#define MNT_INIT_OPTLOC	{ .begin = NULL }

#define mnt_optmap_entry_novalue(e) \
		(e && (e)->name && !strchr((e)->name, '=') && !((e)->mask & MNT_PREFIX))

/*
 * Locates the first option that matches @name. The @end is set to the
 * char behind the option (it means ',' or \0).
 * @opt_sep: comma or custom options separator
 *
 * Returns negative number on parse error, 1 when not found and 0 on success.
 */
static int mnt_optstr_locate_option(char *optstr, const char *name,
					struct libmnt_optloc *ol, char *opt_sep)
{
	char *n;
	size_t namesz, nsz;
	int rc;

	if (!optstr || !opt_sep)
		return 1;

	assert(name);

	namesz = strlen(name);

	do {
		rc = ul_optstr_next(&optstr, &n, &nsz,
					&ol->value, &ol->valsz, opt_sep);
		if (rc)
			break;

		if (namesz == nsz && strncmp(n, name, nsz) == 0) {
			ol->begin = n;
			ol->end = *(optstr - 1) == opt_sep[0] ? optstr - 1 : optstr;
			ol->namesz = nsz;
			return 0;
		}
	} while(1);

	return rc;
}

/**
 * mnt_optstr_next_option_sep:
 * @optstr: option string, returns the position of the next option
 * @name: returns the option name
 * @namesz: returns the option name length
 * @value: returns the option value or NULL
 * @valuesz: returns the option value length or zero
 * @opt_sep: comma or custom options separator (can change)
 *
 * Parses the first option in @optstr.
 *
 * Returns: 0 on success, 1 at the end of @optstr or negative number in case of
 * error.
 */
int mnt_optstr_next_option_sep(char **optstr, char **name, size_t *namesz,
					char **value, size_t *valuesz, char *opt_sep)
{
	if (!optstr || !*optstr)
		return -EINVAL;

	return ul_optstr_next(optstr, name, namesz, value, valuesz, opt_sep);
}

/**
 * mnt_optstr_next_option:
 * @optstr: option string, returns the position of the next option
 * @name: returns the option name
 * @namesz: returns the option name length
 * @value: returns the option value or NULL
 * @valuesz: returns the option value length or zero
 *
 * Parses the first option in @optstr.
 *
 * Returns: 0 on success, 1 at the end of @optstr or negative number in case of
 * error.
 */
int mnt_optstr_next_option(char **optstr, char **name, size_t *namesz,
					char **value, size_t *valuesz)
{
	char opt_sep[2] = ",";
	return mnt_optstr_next_option_sep(optstr, name, namesz, value, valuesz,
		opt_sep);
}

static int __buffer_append_option(struct ul_buffer *buf,
			const char *name, size_t namesz,
			const char *val, size_t valsz,
			const char *opt_sep)
{
	int rc = 0;

	if (!ul_buffer_is_empty(buf))
		rc = ul_buffer_append_data(buf, opt_sep, 1);
	if (!rc)
		rc = ul_buffer_append_data(buf, name, namesz);
	if (val && !rc) {
		/* we need to append '=' is value is empty string, see
		 * 727c689908c5e68c92aa1dd65e0d3bdb6d91c1e5 */
		rc = ul_buffer_append_data(buf, "=", 1);
		if (!rc && valsz)
			rc = ul_buffer_append_data(buf, val, valsz);
	}
	return rc;
}

/**
 * mnt_optstr_append_option_sep:
 * @optstr: option string or NULL, returns a reallocated string
 * @name: value name
 * @value: value
 * @opt_sep: comma or custom options separator
 *
 * Returns: 0 on success or <0 in case of error. After an error the @optstr should
 *          be unmodified.
 */
int mnt_optstr_append_option_sep(char **optstr, const char *name,
	const char *value, const char *opt_sep)
{
	struct ul_buffer buf = UL_INIT_BUFFER;
	int rc;
	size_t nsz, vsz, osz;

	if (!optstr || !opt_sep)
		return -EINVAL;
	if (!name || !*name)
		return 0;

	nsz = strlen(name);
	osz = *optstr ? strlen(*optstr) : 0;
	vsz = value ? strlen(value) : 0;

	ul_buffer_refer_string(&buf, *optstr);
	ul_buffer_set_chunksize(&buf, osz + nsz + vsz + 3);	/* to call realloc() only once */

	rc = __buffer_append_option(&buf, name, nsz, value, vsz, opt_sep);

	*optstr = ul_buffer_get_data(&buf, NULL, NULL);
	return rc;
}

/**
 * mnt_optstr_append_option:
 * @optstr: option string or NULL, returns a reallocated string
 * @name: value name
 * @value: value
 *
 * Returns: 0 on success or <0 in case of error. After an error the @optstr should
 *          be unmodified.
 */
int mnt_optstr_append_option(char **optstr, const char *name, const char *value)
{
	char opt_sep[2] = ",";
	return mnt_optstr_append_option_sep(optstr, name, value, opt_sep);
}

/**
 * mnt_optstr_prepend_option_sep:
 * @optstr: option string or NULL, returns a reallocated string
 * @name: value name
 * @value: value
 * @opt_sep: comma or custom options separator
 *
 * Returns: 0 on success or <0 in case of error. After an error the @optstr should
 *          be unmodified.
 */
int mnt_optstr_prepend_option_sep(char **optstr, const char *name,
	const char *value, const char *opt_sep)
{
	struct ul_buffer buf = UL_INIT_BUFFER;
	size_t nsz, vsz, osz;
	int rc;

	if (!optstr || !opt_sep)
		return -EINVAL;
	if (!name || !*name)
		return 0;

	nsz = strlen(name);
	osz = *optstr ? strlen(*optstr) : 0;
	vsz = value ? strlen(value) : 0;

	ul_buffer_set_chunksize(&buf, osz + nsz + vsz + 3);   /* to call realloc() only once */

	rc = __buffer_append_option(&buf, name, nsz, value, vsz, opt_sep);
	if (*optstr && !rc) {
		rc = ul_buffer_append_data(&buf, opt_sep, 1);
		if (!rc)
			rc = ul_buffer_append_data(&buf, *optstr, osz);
		free(*optstr);
	}

	*optstr = ul_buffer_get_data(&buf, NULL, NULL);
	return rc;
}

/**
 * mnt_optstr_prepend_option:
 * @optstr: option string or NULL, returns a reallocated string
 * @name: value name
 * @value: value
 *
 * Returns: 0 on success or <0 in case of error. After an error the @optstr should
 *          be unmodified.
 */
int mnt_optstr_prepend_option(char **optstr, const char *name, const char *value)
{
	char opt_sep[2] = ",";
	return mnt_optstr_prepend_option_sep(optstr, name, value, opt_sep);
}

/**
 * mnt_optstr_get_option_sep:
 * @optstr: string with a comma separated list of options
 * @name: requested option name
 * @value: returns a pointer to the beginning of the value (e.g. name=VALUE) or NULL
 * @valsz: returns size of the value or 0
 * @opt_sep: comma or custom options separator
 *
 * Returns: 0 on success, 1 when not found the @name or negative number in case
 * of error.
 */
int mnt_optstr_get_option_sep(const char *optstr, const char *name,
			  char **value, size_t *valsz, char *opt_sep)
{
	struct libmnt_optloc ol = MNT_INIT_OPTLOC;
	int rc;

	if (!optstr || !name || !opt_sep)
		return -EINVAL;

	rc = mnt_optstr_locate_option((char *) optstr, name, &ol, opt_sep);
	if (!rc) {
		if (value)
			*value = ol.value;
		if (valsz)
			*valsz = ol.valsz;
	}
	return rc;
}

/**
 * mnt_optstr_get_option:
 * @optstr: string with a comma separated list of options
 * @name: requested option name
 * @value: returns a pointer to the beginning of the value (e.g. name=VALUE) or NULL
 * @valsz: returns size of the value or 0
 *
 * Returns: 0 on success, 1 when not found the @name or negative number in case
 * of error.
 */
int mnt_optstr_get_option(const char *optstr, const char *name,
			  char **value, size_t *valsz)
{
	char opt_sep[2] = ",";
	return mnt_optstr_get_option_sep(optstr, name, value, valsz, opt_sep);
}

/**
 * mnt_optstr_deduplicate_option_sep:
 * @optstr: string with a comma separated list of options
 * @name: requested option name
 * @opt_sep: comma or custom options separator (can change)
 *
 * Removes all instances of @name except the last one.
 *
 * Returns: 0 on success, 1 when not found the @name or negative number in case
 * of error.
 */
int mnt_optstr_deduplicate_option_sep(char **optstr, const char *name,
	char *opt_sep)
{
	int rc;
	char *begin = NULL, *end = NULL, *opt;

	if (!optstr || !name || !opt_sep)
		return -EINVAL;

	opt = *optstr;
	do {
		struct libmnt_optloc ol = MNT_INIT_OPTLOC;

		rc = mnt_optstr_locate_option(opt, name, &ol, opt_sep);
		if (!rc) {
			if (begin) {
				/* remove the previous instance */
				size_t shift = strlen(*optstr);

				mnt_optstr_remove_option_at(optstr, begin, end, opt_sep);

				/* now all the offsets are not valid anymore - recount */
				shift -= strlen(*optstr);
				ol.begin -= shift;
				ol.end -= shift;
			}
			begin = ol.begin;
			end = ol.end;
			opt = end && *end ? end + 1 : NULL;
		}
		if (opt == NULL)
			break;
	} while (rc == 0 && *opt);

	return rc < 0 ? rc : begin ? 0 : 1;
}

/**
 * mnt_optstr_deduplicate_option:
 * @optstr: string with a comma separated list of options
 * @name: requested option name
 *
 * Removes all instances of @name except the last one.
 *
 * Returns: 0 on success, 1 when not found the @name or negative number in case
 * of error.
 */
int mnt_optstr_deduplicate_option(char **optstr, const char *name)
{
	char opt_sep[2] = ",";
	return mnt_optstr_deduplicate_option_sep(optstr, name, opt_sep);
}

/*
 * @opt_sep: comma or custom options separator
 *
 * The result never starts or ends with a comma or contains two commas
 *    (e.g. ",aaa,bbb" or "aaa,,bbb" or "aaa,")
 */
int mnt_optstr_remove_option_at(char **optstr, char *begin, char *end,
	const char *opt_sep)
{
	size_t sz;

	if (!optstr || !begin || !end || !opt_sep)
		return -EINVAL;

	if ((begin == *optstr || *(begin - 1) == opt_sep[0]) && *end == opt_sep[0])
		end++;

	sz = strlen(end);

	memmove(begin, end, sz + 1);
	if (!*begin && (begin > *optstr) && *(begin - 1) == opt_sep[0])
		*(begin - 1) = '\0';

	return 0;
}

/*
 * insert 'substr' or '=substr' to @str on position @pos
 * @opt_sep: comma or custom options separator
 */
static int __attribute__((nonnull(1,2,3,5)))
insert_value(char **str, char *pos, const char *substr, char **next,
	const char *opt_sep)
{
	size_t subsz = strlen(substr);			/* substring size */
	size_t strsz = strlen(*str);
	size_t possz = strlen(pos);
	size_t posoff;
	char *p;
	int sep;

	/* is it necessary to prepend '=' before the substring ? */
	sep = !(pos > *str && *(pos - 1) == '=');

	/* save an offset of the place where we need to add substr */
	posoff = pos - *str;

	p = realloc(*str, strsz + sep + subsz + 1);
	if (!p)
		return -ENOMEM;

	/* zeroize the newly allocated memory -- valgrind loves us... */
	memset(p + strsz, 0, sep + subsz + 1);

	/* set pointers to the reallocated string */
	*str = p;
	pos = p + posoff;

	if (possz)
		/* create a room for the new substring */
		memmove(pos + subsz + sep, pos, possz + 1);
	if (sep)
		*pos++ = '=';

	memcpy(pos, substr, subsz);

	if (next) {
		/* set pointer to the next option */
		*next = pos + subsz;
		if (**next == opt_sep[0])
			(*next)++;
	}
	return 0;
}

/**
 * mnt_optstr_set_option_sep:
 * @optstr: string with a comma separated list of options
 * @name: requested option
 * @value: new value or NULL
 * @opt_sep: comma or custom options separator
 *
 * Set or unset the option @value.
 *
 * Returns: 0 on success, 1 when not found the @name or negative number in case
 * of error.
 */
int mnt_optstr_set_option_sep(char **optstr, const char *name,
	const char *value, char *opt_sep)
{
	struct libmnt_optloc ol = MNT_INIT_OPTLOC;
	char *nameend;
	int rc = 1;

	if (!optstr || !name || !opt_sep)
		return -EINVAL;

	if (*optstr)
		rc = mnt_optstr_locate_option(*optstr, name, &ol, opt_sep);
	if (rc < 0)
		return rc;			/* parse error */
	if (rc == 1)
		return mnt_optstr_append_option_sep(optstr, name, value, opt_sep);	/* not found */

	nameend = ol.begin + ol.namesz;

	if (value == NULL && ol.value && ol.valsz)
		/* remove unwanted "=value" */
		mnt_optstr_remove_option_at(optstr, nameend, ol.end, opt_sep);

	else if (value && ol.value == NULL)
		/* insert "=value" */
		rc = insert_value(optstr, nameend, value, NULL, opt_sep);

	else if (value && ol.value && strlen(value) == ol.valsz)
		/* simply replace =value */
		memcpy(ol.value, value, ol.valsz);

	else if (value && ol.value) {
		mnt_optstr_remove_option_at(optstr, nameend, ol.end, opt_sep);
		rc = insert_value(optstr, nameend, value, NULL, opt_sep);
	}
	return rc;
}

/**
 * mnt_optstr_set_option:
 * @optstr: string with a comma separated list of options
 * @name: requested option
 * @value: new value or NULL
 *
 * Set or unset the option @value.
 *
 * Returns: 0 on success, 1 when not found the @name or negative number in case
 * of error.
 */
int mnt_optstr_set_option(char **optstr, const char *name, const char *value)
{
	char opt_sep[2] = ",";
	return mnt_optstr_set_option_sep(optstr, name, value, opt_sep);
}

/**
 * mnt_optstr_remove_option_sep:
 * @optstr: string with a comma separated list of options
 * @name: requested option name
 * @opt_sep: comma or custom options separator
 *
 * Returns: 0 on success, 1 when not found the @name or negative number in case
 * of error.
 */
int mnt_optstr_remove_option_sep(char **optstr, const char *name, char *opt_sep)
{
	struct libmnt_optloc ol = MNT_INIT_OPTLOC;
	int rc;

	if (!optstr || !name || !opt_sep)
		return -EINVAL;

	rc = mnt_optstr_locate_option(*optstr, name, &ol, opt_sep);
	if (rc != 0)
		return rc;

	mnt_optstr_remove_option_at(optstr, ol.begin, ol.end, opt_sep);
	return 0;
}

/**
 * mnt_optstr_remove_option:
 * @optstr: string with a comma separated list of options
 * @name: requested option name
 *
 * Returns: 0 on success, 1 when not found the @name or negative number in case
 * of error.
 */
int mnt_optstr_remove_option(char **optstr, const char *name)
{
	char opt_sep[2] = ",";
	return mnt_optstr_remove_option_sep(optstr, name, opt_sep);
}

/**
 * mnt_split_optstr_sep:
 * @optstr: string with comma separated list of options
 * @user: returns newly allocated string with userspace options
 * @vfs: returns newly allocated string with VFS options
 * @fs: returns newly allocated string with FS options
 * @ignore_user: option mask for options that should be ignored
 * @ignore_vfs: option mask for options that should be ignored
 * @opt_sep: comma or custom options separator (can change)
 *
 * For example:
 *
 *	mnt_split_optstr_sep(optstr, &u, NULL, NULL, MNT_NOMTAB, 0, fs->opt_sep);
 *
 * returns all userspace options, the options that do not belong to
 * mtab are ignored.
 *
 * Note that FS options are all options that are undefined in MNT_USERSPACE_MAP
 * or MNT_LINUX_MAP.
 *
 * Returns: 0 on success, or a negative number in case of error.
 */
int mnt_split_optstr_sep(const char *optstr, char **user, char **vfs,
		     char **fs, int ignore_user, int ignore_vfs, char *opt_sep)
{
	int rc = 0;
	char *name, *val, *str = (char *) optstr;
	size_t namesz, valsz, chunsz;
	struct libmnt_optmap const *maps[2];
	struct ul_buffer xvfs = UL_INIT_BUFFER,
			 xfs = UL_INIT_BUFFER,
			 xuser = UL_INIT_BUFFER;

	if (!optstr || !opt_sep)
		return -EINVAL;

	maps[0] = mnt_get_builtin_optmap(MNT_LINUX_MAP);
	maps[1] = mnt_get_builtin_optmap(MNT_USERSPACE_MAP);

	chunsz = strlen(optstr) / 2;

	while (!mnt_optstr_next_option_sep(&str, &name, &namesz, &val, &valsz,
		opt_sep)) {
		struct ul_buffer *buf = NULL;
		const struct libmnt_optmap *ent = NULL;
		const struct libmnt_optmap *m =
			 mnt_optmap_get_entry(maps, 2, name, namesz, &ent);

		if (ent && !ent->id)
			continue;	/* ignore undefined options (comments) */

		/* ignore name=<value> if options map expects <name> only */
		if (valsz && mnt_optmap_entry_novalue(ent))
			m = NULL;

		if (ent && m && m == maps[0] && vfs) {
			if (ignore_vfs && (ent->mask & ignore_vfs))
				continue;
			if (vfs)
				buf = &xvfs;
		} else if (ent && m && m == maps[1] && user) {
			if (ignore_user && (ent->mask & ignore_user))
				continue;
			if (user)
				buf = &xuser;
		} else if (!m && fs) {
			if (fs)
				buf = &xfs;
		}

		if (buf) {
			if (ul_buffer_is_empty(buf))
				ul_buffer_set_chunksize(buf, chunsz);
			rc = __buffer_append_option(buf, name, namesz, val, valsz, opt_sep);
		}
		if (rc)
			break;
	}

	if (vfs)
		*vfs  = rc ? NULL : ul_buffer_get_data(&xvfs, NULL, NULL);
	if (fs)
		*fs   = rc ? NULL : ul_buffer_get_data(&xfs, NULL, NULL);
	if (user)
		*user = rc ? NULL : ul_buffer_get_data(&xuser, NULL, NULL);
	if (rc) {
		ul_buffer_free_data(&xvfs);
		ul_buffer_free_data(&xfs);
		ul_buffer_free_data(&xuser);
	}

	return rc;
}

/**
 * mnt_split_optstr:
 * @optstr: string with comma separated list of options
 * @user: returns newly allocated string with userspace options
 * @vfs: returns newly allocated string with VFS options
 * @fs: returns newly allocated string with FS options
 * @ignore_user: option mask for options that should be ignored
 * @ignore_vfs: option mask for options that should be ignored
 *
 * For example:
 *
 *	mnt_split_optstr(optstr, &u, NULL, NULL, MNT_NOMTAB, 0);
 *
 * returns all userspace options, the options that do not belong to
 * mtab are ignored.
 *
 * Note that FS options are all options that are undefined in MNT_USERSPACE_MAP
 * or MNT_LINUX_MAP.
 *
 * Returns: 0 on success, or a negative number in case of error.
 */
int mnt_split_optstr(const char *optstr, char **user, char **vfs,
		     char **fs, int ignore_user, int ignore_vfs)
{
	char opt_sep[2] = ",";
	return mnt_split_optstr_sep(optstr, user, vfs, fs, ignore_user, ignore_vfs,
		opt_sep);
}

/**
 * mnt_optstr_get_options_sep
 * @optstr: string with a comma separated list of options
 * @subset: returns newly allocated string with options
 * @map: options map
 * @ignore: mask of the options that should be ignored
 * @opt_sep: comma or custom options separator
 *
 * Extracts options from @optstr that belong to the @map, for example:
 *
 *	 mnt_optstr_get_options_sep(optstr, &p,
 *			mnt_get_builtin_optmap(MNT_LINUX_MAP),
 *			MNT_NOMTAB, fs->opt_sep);
 *
 * the 'p' returns all VFS options, the options that do not belong to mtab
 * are ignored.
 *
 * Returns: 0 on success, or a negative number in case of error.
 */
int mnt_optstr_get_options_sep(const char *optstr, char **subset,
			    const struct libmnt_optmap *map, int ignore, char *opt_sep)
{
	struct libmnt_optmap const *maps[1];
	struct ul_buffer buf = UL_INIT_BUFFER;
	char *name, *val, *str = (char *) optstr;
	size_t namesz, valsz;
	int rc = 0;

	if (!optstr || !subset || !opt_sep)
		return -EINVAL;

	maps[0] = map;

	ul_buffer_set_chunksize(&buf, strlen(optstr)/2);

	while (!mnt_optstr_next_option_sep(&str, &name, &namesz, &val, &valsz,
		opt_sep)) {
		const struct libmnt_optmap *ent;

		mnt_optmap_get_entry(maps, 1, name, namesz, &ent);

		if (!ent || !ent->id)
			continue;	/* ignore undefined options (comments) */

		if (ignore && (ent->mask & ignore))
			continue;

		/* ignore name=<value> if options map expects <name> only */
		if (valsz && mnt_optmap_entry_novalue(ent))
			continue;

		rc = __buffer_append_option(&buf, name, namesz, val, valsz, opt_sep);
		if (rc)
			break;
	}

	*subset  = rc ? NULL : ul_buffer_get_data(&buf, NULL, NULL);
	if (rc)
		ul_buffer_free_data(&buf);
	return rc;
}

/**
 * mnt_optstr_get_options
 * @optstr: string with a comma separated list of options
 * @subset: returns newly allocated string with options
 * @map: options map
 * @ignore: mask of the options that should be ignored
 *
 * Extracts options from @optstr that belong to the @map, for example:
 *
 *	 mnt_optstr_get_options(optstr, &p,
 *			mnt_get_builtin_optmap(MNT_LINUX_MAP),
 *			MNT_NOMTAB);
 *
 * the 'p' returns all VFS options, the options that do not belong to mtab
 * are ignored.
 *
 * Returns: 0 on success, or a negative number in case of error.
 */
int mnt_optstr_get_options(const char *optstr, char **subset,
			    const struct libmnt_optmap *map, int ignore)
{
	char opt_sep[2] = ",";
	return mnt_optstr_get_options_sep(optstr, subset, map, ignore, opt_sep);
}

/**
 * mnt_optstr_get_flags_sep:
 * @optstr: string with comma separated list of options
 * @flags: returns mount flags
 * @map: options map
 * @opt_sep: comma or custom options separator
 *
 * Returns in @flags IDs of options from @optstr as defined in the @map.
 *
 * For example:
 *
 *	"bind,exec,foo,bar"   --returns->   MS_BIND
 *
 *	"bind,noexec,foo,bar" --returns->   MS_BIND|MS_NOEXEC
 *
 * Note that @flags are not zeroized by this function! This function sets/unsets
 * bits in the @flags only.
 *
 * Returns: 0 on success or negative number in case of error
 */
int mnt_optstr_get_flags_sep(const char *optstr, unsigned long *flags,
		const struct libmnt_optmap *map, char *opt_sep)
{
	struct libmnt_optmap const *maps[2];
	char *name, *str = (char *) optstr;
	size_t namesz = 0, valsz = 0;
	int nmaps = 0;

	if (!optstr || !flags || !map || !opt_sep)
		return -EINVAL;

	maps[nmaps++] = map;

	if (map == mnt_get_builtin_optmap(MNT_LINUX_MAP))
		/*
		 * Add userspace map -- the "user" is interpreted as
		 *                      MS_NO{EXEC,SUID,DEV}.
		 */
		maps[nmaps++] = mnt_get_builtin_optmap(MNT_USERSPACE_MAP);

	while (!mnt_optstr_next_option_sep(&str, &name, &namesz, NULL, &valsz,
		opt_sep)) {
		const struct libmnt_optmap *ent;
		const struct libmnt_optmap *m;

		m = mnt_optmap_get_entry(maps, nmaps, name, namesz, &ent);
		if (!m || !ent || !ent->id)
			continue;

		/* ignore name=<value> if options map expects <name> only */
		if (valsz && mnt_optmap_entry_novalue(ent))
			continue;

		if (m == map) {				/* requested map */
			if (ent->mask & MNT_INVERT)
				*flags &= ~ent->id;
			else
				*flags |= ent->id;

		} else if (nmaps == 2 && m == maps[1] && valsz == 0) {
			/*
			 * Special case -- translate "user" (but no user=) to
			 * MS_ options
			 */
			if (ent->mask & MNT_INVERT)
				continue;
			if (ent->id & (MNT_MS_OWNER | MNT_MS_GROUP))
				*flags |= MS_OWNERSECURE;
			else if (ent->id & (MNT_MS_USER | MNT_MS_USERS))
				*flags |= MS_SECURE;
		}
	}

	return 0;
}

/**
 * mnt_optstr_get_flags:
 * @optstr: string with comma separated list of options
 * @flags: returns mount flags
 * @map: options map
 *
 * Returns in @flags IDs of options from @optstr as defined in the @map.
 *
 * For example:
 *
 *	"bind,exec,foo,bar"   --returns->   MS_BIND
 *
 *	"bind,noexec,foo,bar" --returns->   MS_BIND|MS_NOEXEC
 *
 * Note that @flags are not zeroized by this function! This function sets/unsets
 * bits in the @flags only.
 *
 * Returns: 0 on success or negative number in case of error
 */
int mnt_optstr_get_flags(const char *optstr, unsigned long *flags,
		const struct libmnt_optmap *map)
{
	char opt_sep[2] = ",";
	return mnt_optstr_get_flags_sep(optstr, flags, map, opt_sep);
}

/**
 * mnt_optstr_apply_flags_sep:
 * @optstr: string with comma separated list of options
 * @flags: returns mount flags
 * @map: options map
 * @opt_sep: comma or custom options separator
 *
 * Removes/adds options to the @optstr according to flags. For example:
 *
 *	MS_NOATIME and "foo,bar,noexec"   --returns->  "foo,bar,noatime"
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_optstr_apply_flags_sep(char **optstr, unsigned long flags,
				const struct libmnt_optmap *map, char *opt_sep)
{
	struct libmnt_optmap const *maps[1];
	char *name, *next, *val;
	size_t namesz = 0, valsz = 0, multi = 0;
	unsigned long fl;
	int rc = 0;

	if (!optstr || !map || !opt_sep)
		return -EINVAL;

	DBG(CXT, ul_debug("applying 0x%08lx flags to '%s'", flags, *optstr));

	maps[0] = map;
	next = *optstr;
	fl = flags;

	/*
	 * There is a convention that 'rw/ro' flags are always at the beginning of
	 * the string (although the 'rw' is unnecessary).
	 */
	if (map == mnt_get_builtin_optmap(MNT_LINUX_MAP)) {
		const char *o = (fl & MS_RDONLY) ? "ro" : "rw";

		if (next &&
		    (!strncmp(next, "rw", 2) || !strncmp(next, "ro", 2)) &&
		    (*(next + 2) == '\0' || *(next + 2) == opt_sep[0])) {

			/* already set, be paranoid and fix it */
			memcpy(next, o, 2);
		} else {
			rc = mnt_optstr_prepend_option_sep(optstr, o, NULL, opt_sep);
			if (rc)
				goto err;
			next = *optstr;		/* because realloc() */
		}
		fl &= ~MS_RDONLY;
		next += 2;
		if (*next == opt_sep[0])
			next++;
	}

	if (next && *next) {
		/*
		 * scan @optstr and remove options that are missing in
		 * @flags
		 */
		while(!mnt_optstr_next_option_sep(&next, &name, &namesz,
							&val, &valsz, opt_sep)) {
			const struct libmnt_optmap *ent;

			if (mnt_optmap_get_entry(maps, 1, name, namesz, &ent)) {
				/*
				 * remove unwanted option (rw/ro is already set)
				 */
				if (!ent || !ent->id)
					continue;
				/* ignore name=<value> if options map expects <name> only */
				if (valsz && mnt_optmap_entry_novalue(ent))
					continue;

				if (ent->id == MS_RDONLY ||
				    (ent->mask & MNT_INVERT) ||
				    (fl & ent->id) != (unsigned long) ent->id) {

					char *end = val ? val + valsz :
							  name + namesz;
					next = name;
					rc = mnt_optstr_remove_option_at(
							optstr, name, end, opt_sep);
					if (rc)
						goto err;
				}
				if (!(ent->mask & MNT_INVERT)) {
					/* allow options with prefix (X-mount.foo,X-mount.bar) more than once */
					if (ent->mask & MNT_PREFIX)
						multi |= ent->id;
					else
						fl &= ~ent->id;
					if (ent->id & MS_REC)
						fl |= MS_REC;
				}
			}
		}
	}

	/* remove from flags options which are allowed more than once */
	fl &= ~multi;

	/* add missing options (but ignore fl if contains MS_REC only) */
	if (fl && fl != MS_REC) {

		const struct libmnt_optmap *ent;
		struct ul_buffer buf = UL_INIT_BUFFER;
		size_t sz;
		char *p;

		ul_buffer_refer_string(&buf, *optstr);

		for (ent = map; ent && ent->name; ent++) {
			if ((ent->mask & MNT_INVERT)
			    || ent->id == 0
			    || (fl & ent->id) != (unsigned long) ent->id)
				continue;

			/* don't add options which require values (e.g. offset=%d) */
			p = strchr(ent->name, '=');
			if (p) {
				if (p > ent->name && *(p - 1) == '[')
					p--;			/* name[=] */
				else
					continue;		/* name= */
				sz = p - ent->name;
			} else
				sz = strlen(ent->name);

			rc = __buffer_append_option(&buf, ent->name, sz, NULL, 0, opt_sep);
			if (rc)
				goto err;
		}

		*optstr = ul_buffer_get_data(&buf, NULL, NULL);
	}

	DBG(CXT, ul_debug("new optstr '%s'", *optstr));
	return rc;
err:
	DBG(CXT, ul_debug("failed to apply flags [rc=%d]", rc));
	return rc;
}

/**
 * mnt_optstr_apply_flags:
 * @optstr: string with comma separated list of options
 * @flags: returns mount flags
 * @map: options map
 *
 * Removes/adds options to the @optstr according to flags. For example:
 *
 *	MS_NOATIME and "foo,bar,noexec"   --returns->  "foo,bar,noatime"
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_optstr_apply_flags(char **optstr, unsigned long flags,
				const struct libmnt_optmap *map)
{
	char opt_sep[2] = ",";
	return mnt_optstr_apply_flags_sep(optstr, flags, map, opt_sep);
}

/*
 * @optstr: string with comma separated list of options
 * @value: pointer to the begin of the context value
 * @valsz: size of the value
 * @next: returns pointer to the next option (optional argument)
 * @opt_sep: comma or custom options separator
 *
 * Translates SELinux context from human to raw format. The function does not
 * modify @optstr and returns zero if libmount is compiled without SELinux
 * support.
 *
 * Returns: 0 on success, a negative number in case of error.
 */
#ifndef HAVE_LIBSELINUX
int mnt_optstr_fix_secontext(char **optstr __attribute__ ((__unused__)),
			     char *value   __attribute__ ((__unused__)),
			     size_t valsz  __attribute__ ((__unused__)),
			     char **next   __attribute__ ((__unused__)),
			     const char *opt_sep __attribute__ ((__unused__)))
{
	return 0;
}
#else
int mnt_optstr_fix_secontext(char **optstr,
			     char *value,
			     size_t valsz,
			     char **next,
			     const char *opt_sep)
{
	int rc = 0;
	char *p, *val, *begin, *end, *raw = NULL;
	size_t sz;

	if (!optstr || !*optstr || !value || !valsz || !opt_sep)
		return -EINVAL;

	DBG(CXT, ul_debug("fixing SELinux context"));

	begin = value;
	end = value + valsz;

	/* the selinux contexts are quoted */
	if (*value == '"') {
		if (valsz <= 2 || *(value + valsz - 1) != '"')
			return -EINVAL;		/* improperly quoted option string */
		value++;
		valsz -= 2;
	}

	p = strndup(value, valsz);
	if (!p)
		return -ENOMEM;


	/* translate the context */
	rc = selinux_trans_to_raw_context(p, &raw);

	DBG(CXT, ul_debug("SELinux context '%s' translated to '%s'",
			p, rc == -1 ? "FAILED" : (char *) raw));

	free(p);
	if (rc == -1 ||	!raw)
		return -EINVAL;


	/* create a quoted string from the raw context */
	sz = strlen((char *) raw);
	if (!sz) {
		freecon(raw);
		return -EINVAL;
	}

	p = val = malloc(valsz + 3);
	if (!val) {
		freecon(raw);
		return -ENOMEM;
	}

	*p++ = '"';
	memcpy(p, raw, sz);
	p += sz;
	*p++ = '"';
	*p = '\0';

	freecon(raw);

	/* set new context */
	mnt_optstr_remove_option_at(optstr, begin, end, opt_sep);
	rc = insert_value(optstr, begin, val, next, opt_sep);
	free(val);

	return rc;
}
#endif

static int set_uint_value(char **optstr, unsigned int num,
			char *begin, char *end, char **next, const char *opt_sep)
{
	char buf[40];
	snprintf(buf, sizeof(buf), "%u", num);

	mnt_optstr_remove_option_at(optstr, begin, end, opt_sep);
	return insert_value(optstr, begin, buf, next, opt_sep);
}

/*
 * @optstr: string with a comma separated list of options
 * @value: pointer to the beginning of the uid value
 * @valsz: size of the value
 * @next: returns pointer to the next option (optional argument)
 * @opt_sep: comma or custom options separator
 *
 * Translates "username" or "useruid" to the real UID.
 *
 * For example:
 *	if (!mnt_optstr_get_option(optstr, "uid", &val, &valsz))
 *		mnt_optstr_fix_uid(&optstr, val, valsz, NULL, fs->opt_sep);
 *
 * Returns: 0 on success, a negative number in case of error.
 */
int mnt_optstr_fix_uid(char **optstr, char *value, size_t valsz, char **next,
	const char *opt_sep)
{
	char *end;

	if (!optstr || !*optstr || !value || !valsz || !opt_sep)
		return -EINVAL;

	DBG(CXT, ul_debug("fixing uid"));

	end = value + valsz;

	if (valsz == 7 && !strncmp(value, "useruid", 7) &&
	    (*(value + 7) == opt_sep[0] || !*(value + 7)))
		return set_uint_value(optstr, getuid(), value, end, next, opt_sep);

	if (!isdigit(*value)) {
		uid_t id;
		int rc;
		char *p = strndup(value, valsz);
		if (!p)
			return -ENOMEM;
		rc = mnt_get_uid(p, &id);
		free(p);

		if (!rc)
			return set_uint_value(optstr, id, value, end, next, opt_sep);
	}

	if (next) {
		/* no change, let's keep the original value */
		*next = value + valsz;
		if (**next == opt_sep[0])
			(*next)++;
	}

	return 0;
}

/*
 * @optstr: string with a comma separated list of options
 * @value: pointer to the beginning of the uid value
 * @valsz: size of the value
 * @next: returns pointer to the next option (optional argument)
 * @opt_sep: comma or custom options separator

 * Translates "groupname" or "usergid" to the real GID.
 *
 * Returns: 0 on success, a negative number in case of error.
 */
int mnt_optstr_fix_gid(char **optstr, char *value, size_t valsz, char **next,
	const char *opt_sep)
{
	char *end;

	if (!optstr || !*optstr || !value || !valsz || !opt_sep)
		return -EINVAL;

	DBG(CXT, ul_debug("fixing gid"));

	end = value + valsz;

	if (valsz == 7 && !strncmp(value, "usergid", 7) &&
	    (*(value + 7) == opt_sep[0] || !*(value + 7)))
		return set_uint_value(optstr, getgid(), value, end, next, opt_sep);

	if (!isdigit(*value)) {
		int rc;
		gid_t id;
		char *p = strndup(value, valsz);
		if (!p)
			return -ENOMEM;
		rc = mnt_get_gid(p, &id);
		free(p);

		if (!rc)
			return set_uint_value(optstr, id, value, end, next, opt_sep);

	}

	if (next) {
		/* nothing */
		*next = value + valsz;
		if (**next == opt_sep[0])
			(*next)++;
	}
	return 0;
}

/*
 * Converts "user" to "user=<username>".
 * @opt_sep: comma or custom options separator.
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_optstr_fix_user(char **optstr, char *opt_sep)
{
	char *username;
	struct libmnt_optloc ol = MNT_INIT_OPTLOC;
	int rc = 0;

	DBG(CXT, ul_debug("fixing user"));

	assert(opt_sep);
	rc = mnt_optstr_locate_option(*optstr, "user", &ol, opt_sep);
	if (rc)
		return rc == 1 ? 0 : rc;	/* 1: user= not found */

	username = mnt_get_username(getuid());
	if (!username)
		return -ENOMEM;

	if (!ol.valsz || (ol.value && strncmp(ol.value, username, ol.valsz) != 0)) {
		if (ol.valsz)
			/* remove old value */
			mnt_optstr_remove_option_at(optstr, ol.value, ol.end, opt_sep);

		rc = insert_value(optstr, ol.value ? ol.value : ol.end,
				  username, NULL, opt_sep);
	}

	free(username);
	return rc;
}

/*
 * Converts value from @optstr addressed by @name to uid.
 *
 * Returns: 0 on success, <0 on error
 */
int mnt_optstr_get_uid(const char *optstr, const char *name, uid_t *uid)
{
	char *value = NULL;
	size_t valsz = 0;
	int rc;

	assert(optstr);
	assert(name);
	assert(uid);

	rc = mnt_optstr_get_option(optstr, name, &value, &valsz);
	if (rc != 0)
		goto fail;

	rc = mnt_parse_uid(value, valsz, uid);
	if (rc != 0) {
		rc = -errno;
		goto fail;
	}

	return 0;
fail:
	DBG(UTILS, ul_debug("failed to convert '%s'= to number [rc=%d]", name, rc));
	return rc;
}

/**
 * mnt_match_options_sep:
 * @optstr: options string
 * @pattern: comma delimited list of options
 * @opt_sep: comma or custom options separator
 *
 * The "no" could be used for individual items in the @options list. The "no"
 * prefix does not have a global meaning.
 *
 * Unlike fs type matching, nonetdev,user and nonetdev,nouser have
 * DIFFERENT meanings; each option is matched explicitly as specified.
 *
 * The "no" prefix interpretation could be disabled by the "+" prefix, for example
 * "+noauto" matches if @optstr literally contains the "noauto" string.
 *
 * "xxx,yyy,zzz" : "nozzz"	-> False
 *
 * "xxx,yyy,zzz" : "xxx,noeee"	-> True
 *
 * "bar,zzz"     : "nofoo"      -> True		(does not contain "foo")
 *
 * "nofoo,bar"   : "nofoo"      -> True		(does not contain "foo")
 *
 * "nofoo,bar"   : "+nofoo"     -> True		(contains "nofoo")
 *
 * "bar,zzz"     : "+nofoo"     -> False	(does not contain "nofoo")
 *
 *
 * Returns: 1 if pattern is matching, else 0. This function also returns 0
 *          if @pattern is NULL and @optstr is non-NULL.
 */
int mnt_match_options_sep(const char *optstr, const char *pattern, char *opt_sep)
{
	char *name, *pat = (char *) pattern;
	char *buf, *patval;
	size_t namesz = 0, patvalsz = 0;
	int match = 1;

	assert(opt_sep);

	if (!pattern && !optstr)
		return 1;
	if (!pattern)
		return 0;

	buf = malloc(strlen(pattern) + 1);
	if (!buf)
		return 0;

	/* walk on pattern string
	 */
	while (match && !mnt_optstr_next_option_sep(&pat, &name, &namesz,
						&patval, &patvalsz, opt_sep)) {
		char *val;
		size_t sz;
		int no = 0, rc;

		if (*name == '+')
			name++, namesz--;
		else if ((no = (startswith(name, "no") != NULL)))
			name += 2, namesz -= 2;

		xstrncpy(buf, name, namesz + 1);

		rc = mnt_optstr_get_option_sep(optstr, buf, &val, &sz, opt_sep);

		/* check also value (if the pattern is "foo=value") */
		if (rc == 0 && patvalsz > 0 &&
		    (patvalsz != sz || strncmp(patval, val, sz) != 0))
			rc = 1;

		switch (rc) {
		case 0:		/* found */
			match = no == 0 ? 1 : 0;
			break;
		case 1:		/* not found */
			match = no == 1 ? 1 : 0;
			break;
		default:	/* parse error */
			match = 0;
			break;
		}

	}

	free(buf);
	return match;
}

/**
 * mnt_match_options:
 * @optstr: options string
 * @pattern: comma delimited list of options
 *
 * The "no" could be used for individual items in the @options list. The "no"
 * prefix does not have a global meaning.
 *
 * Unlike fs type matching, nonetdev,user and nonetdev,nouser have
 * DIFFERENT meanings; each option is matched explicitly as specified.
 *
 * The "no" prefix interpretation could be disabled by the "+" prefix, for example
 * "+noauto" matches if @optstr literally contains the "noauto" string.
 *
 * "xxx,yyy,zzz" : "nozzz"	-> False
 *
 * "xxx,yyy,zzz" : "xxx,noeee"	-> True
 *
 * "bar,zzz"     : "nofoo"      -> True		(does not contain "foo")
 *
 * "nofoo,bar"   : "nofoo"      -> True		(does not contain "foo")
 *
 * "nofoo,bar"   : "+nofoo"     -> True		(contains "nofoo")
 *
 * "bar,zzz"     : "+nofoo"     -> False	(does not contain "nofoo")
 *
 *
 * Returns: 1 if pattern is matching, else 0. This function also returns 0
 *          if @pattern is NULL and @optstr is non-NULL.
 */
int mnt_match_options(const char *optstr, const char *pattern)
{
	char opt_sep[2] = ",";
	return mnt_match_options_sep(optstr, pattern, opt_sep);
}

#ifdef TEST_PROGRAM
#include "xalloc.h"

static int test_append(struct libmnt_test *ts, int argc, char *argv[])
{
	const char *value = NULL, *name;
	char *optstr;
	int rc;

	if (argc < 3)
		return -EINVAL;
	optstr = xstrdup(argv[1]);
	name = argv[2];

	if (argc == 4)
		value = argv[3];

	rc = mnt_optstr_append_option(&optstr, name, value);
	if (!rc)
		printf("result: >%s<\n", optstr);
	free(optstr);
	return rc;
}

static int test_prepend(struct libmnt_test *ts, int argc, char *argv[])
{
	const char *value = NULL, *name;
	char *optstr;
	int rc;

	if (argc < 3)
		return -EINVAL;
	optstr = xstrdup(argv[1]);
	name = argv[2];

	if (argc == 4)
		value = argv[3];

	rc = mnt_optstr_prepend_option(&optstr, name, value);
	if (!rc)
		printf("result: >%s<\n", optstr);
	free(optstr);
	return rc;
}

static int test_split(struct libmnt_test *ts, int argc, char *argv[])
{
	char *optstr, *user = NULL, *fs = NULL, *vfs = NULL;
	int rc;

	if (argc < 2)
		return -EINVAL;

	optstr = xstrdup(argv[1]);

	rc = mnt_split_optstr(optstr, &user, &vfs, &fs, 0, 0);
	if (!rc) {
		printf("user : %s\n", user);
		printf("vfs  : %s\n", vfs);
		printf("fs   : %s\n", fs);
	}

	free(user);
	free(vfs);
	free(fs);
	free(optstr);
	return rc;
}

static int test_flags(struct libmnt_test *ts, int argc, char *argv[])
{
	char *optstr;
	int rc;
	unsigned long fl = 0;

	if (argc < 2)
		return -EINVAL;

	optstr = xstrdup(argv[1]);

	rc = mnt_optstr_get_flags(optstr, &fl, mnt_get_builtin_optmap(MNT_LINUX_MAP));
	if (rc)
		return rc;
	printf("mountflags:           0x%08lx\n", fl);

	fl = 0;
	rc = mnt_optstr_get_flags(optstr, &fl, mnt_get_builtin_optmap(MNT_USERSPACE_MAP));
	if (rc)
		return rc;
	printf("userspace-mountflags: 0x%08lx\n", fl);

	free(optstr);
	return rc;
}

static int test_apply(struct libmnt_test *ts, int argc, char *argv[])
{
	char *optstr;
	int rc, map;
	unsigned long flags;

	if (argc < 4)
		return -EINVAL;

	if (!strcmp(argv[1], "--user"))
		map = MNT_USERSPACE_MAP;
	else if (!strcmp(argv[1], "--linux"))
		map = MNT_LINUX_MAP;
	else {
		fprintf(stderr, "unknown option '%s'\n", argv[1]);
		return -EINVAL;
	}

	optstr = xstrdup(argv[2]);
	flags = strtoul(argv[3], NULL, 16);

	printf("flags:  0x%08lx\n", flags);

	rc = mnt_optstr_apply_flags(&optstr, flags, mnt_get_builtin_optmap(map));
	printf("optstr: %s\n", optstr);

	free(optstr);
	return rc;
}

static int test_set(struct libmnt_test *ts, int argc, char *argv[])
{
	const char *value = NULL, *name;
	char *optstr;
	int rc;

	if (argc < 3)
		return -EINVAL;
	optstr = xstrdup(argv[1]);
	name = argv[2];

	if (argc == 4)
		value = argv[3];

	rc = mnt_optstr_set_option(&optstr, name, value);
	if (!rc)
		printf("result: >%s<\n", optstr);
	free(optstr);
	return rc;
}

static int test_get(struct libmnt_test *ts, int argc, char *argv[])
{
	char *optstr;
	const char *name;
	char *val = NULL;
	size_t sz = 0;
	int rc;

	if (argc < 2)
		return -EINVAL;
	optstr = argv[1];
	name = argv[2];

	rc = mnt_optstr_get_option(optstr, name, &val, &sz);
	if (rc == 0) {
		printf("found; name: %s", name);
		if (sz) {
			printf(", argument: size=%zd data=", sz);
			if (fwrite(val, 1, sz, stdout) != sz)
				return -1;
		}
		printf("\n");
	} else if (rc == 1)
		printf("%s: not found\n", name);
	else
		printf("parse error: %s\n", optstr);
	return rc;
}

static int test_remove(struct libmnt_test *ts, int argc, char *argv[])
{
	const char *name;
	char *optstr;
	int rc;

	if (argc < 3)
		return -EINVAL;
	optstr = xstrdup(argv[1]);
	name = argv[2];

	rc = mnt_optstr_remove_option(&optstr, name);
	if (!rc)
		printf("result: >%s<\n", optstr);
	free(optstr);
	return rc;
}

static int test_dedup(struct libmnt_test *ts, int argc, char *argv[])
{
	const char *name;
	char *optstr;
	int rc;

	if (argc < 3)
		return -EINVAL;
	optstr = xstrdup(argv[1]);
	name = argv[2];

	rc = mnt_optstr_deduplicate_option(&optstr, name);
	if (!rc)
		printf("result: >%s<\n", optstr);
	free(optstr);
	return rc;
}

static int test_fix(struct libmnt_test *ts, int argc, char *argv[])
{
	char *optstr;
	int rc = 0;
	char *name, *val, *next;
	size_t valsz, namesz;
	char opt_sep[2] = ",";

	if (argc < 2)
		return -EINVAL;

	next = optstr = xstrdup(argv[1]);

	printf("optstr: %s\n", optstr);

	while (!mnt_optstr_next_option(&next, &name, &namesz, &val, &valsz)) {

		if (!strncmp(name, "uid", 3))
			rc = mnt_optstr_fix_uid(&optstr, val, valsz, &next, opt_sep);
		else if (!strncmp(name, "gid", 3))
			rc = mnt_optstr_fix_gid(&optstr, val, valsz, &next, opt_sep);
		else if (!strncmp(name, "context", 7))
			rc = mnt_optstr_fix_secontext(&optstr, val, valsz, &next, opt_sep);
		if (rc)
			break;
	}
	if (rc)
		rc = mnt_optstr_fix_user(&optstr, opt_sep);

	printf("fixed:  %s\n", optstr);

	free(optstr);
	return rc;

}

int main(int argc, char *argv[])
{
	struct libmnt_test tss[] = {
		{ "--append", test_append, "<optstr> <name> [<value>]  append value to optstr" },
		{ "--prepend",test_prepend,"<optstr> <name> [<value>]  prepend value to optstr" },
		{ "--set",    test_set,    "<optstr> <name> [<value>]  (un)set value" },
		{ "--get",    test_get,    "<optstr> <name>            search name in optstr" },
		{ "--remove", test_remove, "<optstr> <name>            remove name in optstr" },
		{ "--dedup",  test_dedup,  "<optstr> <name>            deduplicate name in optstr" },
		{ "--split",  test_split,  "<optstr>                   split into FS, VFS and userspace" },
		{ "--flags",  test_flags,  "<optstr>                   convert options to MS_* flags" },
		{ "--apply",  test_apply,  "--{linux,user} <optstr> <mask>    apply mask to optstr" },
		{ "--fix",    test_fix,    "<optstr>                   fix uid=, gid=, user, and context=" },

		{ NULL }
	};
	return  mnt_run_test(tss, argc, argv);
}
#endif /* TEST_PROGRAM */
