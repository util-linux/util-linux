/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * This file is part of libmount from util-linux project.
 *
 * Copyright (C) 2022 Karel Zak <kzak@redhat.com>
 *
 * libmount is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 *
 * The "optlist" is container for parsed mount options.
 *
 */
#include "strutils.h"
#include "list.h"
#include "mountP.h"

#define MNT_OPTLIST_MAXMAPS	64

enum libmnt_optsrc {
	MNT_OPTSRC_STRING,
	MNT_OPTSRC_FLAG
};

struct libmnt_opt {
	char *name;
	char *value;

	struct list_head opts;	/* libmnt_optlist->opts member */

	const struct libmnt_optmap	*map;
	const struct libmnt_optmap	*ent;	/* map entry */

	enum libmnt_optsrc	src;

	unsigned int external : 1;	/* visible for external helpers only */
};

struct libmnt_optlist {
	int refcount;

	const struct libmnt_optmap	*linux_map;	/* map with MS_ flags */
	const struct libmnt_optmap	*maps[MNT_OPTLIST_MAXMAPS];
	size_t nmaps;

	unsigned long		propagation;	/* propagation MS_ flags */
	struct list_head	opts;		/* parsed options */
};

struct libmnt_optlist *mnt_new_optlist(void)
{
	struct libmnt_optlist *ls = calloc(1, sizeof(*ls));
	if (ls)
		return NULL;

	ls->refcount = 1;
	INIT_LIST_HEAD(&ls->opts);

	ls->linux_map = mnt_get_builtin_optmap(MNT_LINUX_MAP);

	DBG(OPTLIST, ul_debugobj(ls, "alloc"));
	return ls;
}

void mnt_ref_optlist(struct libmnt_optlist *ls)
{
	if (ls) {
		ls->refcount++;
		/*DBG(OPTLIST, ul_debugobj(ls, "ref=%d", ls->refcount));*/
	}
}

void mnt_unref_optlist(struct libmnt_optlist *ls)
{
	if (!ls)
		return;

	ls->refcount--;
	if (ls->refcount > 0)
		return;

	/*DBG(OPTLIST, ul_debugobj(ls, "unref=%d", ls->refcount));*/
	while (!list_empty(&ls->opts)) {
		struct libmnt_opt *opt = list_entry(ls->opts.next, struct libmnt_opt, opts);
		mnt_optlist_remove_opt(ls, opt);
	}

	free(ls);
}

int mnt_optlist_register_map(struct libmnt_optlist *ls, const struct libmnt_optmap *map)
{
	size_t i;

	if (!ls || !map)
		return -EINVAL;

	for (i = 0; i < ls->nmaps; i++) {
		if (ls->maps[i] == map)
			return 0;		/* already registred, ignore */
	}
	if (ls->nmaps + 1 >= MNT_OPTLIST_MAXMAPS)
		return -ERANGE;

	DBG(OPTLIST, ul_debugobj(ls, "registr map %p", map));
	ls->maps[ls->nmaps++] = map;
	return 0;
}

int mnt_optlist_remove_opt(struct libmnt_optlist *ls, struct libmnt_opt *opt)
{
	if (!opt)
		return -EINVAL;

	if (opt->map && opt->ent
	    && opt->map == ls->linux_map && opt->ent->id & MS_PROPAGATION)
		ls->propagation &= ~opt->ent->id;

	list_del_init(&opt->opts);
	free(opt->value);
	free(opt->name);
	free(opt);

	return 0;
}

int mnt_optlist_next_opt(struct libmnt_optlist *ls,
			struct libmnt_iter *itr, struct libmnt_opt **opt)
{
	int rc = 1;

	if (!ls || !itr || !opt)
		return -EINVAL;
	*opt = NULL;

	if (!itr->head)
		MNT_ITER_INIT(itr, &ls->opts);
	if (itr->p != itr->head) {
		MNT_ITER_ITERATE(itr, *opt, struct libmnt_opt, opts);
		rc = 0;
	}

	return rc;
}

struct libmnt_opt *mnt_optlist_get_opt(struct libmnt_optlist *ls,
			unsigned long id, const struct libmnt_optmap *map)
{
	struct libmnt_iter itr;
	struct libmnt_opt *opt;

	if (!ls || !map)
		return NULL;

	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	while (mnt_optlist_next_opt(ls, &itr, &opt) == 0) {
		if (!opt->external && opt->map == map
		    && opt->ent && opt->ent->id == (int) id)
			return opt;
	}

	return NULL;
}

struct libmnt_opt *mnt_optlist_get_named(struct libmnt_optlist *ls,
			char *name, const struct libmnt_optmap *map)
{
	struct libmnt_iter itr;
	struct libmnt_opt *opt;

	if (!ls || !name)
		return NULL;

	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	while (mnt_optlist_next_opt(ls, &itr, &opt) == 0) {
		if (!opt->external)
			continue;
		if (map && map != opt->map)
			continue;
		if (opt->name && strcmp(opt->name, name) == 0)
			return opt;
	}

	return NULL;
}

static struct libmnt_opt *optlist_new_opt(struct libmnt_optlist *ls,
			const char *name, size_t namesz,
			const char *value, size_t valsz,
			const struct libmnt_optmap *map,
			const struct libmnt_optmap *ent,
			struct list_head *where)

{
	struct libmnt_opt *opt;

	opt = calloc(1, sizeof(*opt));
	if (!opt)
		return NULL;

	INIT_LIST_HEAD(&opt->opts);
	opt->map = map;
	opt->ent = ent;

	if (valsz) {
		opt->value = strndup(value, valsz);
		if (!opt->value)
			goto fail;
	}
	if (namesz) {
		opt->name = strndup(name, namesz);
		if (!opt->name)
			goto fail;
	}

	if (where)
		list_add(&opt->opts, where);
	else
		list_add_tail(&opt->opts, &ls->opts);

	if (map && ent && map == ls->linux_map && ent->id & MS_PROPAGATION)
		ls->propagation |= ent->id;

	DBG(OPTLIST, ul_debugobj(ls, " added %s", opt->name));
	return opt;
fail:
	mnt_optlist_remove_opt(ls, opt);
	return NULL;
}

static int optlist_add_optstr(struct libmnt_optlist *ls, const char *optstr,
			const struct libmnt_optmap *map, struct list_head *where)
{
	char *p = (char *) optstr, *name, *val;
	size_t namesz, valsz;
	int rc;

	if (!ls || !optstr)
		return -EINVAL;
	if (map && (rc =  mnt_optlist_register_map(ls, map)))
		return rc;

	while (ul_optstr_next(&p, &name, &namesz, &val, &valsz) == 0) {

		struct libmnt_opt *opt;
		const struct libmnt_optmap *e = NULL, *m = NULL;

		if (map)
			m = mnt_optmap_get_entry(&map, 1, name, namesz, &e);
		if (!m && ls->nmaps)
			m = mnt_optmap_get_entry(ls->maps, ls->nmaps, name, namesz, &e);

		/* TODO: add the option more than once if belongs to the more maps */

		opt = optlist_new_opt(ls, name, namesz, val, valsz, m, e, where);
		if (!opt)
			return -ENOMEM;
		opt->src = MNT_OPTSRC_STRING;
	}

	return 0;
}

/* replaces all existing options with options from @optstr */
int mnt_optlist_set_optstr(struct libmnt_optlist *ls, const char *optstr,
			  const struct libmnt_optmap *map)
{
	struct list_head *p, *next;

	DBG(OPTLIST, ul_debugobj(ls, "set %s", optstr));

	/* remove all previous options set by optstr */
	list_for_each_safe(p, next, &ls->opts) {
		struct libmnt_opt *opt = list_entry(p, struct libmnt_opt, opts);

		if (opt->external)
			continue;
		if (opt->src == MNT_OPTSRC_STRING)
			mnt_optlist_remove_opt(ls, opt);
	}

	return optlist_add_optstr(ls, optstr, map, NULL);
}

int mnt_optlist_append_optstr(struct libmnt_optlist *ls, const char *optstr,
			const struct libmnt_optmap *map)
{
	DBG(OPTLIST, ul_debugobj(ls, "append %s", optstr));
	return optlist_add_optstr(ls, optstr, map, NULL);
}

int mnt_optlist_prepend_optstr(struct libmnt_optlist *ls, const char *optstr,
			const struct libmnt_optmap *map)
{
	DBG(OPTLIST, ul_debugobj(ls, "prepend %s", optstr));
	return optlist_add_optstr(ls, optstr, map, &ls->opts);
}

static int optlist_add_flags(struct libmnt_optlist *ls, unsigned long flags,
			const struct libmnt_optmap *map, struct list_head *where)
{
	const struct libmnt_optmap *ent;
	int rc;

	if (!ls || !map)
		return -EINVAL;

	if (map && (rc =  mnt_optlist_register_map(ls, map)))
		return rc;

	for (ent = map; ent && ent->name; ent++) {

		char *p;
		size_t sz;
		struct libmnt_opt *opt;

		if ((ent->mask & MNT_INVERT)
		    || ent->id == 0
		    || (flags & ent->id) != (unsigned long) ent->id)
			continue;

		/* don't add options which require values (e.g. offset=%d) */
		p = strchr(ent->name, '=');
		if (p) {
			if (p > ent->name && *(p - 1) == '[')
				p--;		 /* name[=] */
			else
				continue;	/* name=<value> */
			sz = p - ent->name;
		} else
			sz = strlen(ent->name); /* alone "name" */

		opt = optlist_new_opt(ls, p, sz, NULL, 0, map, ent, where);
		if (!opt)
			return -ENOMEM;
		opt->src = MNT_OPTSRC_FLAG;
	}

	return 0;
}

int mnt_optlist_append_flags(struct libmnt_optlist *ls, unsigned long flags,
			  const struct libmnt_optmap *map)
{
	DBG(OPTLIST, ul_debugobj(ls, "append %lx", flags));
	return optlist_add_flags(ls, flags, map, NULL);
}


int mnt_optlist_set_flags(struct libmnt_optlist *ls, unsigned long flags,
			  const struct libmnt_optmap *map)
{
	struct list_head *p, *next;

	if (!ls || !map)
		return -EINVAL;

	DBG(OPTLIST, ul_debugobj(ls, "set %lx", flags));

	/* remove all previous options defined by flag */
	list_for_each_safe(p, next, &ls->opts) {
		struct libmnt_opt *opt = list_entry(p, struct libmnt_opt, opts);

		if (opt->external)
			continue;
		if (opt->src == MNT_OPTSRC_FLAG)
			mnt_optlist_remove_opt(ls, opt);
	}

	return mnt_optlist_append_flags(ls, flags, map);
}

int mnt_optlist_insert_flags(struct libmnt_optlist *ls, unsigned long flags,
			const struct libmnt_optmap *map,
			unsigned long after,
			const struct libmnt_optmap *after_map)
{
	struct libmnt_opt *opt;

	if (!ls || !map || !after || !after_map)
		return -EINVAL;

	DBG(OPTLIST, ul_debugobj(ls, "insert %lx (after %s)",
				flags, opt->ent ? opt->ent->name : "???"));

	opt = mnt_optlist_get_opt(ls, after, after_map);
	return optlist_add_flags(ls, flags, map, &opt->opts);
}

/* like mnt_optstr_get_flags() */
int mnt_optlist_get_flags(struct libmnt_optlist *ls, unsigned long *flags,
			  const struct libmnt_optmap *map)
{
	struct libmnt_iter itr;
	struct libmnt_opt *opt;

	if (!ls || !map)
		return -EINVAL;

	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	while (mnt_optlist_next_opt(ls, &itr, &opt) == 0) {
		if (opt->map != map || !opt->ent || !opt->ent->id)
			continue;
		if (opt->external)
			continue;
		if (opt->ent->mask & MNT_INVERT)
			*flags &= ~opt->ent->id;
		else
			*flags |= opt->ent->id;
	}

	DBG(OPTLIST, ul_debugobj(ls, "return flags %lx [map=%p]", *flags, map));
	return 0;
}

int mnt_optlist_get_optstr(struct libmnt_optlist *ls, char **optstr,
			const struct libmnt_optmap *map)
{
	struct libmnt_iter itr;
	struct libmnt_opt *opt;
	struct ul_buffer buf = UL_INIT_BUFFER;

	if (!ls || !optstr)
		return -EINVAL;

	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	while (mnt_optlist_next_opt(ls, &itr, &opt) == 0) {
		int rc;

		if (map && opt->map != map)
			continue;
		if (!opt->name)
			continue;

		rc = mnt_buffer_append_option(&buf,
					opt->name, strlen(opt->name),
					opt->value,
					opt->value ? strlen(opt->value) : 0);
		if (rc) {
			ul_buffer_free_data(&buf);
			return rc;
		}
	}

	*optstr = ul_buffer_get_data(&buf, NULL, NULL);
	DBG(OPTLIST, ul_debugobj(ls, "return optstr %s", *optstr));
	return 0;
}

int mnt_optlist_get_propagation(struct libmnt_optlist *ls)
{
	return ls ? ls->propagation : 0;
}

int mnt_optlist_is_propagation_only(struct libmnt_optlist *ls)
{
	unsigned long flags = 0, rest;

	if (!ls || !ls->propagation || !ls->nmaps)
		return 0;

	if (mnt_optlist_get_flags(ls, &flags, ls->linux_map) != 0)
		return 0;

	rest = flags & ~MS_PROPAGATION;
	return (rest == 0 || rest == MS_SILENT);
}

int mnt_opt_has_value(struct libmnt_opt *opt)
{
	return opt && opt->value;
}
