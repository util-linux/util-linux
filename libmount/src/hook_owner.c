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
 * This is X-mount.owner=, X-mount.group= and X-mount.mode= implementation.
 */

#include <sched.h>

#include "mountP.h"
#include "fileutils.h"

struct hook_data {
	uid_t owner;
	gid_t group;
	mode_t mode;
};

/* de-initiallize this module */
static int hookset_deinit(struct libmnt_context *cxt, const struct libmnt_hookset *hs)
{
	void *data;

	DBG(HOOK, ul_debugobj(hs, "deinit '%s'", hs->name));

	/* remove all our hooks and free hook data */
	while (mnt_context_remove_hook(cxt, hs, 0, &data) == 0) {
		if (data)
			free(data);
		data = NULL;
	}

	return 0;
}

static int hook_mount_post(
			struct libmnt_context *cxt,
			const struct libmnt_hookset *hs __attribute__((__unused__)),
			void *data)
{
	struct hook_data *hd = (struct hook_data *) data;
	const char *target;
	int rc = 0;

	assert(cxt);

	if (!hd || !cxt->fs)
		return 0;

	target = mnt_fs_get_target(cxt->fs);
	if (!target)
		return 0;

	if (hd->owner != (uid_t) -1 || hd->group != (uid_t) -1) {
		DBG(CXT, ul_debugobj(cxt, " lchown(%s, %u, %u)", target, hd->owner, hd->group));
		if (lchown(target, hd->owner, hd->group) == -1)
			return -MNT_ERR_CHOWN;
	}

	if (hd->mode != (mode_t) -1) {
		DBG(CXT, ul_debugobj(cxt, " chmod(%s, %04o)", target, hd->mode));
		if (chmod(target, hd->mode) == -1)
			return -MNT_ERR_CHMOD;
	}

	return rc;
}

static inline struct hook_data *new_hook_data(void)
{
	struct hook_data *hd = calloc(1, sizeof(*hd));

	if (!hd)
		return NULL;

	hd->owner = (uid_t) -1;
	hd->group = (gid_t) -1;
	hd->mode = (mode_t) -1;
	return hd;
}

static int hook_prepare_options(
			struct libmnt_context *cxt,
			const struct libmnt_hookset *hs,
			void *data __attribute__((__unused__)))
{
	struct hook_data *hd = NULL;
	const char *o;
	int rc = 0;
	char *value;
	size_t valsz;

	o = mnt_fs_get_user_options(cxt->fs);
	if (!o)
		return 0;

	if ((rc = mnt_optstr_get_option(o, "X-mount.owner", &value, &valsz)) < 0)
		goto fail;
	if (rc == 0) {
		if (!valsz)
			goto fail;
		if (!hd) {
			hd = new_hook_data();
			if (!hd)
				goto fail;
		}
		if (mnt_parse_uid(value, valsz, &hd->owner))
			goto fail;
	}

	if ((rc = mnt_optstr_get_option(o, "X-mount.group", &value, &valsz)) < 0)
		goto fail;
	if (rc == 0) {
		if (!valsz)
			goto fail;
		if (!hd) {
			hd = new_hook_data();
			if (!hd)
				goto fail;
		}
		if (mnt_parse_gid(value, valsz, &hd->group))
			goto fail;
	}

	if ((rc = mnt_optstr_get_option(o, "X-mount.mode", &value, &valsz)) < 0)
		goto fail;
	if (rc == 0) {
		if (!valsz)
			goto fail;
		if (!hd) {
			hd = new_hook_data();
			if (!hd)
				goto fail;
		}
		if (mnt_parse_mode(value, valsz, &hd->mode))
			goto fail;
	}

	if (hd) {
		DBG(CXT, ul_debugobj(cxt, " wanted ownership %d:%d, mode %04o",
					hd->owner, hd->group, hd->mode));
		rc = mnt_context_append_hook(cxt, hs,
				MNT_STAGE_MOUNT_POST,
				hd, hook_mount_post);
		if (rc < 0)
			goto fail;
	}
	return 0;
fail:
	if (rc == 0)
		rc = -MNT_ERR_MOUNTOPT;
	free(hd);
	return rc;
}


const struct libmnt_hookset hookset_owner =
{
	.name = "__owner",

	.firststage = MNT_STAGE_PREP_OPTIONS,
	.firstcall = hook_prepare_options,

	.deinit = hookset_deinit
};
