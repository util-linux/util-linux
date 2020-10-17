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

#define mnt_init_optloc(_ol)	(memset((_ol), 0, sizeof(struct libmnt_optloc)))

#define mnt_optmap_entry_novalue(e) \
		(e && (e)->name && !strchr((e)->name, '=') && !((e)->mask & MNT_PREFIX))

/*
 * Parses the first option from @optstr. The @optstr pointer is set to the beginning
 * of the next option.
 *
 * Returns -EINVAL on parse error, 1 at the end of optstr and 0 on success.
 */
static int mnt_optstr_parse_next(char **optstr,	 char **name, size_t *namesz,
					char **value, size_t *valsz)
{
	int open_quote = 0;
	char *start = NULL, *stop = NULL, *p, *sep = NULL;
	char *optstr0;

	assert(optstr);
	assert(*optstr);

	optstr0 = *optstr;

	if (name)
		*name = NULL;
	if (namesz)
		*namesz = 0;
	if (value)
		*value = NULL;
	if (valsz)
		*valsz = 0;

	/* trim leading commas as to not invalidate option
	 * strings with multiple consecutive commas */
	while (optstr0 && *optstr0 == ',')
		optstr0++;

	for (p = optstr0; p && *p; p++) {
		if (!start)
			start = p;		/* beginning of the option item */
		if (*p == '"')
			open_quote ^= 1;	/* reverse the status */
		if (open_quote)
			continue;		/* still in quoted block */
		if (!sep && p > start && *p == '=')
			sep = p;		/* name and value separator */
		if (*p == ',')
			stop = p;		/* terminate the option item */
		else if (*(p + 1) == '\0')
			stop = p + 1;		/* end of optstr */
		if (!start || !stop)
			continue;
		if (stop <= start)
			goto error;

		if (name)
			*name = start;
		if (namesz)
			*namesz = sep ? sep - start : stop - start;
		*optstr = *stop ? stop + 1 : stop;

		if (sep) {
			if (value)
				*value = sep + 1;
			if (valsz)
				*valsz = stop - sep - 1;
		}
		return 0;
	}

	return 1;				/* end of optstr */

error:
	DBG(OPTIONS, ul_debug("parse error: \"%s\"", optstr0));
	return -EINVAL;
}

/*
 * Locates the first option that matches @name. The @end is set to the
 * char behind the option (it means ',' or \0).
 *
 * Returns negative number on parse error, 1 when not found and 0 on success.
 */
static int mnt_optstr_locate_option(char *optstr, const char *name,
					struct libmnt_optloc *ol)
{
	char *n;
	size_t namesz, nsz;
	int rc;

	if (!optstr)
		return 1;

	assert(name);

	namesz = strlen(name);

	do {
		rc = mnt_optstr_parse_next(&optstr, &n, &nsz,
					&ol->value, &ol->valsz);
		if (rc)
			break;

		if (namesz == nsz && strncmp(n, name, nsz) == 0) {
			ol->begin = n;
			ol->end = *(optstr - 1) == ',' ? optstr - 1 : optstr;
			ol->namesz = nsz;
			return 0;
		}
	} while(1);

	return rc;
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
	if (!optstr || !*optstr)
		return -EINVAL;
	return mnt_optstr_parse_next(optstr, name, namesz, value, valuesz);
}

static int __mnt_optstr_append_option(char **optstr,
			const char *name, size_t nsz,
			const char *value, size_t vsz)
{
	char *p;
	size_t sz, osz;

	assert(name);
	assert(*name);
	assert(nsz);
	assert(optstr);

	osz = *optstr ? strlen(*optstr) : 0;

	sz = osz + nsz + 1;		/* 1: '\0' */
	if (osz)
		sz++;			/* ',' options separator */
	if (value)
		sz += vsz + 1;		/* 1: '=' */

	p = realloc(*optstr, sz);
	if (!p)
		return -ENOMEM;
	*optstr = p;

	if (osz) {
		p += osz;
		*p++ = ',';
	}

	memcpy(p, name, nsz);
	p += nsz;

	if (value) {
		*p++ = '=';
		memcpy(p, value, vsz);
		p += vsz;
	}
	*p = '\0';

	return 0;
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
	size_t vsz, nsz;

	if (!optstr)
		return -EINVAL;
	if (!name || !*name)
		return 0;

	nsz = strlen(name);
	vsz = value ? strlen(value) : 0;

	return __mnt_optstr_append_option(optstr, name, nsz, value, vsz);
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
	int rc = 0;
	char *tmp;

	if (!optstr)
		return -EINVAL;
	if (!name || !*name)
		return 0;

	tmp = *optstr;
	*optstr = NULL;

	rc = mnt_optstr_append_option(optstr, name, value);
	if (!rc && tmp && *tmp)
		rc = mnt_optstr_append_option(optstr, tmp, NULL);
	if (!rc) {
		free(tmp);
		return 0;
	}

	free(*optstr);
	*optstr = tmp;

	DBG(OPTIONS, ul_debug("failed to prepend '%s[=%s]' to '%s'",
				name, value, *optstr));
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
	struct libmnt_optloc ol;
	int rc;

	if (!optstr || !name)
		return -EINVAL;

	mnt_init_optloc(&ol);

	rc = mnt_optstr_locate_option((char *) optstr, name, &ol);
	if (!rc) {
		if (value)
			*value = ol.value;
		if (valsz)
			*valsz = ol.valsz;
	}
	return rc;
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
	int rc;
	char *begin = NULL, *end = NULL, *opt;

	if (!optstr || !name)
		return -EINVAL;

	opt = *optstr;
	do {
		struct libmnt_optloc ol;

		mnt_init_optloc(&ol);

		rc = mnt_optstr_locate_option(opt, name, &ol);
		if (!rc) {
			if (begin) {
				/* remove the previous instance */
				size_t shift = strlen(*optstr);

				mnt_optstr_remove_option_at(optstr, begin, end);

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

/*
 * The result never starts or ends with a comma or contains two commas
 *    (e.g. ",aaa,bbb" or "aaa,,bbb" or "aaa,")
 */
int mnt_optstr_remove_option_at(char **optstr, char *begin, char *end)
{
	size_t sz;

	if (!optstr || !begin || !end)
		return -EINVAL;

	if ((begin == *optstr || *(begin - 1) == ',') && *end == ',')
		end++;

	sz = strlen(end);

	memmove(begin, end, sz + 1);
	if (!*begin && (begin > *optstr) && *(begin - 1) == ',')
		*(begin - 1) = '\0';

	return 0;
}

/* insert 'substr' or '=substr' to @str on position @pos */
static int __attribute__((nonnull(1,2,3)))
insert_value(char **str, char *pos, const char *substr, char **next)
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
		if (**next == ',')
			(*next)++;
	}
	return 0;
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
	struct libmnt_optloc ol;
	char *nameend;
	int rc = 1;

	if (!optstr || !name)
		return -EINVAL;

	mnt_init_optloc(&ol);

	if (*optstr)
		rc = mnt_optstr_locate_option(*optstr, name, &ol);
	if (rc < 0)
		return rc;			/* parse error */
	if (rc == 1)
		return mnt_optstr_append_option(optstr, name, value);	/* not found */

	nameend = ol.begin + ol.namesz;

	if (value == NULL && ol.value && ol.valsz)
		/* remove unwanted "=value" */
		mnt_optstr_remove_option_at(optstr, nameend, ol.end);

	else if (value && ol.value == NULL)
		/* insert "=value" */
		rc = insert_value(optstr, nameend, value, NULL);

	else if (value && ol.value && strlen(value) == ol.valsz)
		/* simply replace =value */
		memcpy(ol.value, value, ol.valsz);

	else if (value && ol.value) {
		mnt_optstr_remove_option_at(optstr, nameend, ol.end);
		rc = insert_value(optstr, nameend, value, NULL);
	}
	return rc;
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
	struct libmnt_optloc ol;
	int rc;

	if (!optstr || !name)
		return -EINVAL;

	mnt_init_optloc(&ol);

	rc = mnt_optstr_locate_option(*optstr, name, &ol);
	if (rc != 0)
		return rc;

	mnt_optstr_remove_option_at(optstr, ol.begin, ol.end);
	return 0;
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
	char *name, *val, *str = (char *) optstr;
	size_t namesz, valsz;
	struct libmnt_optmap const *maps[2];

	if (!optstr)
		return -EINVAL;

	maps[0] = mnt_get_builtin_optmap(MNT_LINUX_MAP);
	maps[1] = mnt_get_builtin_optmap(MNT_USERSPACE_MAP);

	if (vfs)
		*vfs = NULL;
	if (fs)
		*fs = NULL;
	if (user)
		*user = NULL;

	while (!mnt_optstr_next_option(&str, &name, &namesz, &val, &valsz)) {
		int rc = 0;
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
			rc = __mnt_optstr_append_option(vfs, name, namesz,
								val, valsz);
		} else if (ent && m && m == maps[1] && user) {
			if (ignore_user && (ent->mask & ignore_user))
				continue;
			rc = __mnt_optstr_append_option(user, name, namesz,
								val, valsz);
		} else if (!m && fs)
			rc = __mnt_optstr_append_option(fs, name, namesz,
								val, valsz);
		if (rc) {
			if (vfs) {
				free(*vfs);
				*vfs = NULL;
			}
			if (fs) {
				free(*fs);
				*fs = NULL;
			}
			if (user) {
				free(*user);
				*user = NULL;
			}
			return rc;
		}
	}

	return 0;
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
	struct libmnt_optmap const *maps[1];
	char *name, *val, *str = (char *) optstr;
	size_t namesz, valsz;

	if (!optstr || !subset)
		return -EINVAL;

	maps[0] = map;
	*subset = NULL;

	while(!mnt_optstr_next_option(&str, &name, &namesz, &val, &valsz)) {
		int rc = 0;
		const struct libmnt_optmap *ent;

		mnt_optmap_get_entry(maps, 1, name, namesz, &ent);

		if (!ent || !ent->id)
			continue;	/* ignore undefined options (comments) */

		if (ignore && (ent->mask & ignore))
			continue;

		/* ignore name=<value> if options map expects <name> only */
		if (valsz && mnt_optmap_entry_novalue(ent))
			continue;

		rc = __mnt_optstr_append_option(subset, name, namesz, val, valsz);
		if (rc) {
			free(*subset);
			return rc;
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
	struct libmnt_optmap const *maps[2];
	char *name, *str = (char *) optstr;
	size_t namesz = 0, valsz = 0;
	int nmaps = 0;

	if (!optstr || !flags || !map)
		return -EINVAL;

	maps[nmaps++] = map;

	if (map == mnt_get_builtin_optmap(MNT_LINUX_MAP))
		/*
		 * Add userspace map -- the "user" is interpreted as
		 *                      MS_NO{EXEC,SUID,DEV}.
		 */
		maps[nmaps++] = mnt_get_builtin_optmap(MNT_USERSPACE_MAP);

	while(!mnt_optstr_next_option(&str, &name, &namesz, NULL, &valsz)) {
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
	struct libmnt_optmap const *maps[1];
	char *name, *next, *val;
	size_t namesz = 0, valsz = 0;
	unsigned long fl;
	int rc = 0;

	if (!optstr || !map)
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
		    (*(next + 2) == '\0' || *(next + 2) == ',')) {

			/* already set, be paranoid and fix it */
			memcpy(next, o, 2);
		} else {
			rc = mnt_optstr_prepend_option(optstr, o, NULL);
			if (rc)
				goto err;
			next = *optstr;		/* because realloc() */
		}
		fl &= ~MS_RDONLY;
		next += 2;
		if (*next == ',')
			next++;
	}

	if (next && *next) {
		/*
		 * scan @optstr and remove options that are missing in
		 * @flags
		 */
		while(!mnt_optstr_next_option(&next, &name, &namesz,
							&val, &valsz)) {
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
							optstr, name, end);
					if (rc)
						goto err;
				}
				if (!(ent->mask & MNT_INVERT)) {
					fl &= ~ent->id;
					if (ent->id & MS_REC)
						fl |= MS_REC;
				}
			}
		}
	}

	/* add missing options (but ignore fl if contains MS_REC only) */
	if (fl && fl != MS_REC) {
		const struct libmnt_optmap *ent;
		char *p;

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

				p = strndup(ent->name, p - ent->name);
				if (!p) {
					rc = -ENOMEM;
					goto err;
				}
				mnt_optstr_append_option(optstr, p, NULL);
				free(p);
			} else
				mnt_optstr_append_option(optstr, ent->name, NULL);
		}
	}

	DBG(CXT, ul_debug("new optstr '%s'", *optstr));
	return rc;
err:
	DBG(CXT, ul_debug("failed to apply flags [rc=%d]", rc));
	return rc;
}

/*
 * @optstr: string with comma separated list of options
 * @value: pointer to the begin of the context value
 * @valsz: size of the value
 * @next: returns pointer to the next option (optional argument)
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
			     char **next   __attribute__ ((__unused__)))
{
	return 0;
}
#else
int mnt_optstr_fix_secontext(char **optstr,
			     char *value,
			     size_t valsz,
			     char **next)
{
	int rc = 0;

	security_context_t raw = NULL;
	char *p, *val, *begin, *end;
	size_t sz;

	if (!optstr || !*optstr || !value || !valsz)
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
	rc = selinux_trans_to_raw_context((security_context_t) p, &raw);

	DBG(CXT, ul_debug("SELinux context '%s' translated to '%s'",
			p, rc == -1 ? "FAILED" : (char *) raw));

	free(p);
	if (rc == -1 ||	!raw)
		return -EINVAL;


	/* create a quoted string from the raw context */
	sz = strlen((char *) raw);
	if (!sz)
		return -EINVAL;

	p = val = malloc(valsz + 3);
	if (!val)
		return -ENOMEM;

	*p++ = '"';
	memcpy(p, raw, sz);
	p += sz;
	*p++ = '"';
	*p = '\0';

	freecon(raw);

	/* set new context */
	mnt_optstr_remove_option_at(optstr, begin, end);
	rc = insert_value(optstr, begin, val, next);
	free(val);

	return rc;
}
#endif

static int set_uint_value(char **optstr, unsigned int num,
			char *begin, char *end, char **next)
{
	char buf[40];
	snprintf(buf, sizeof(buf), "%u", num);

	mnt_optstr_remove_option_at(optstr, begin, end);
	return insert_value(optstr, begin, buf, next);
}

/*
 * @optstr: string with a comma separated list of options
 * @value: pointer to the beginning of the uid value
 * @valsz: size of the value
 * @next: returns pointer to the next option (optional argument)

 * Translates "username" or "useruid" to the real UID.
 *
 * For example:
 *	if (!mnt_optstr_get_option(optstr, "uid", &val, &valsz))
 *		mnt_optstr_fix_uid(&optstr, val, valsz, NULL);
 *
 * Returns: 0 on success, a negative number in case of error.
 */
int mnt_optstr_fix_uid(char **optstr, char *value, size_t valsz, char **next)
{
	char *end;

	if (!optstr || !*optstr || !value || !valsz)
		return -EINVAL;

	DBG(CXT, ul_debug("fixing uid"));

	end = value + valsz;

	if (valsz == 7 && !strncmp(value, "useruid", 7) &&
	    (*(value + 7) == ',' || !*(value + 7)))
		return set_uint_value(optstr, getuid(), value, end, next);

	if (!isdigit(*value)) {
		uid_t id;
		int rc;
		char *p = strndup(value, valsz);
		if (!p)
			return -ENOMEM;
		rc = mnt_get_uid(p, &id);
		free(p);

		if (!rc)
			return set_uint_value(optstr, id, value, end, next);
	}

	if (next) {
		/* no change, let's keep the original value */
		*next = value + valsz;
		if (**next == ',')
			(*next)++;
	}

	return 0;
}

/*
 * @optstr: string with a comma separated list of options
 * @value: pointer to the beginning of the uid value
 * @valsz: size of the value
 * @next: returns pointer to the next option (optional argument)

 * Translates "groupname" or "usergid" to the real GID.
 *
 * Returns: 0 on success, a negative number in case of error.
 */
int mnt_optstr_fix_gid(char **optstr, char *value, size_t valsz, char **next)
{
	char *end;

	if (!optstr || !*optstr || !value || !valsz)
		return -EINVAL;

	DBG(CXT, ul_debug("fixing gid"));

	end = value + valsz;

	if (valsz == 7 && !strncmp(value, "usergid", 7) &&
	    (*(value + 7) == ',' || !*(value + 7)))
		return set_uint_value(optstr, getgid(), value, end, next);

	if (!isdigit(*value)) {
		int rc;
		gid_t id;
		char *p = strndup(value, valsz);
		if (!p)
			return -ENOMEM;
		rc = mnt_get_gid(p, &id);
		free(p);

		if (!rc)
			return set_uint_value(optstr, id, value, end, next);

	}

	if (next) {
		/* nothing */
		*next = value + valsz;
		if (**next == ',')
			(*next)++;
	}
	return 0;
}

/*
 * Converts "user" to "user=<username>".
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_optstr_fix_user(char **optstr)
{
	char *username;
	struct libmnt_optloc ol;
	int rc = 0;

	DBG(CXT, ul_debug("fixing user"));

	mnt_init_optloc(&ol);

	rc = mnt_optstr_locate_option(*optstr, "user", &ol);
	if (rc)
		return rc == 1 ? 0 : rc;	/* 1: user= not found */

	username = mnt_get_username(getuid());
	if (!username)
		return -ENOMEM;

	if (!ol.valsz || (ol.value && strncmp(ol.value, username, ol.valsz) != 0)) {
		if (ol.valsz)
			/* remove old value */
			mnt_optstr_remove_option_at(optstr, ol.value, ol.end);

		rc = insert_value(optstr, ol.value ? ol.value : ol.end,
				  username, NULL);
	}

	free(username);
	return rc;
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
	char *name, *pat = (char *) pattern;
	char *buf, *patval;
	size_t namesz = 0, patvalsz = 0;
	int match = 1;

	if (!pattern && !optstr)
		return 1;
	if (!pattern)
		return 0;

	buf = malloc(strlen(pattern) + 1);
	if (!buf)
		return 0;

	/* walk on pattern string
	 */
	while (match && !mnt_optstr_next_option(&pat, &name, &namesz,
						&patval, &patvalsz)) {
		char *val;
		size_t sz;
		int no = 0, rc;

		if (*name == '+')
			name++, namesz--;
		else if ((no = (startswith(name, "no") != NULL)))
			name += 2, namesz -= 2;

		xstrncpy(buf, name, namesz + 1);

		rc = mnt_optstr_get_option(optstr, buf, &val, &sz);

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

#ifdef TEST_PROGRAM

static int test_append(struct libmnt_test *ts, int argc, char *argv[])
{
	const char *value = NULL, *name;
	char *optstr;
	int rc;

	if (argc < 3)
		return -EINVAL;
	optstr = strdup(argv[1]);
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
	optstr = strdup(argv[1]);
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

	optstr = strdup(argv[1]);

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

	optstr = strdup(argv[1]);

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

	optstr = strdup(argv[2]);
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
	optstr = strdup(argv[1]);
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
	optstr = strdup(argv[1]);
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
	optstr = strdup(argv[1]);
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

	if (argc < 2)
		return -EINVAL;

	next = optstr = strdup(argv[1]);

	printf("optstr: %s\n", optstr);

	while (!mnt_optstr_next_option(&next, &name, &namesz, &val, &valsz)) {

		if (!strncmp(name, "uid", 3))
			rc = mnt_optstr_fix_uid(&optstr, val, valsz, &next);
		else if (!strncmp(name, "gid", 3))
			rc = mnt_optstr_fix_gid(&optstr, val, valsz, &next);
		else if (!strncmp(name, "context", 7))
			rc = mnt_optstr_fix_secontext(&optstr, val, valsz, &next);
		if (rc)
			break;
	}
	if (rc)
		rc = mnt_optstr_fix_user(&optstr);

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
