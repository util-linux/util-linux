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
 */

#include "mountP.h"
#include "fileutils.h"

static int hook_prepare_target(struct libmnt_context *cxt, const struct libmnt_hookset *hs, void *data);

static int hookset_init(struct libmnt_context *cxt, const struct libmnt_hookset *hs)
{
	DBG(HOOK, ul_debugobj(hs, "init '%s'", hs->name));

	return mnt_context_append_hook(cxt, hs,
				MNT_STAGE_PREP_TARGET, NULL, hook_prepare_target);
}

static int hookset_deinit(struct libmnt_context *cxt, const struct libmnt_hookset *hs)
{
	void *data = NULL;

	DBG(HOOK, ul_debugobj(hs, "deinit '%s'", hs->name));

	/* remove all our hooks and free hook data */
	while (mnt_context_remove_hook(cxt, hs, 0, &data) == 0) {
		if (data)
			free(data);
		data = NULL;
	}

	return 0;
}

static int is_mkdir_required(const char *tgt, struct libmnt_fs *fs, mode_t *mode, int *rc)
{
	char *mstr = NULL;
	size_t mstr_sz = 0;
	struct stat st;

	assert(tgt);
	assert(fs);
	assert(mode);
	assert(rc);

	*mode = 0;
	*rc = 0;

	if (mnt_optstr_get_option(fs->user_optstr, "X-mount.mkdir", &mstr, &mstr_sz) != 0 &&
	    mnt_optstr_get_option(fs->user_optstr, "x-mount.mkdir", &mstr, &mstr_sz) != 0)   	/* obsolete */
		return 0;

	if (mnt_stat_mountpoint(tgt, &st) == 0)
		return 0;

	DBG(HOOK, ul_debug("mkdir %s (%s) wanted", tgt, mstr));

	if (mstr && mstr_sz) {
		char *end = NULL;

		if (*mstr == '"')
			mstr++, mstr_sz-=2;

		errno = 0;
		*mode = strtol(mstr, &end, 8);

		if (errno || !end || mstr + mstr_sz != end) {
			DBG(HOOK, ul_debug("failed to parse mkdir mode '%s'", mstr));
			*rc = -MNT_ERR_MOUNTOPT;
			return 0;
		}
	}

	if (!*mode)
		*mode = S_IRWXU |			/* 0755 */
		       S_IRGRP | S_IXGRP |
		       S_IROTH | S_IXOTH;

	return 1;
}


static int hook_prepare_target(
			struct libmnt_context *cxt,
			const struct libmnt_hookset *hs,
			void *data __attribute__((__unused__)))
{
	int rc = 0;
	mode_t mode = 0;
	const char *tgt;

	assert(cxt);

	tgt = mnt_fs_get_target(cxt->fs);
	if (!tgt)
		return 0;

	if (cxt->action == MNT_ACT_MOUNT
	    && (cxt->user_mountflags & MNT_MS_XCOMMENT ||
		cxt->user_mountflags & MNT_MS_XFSTABCOMM)
	    && is_mkdir_required(tgt, cxt->fs, &mode, &rc)) {

		/* supported only for root or non-suid mount(8) */
		if (!mnt_context_is_restricted(cxt)) {
			rc = ul_mkdir_p(tgt, mode);
			if (rc)
				DBG(HOOK, ul_debugobj(hs, "mkdir %s failed: %m", tgt));
		} else
			rc = -EPERM;
	}

	return rc;
}


const struct libmnt_hookset hookset_mkdir =
{
	.name = "__mkdir",
	.init = hookset_init,
	.deinit = hookset_deinit
};
