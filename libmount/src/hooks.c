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
 * The "hookset" is set of callbacks (hooks) that implement some functionality.
 * It supports two kinds of data:
 *
 *  - global data    : accessible for all callbacks, independent on defined hooks
 *
 *  - per-hook data  : usually used by the callback function
 */

#include "mountP.h"
#include "mount-api-utils.h"

/* built-in hooksets */
static const struct libmnt_hookset *hooksets[] =
{
#ifdef __linux__
	&hookset_mkdir,
	&hookset_subdir,
	&hookset_mount_legacy,
#ifdef UL_HAVE_MOUNT_API
	&hookset_idmap,
#endif
	&hookset_owner
#endif
};

/* hooksets data */
struct hookset_data {
	const struct libmnt_hookset *hookset;
	void *data;

	struct list_head	datas;
};

/* individial callback */
struct hookset_hook {
	const struct libmnt_hookset *hookset;
	int stage;
	void *data;

	int (*func)(struct libmnt_context *, const struct libmnt_hookset *, void *);

	struct list_head	hooks;
};

static const char *stagenames[] = {
	/* prepare */
	[MNT_STAGE_PREP_SOURCE] = "prep-source",
	[MNT_STAGE_PREP_TARGET] = "prep-target",
	[MNT_STAGE_PREP_OPTIONS] = "prep-options",
	[MNT_STAGE_PREP] = "prep",

	/* mount */
	[MNT_STAGE_MOUNT_PRE] = "pre-mount",
	[MNT_STAGE_MOUNT] = "mount",
	[MNT_STAGE_MOUNT_POST] = "post-mount",

	/* post */
	[MNT_STAGE_POST] = "post",
};

int mnt_context_deinit_hooksets(struct libmnt_context *cxt)
{
	size_t i;
	int rc = 0;

	assert(cxt);

	if (list_empty(&cxt->hooksets_hooks))
		return 0;

	for (i = 0; i <  ARRAY_SIZE(hooksets); i++) {
		const struct libmnt_hookset *hs = hooksets[i];

		rc += hs->deinit(cxt, hs);
	}

	assert(list_empty(&cxt->hooksets_datas));
	assert(list_empty(&cxt->hooksets_hooks));

	INIT_LIST_HEAD(&cxt->hooksets_datas);
	INIT_LIST_HEAD(&cxt->hooksets_hooks);

	return rc;
}

const struct libmnt_hookset *mnt_context_get_hookset(
			struct libmnt_context *cxt, const char *name)
{
	size_t i;

	assert(cxt);
	assert(name);

	for (i = 0; i <  ARRAY_SIZE(hooksets); i++) {
		const struct libmnt_hookset *hs = hooksets[i];

		if (strcmp(name, hs->name) == 0)
			return hs;
	}

	return NULL;
}

static struct hookset_data *get_hookset_data(
			struct libmnt_context *cxt,
			const struct libmnt_hookset *hs)
{
	struct list_head *p;

	assert(cxt);
	assert(hs);

	list_for_each(p, &cxt->hooksets_datas) {
		struct hookset_data *x = list_entry(p, struct hookset_data, datas);

		if (x->hookset == hs)
			return x;
	}
	return 0;
}

int mnt_context_set_hookset_data(struct libmnt_context *cxt,
				 const struct libmnt_hookset *hs,
				 void *data)
{
	struct hookset_data *hd = NULL;

	hd = get_hookset_data(cxt, hs);

	/* deallocate old data */
	if (data == NULL) {
		if (hd) {
			DBG(CXT, ul_debugobj(cxt, " free '%s' data", hs->name));
			list_del(&hd->datas);
			free(hd);
		}
		return 0;
	}

	/* create and append new data */
	if (!hd) {
		hd = calloc(1, sizeof(*hd));
		if (!hd)
			return -ENOMEM;

		DBG(CXT, ul_debugobj(cxt, " alloc '%s' data", hs->name));
		INIT_LIST_HEAD(&hd->datas);
		hd->hookset = hs;
		list_add_tail(&hd->datas, &cxt->hooksets_datas);

	}
	hd->data = data;
	return 0;
}

void *mnt_context_get_hookset_data(struct libmnt_context *cxt,
				   const struct libmnt_hookset *hs)
{
	struct hookset_data *hd = get_hookset_data(cxt, hs);

	return hd ? hd->data : NULL;
}

int mnt_context_append_hook(struct libmnt_context *cxt,
			const struct libmnt_hookset *hs,
			int stage,
			void *data,
			int (*func)(struct libmnt_context *,
				    const struct libmnt_hookset *,
				    void *))
{
	struct hookset_hook *hook;

	assert(cxt);
	assert(hs);
	assert(stage);

	hook = calloc(1, sizeof(*hook));
	if (!hook)
		return -ENOMEM;

	DBG(CXT, ul_debugobj(cxt, " appending %s hook from %s",
				stagenames[stage], hs->name));

	INIT_LIST_HEAD(&hook->hooks);

	hook->hookset = hs;
	hook->data = data;
	hook->func = func;
	hook->stage = stage;

	list_add_tail(&hook->hooks, &cxt->hooksets_hooks);
	return 0;
}

static struct hookset_hook *get_hookset_hook(struct libmnt_context *cxt,
					     const struct libmnt_hookset *hs,
					     int stage,
					     void *data)
{
	struct list_head *p, *next;

	assert(cxt);

	list_for_each_safe(p, next, &cxt->hooksets_hooks) {
		struct hookset_hook *x = list_entry(p, struct hookset_hook, hooks);

		if (hs && x->hookset != hs)
			continue;
		if (stage && x->stage != stage)
			continue;
		if (data && x->data != data)
			continue;
		return x;
	}

	return NULL;
}

int mnt_context_remove_hook(struct libmnt_context *cxt,
			    const struct libmnt_hookset *hs,
			    int stage,
			    void **data)
{
	struct hookset_hook *hook;

	assert(cxt);

	hook = get_hookset_hook(cxt, hs, stage, NULL);
	if (hook) {
		DBG(CXT, ul_debugobj(cxt, " removing %s hook from %s",
			stagenames[hook->stage], hook->hookset->name));

		if (data)
			*data = hook->data;

		list_del(&hook->hooks);
		free(hook);
		return 0;
	}

	return 1;
}

int mnt_context_has_hook(struct libmnt_context *cxt,
			 const struct libmnt_hookset *hs,
			 int stage,
			 void *data)
{
	return get_hookset_hook(cxt, hs, stage, data) ? 1 : 0;
}

int mnt_context_call_hooks(struct libmnt_context *cxt, int stage)
{
	struct list_head *p, *next;
	size_t i;

	/* call initial hooks */
	for (i = 0; i <  ARRAY_SIZE(hooksets); i++) {
		int rc;
		const struct libmnt_hookset *hs = hooksets[i];

		if (hs->firststage != stage)
			continue;

		DBG(CXT, ul_debugobj(cxt, "calling %s hook from %s",
                        stagenames[hs->firststage], hs->name));

		rc = hs->firstcall(cxt, hs, NULL);
		if (rc < 0)
			return rc;
	}

	/* call already active hooks */
	list_for_each_safe(p, next, &cxt->hooksets_hooks) {
		int rc;
		struct hookset_hook *x = list_entry(p, struct hookset_hook, hooks);

		if (x->stage != stage)
			continue;

		DBG(CXT, ul_debugobj(cxt, "calling %s hook from %s",
			stagenames[x->stage], x->hookset->name));

		rc = x->func(cxt, x->hookset, x->data);
		if (rc < 0)
			return rc;
	}

	return 0;
}
