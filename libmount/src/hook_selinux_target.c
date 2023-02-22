/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * This file is part of libmount from util-linux project.
 *
 * Copyright (C) 2023 Karel Zak <kzak@redhat.com>
 *
 * libmount is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 *
 * Please, see the comment in libmount/src/hooks.c to understand how hooks work.
 */
#include "mountP.h"
#include "fileutils.h"

#ifdef HAVE_LIBSELINUX
#include <selinux/selinux.h>
#include <selinux/context.h>

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

static int hook_prepare_target(
			struct libmnt_context *cxt,
			const struct libmnt_hookset *hs __attribute__((__unused__)),
			void *data __attribute__((__unused__)))
{
	int rc = 0;
	const char *tgt, *val;
	char *raw = NULL;
	struct libmnt_optlist *ol;
	struct libmnt_opt *opt;

	assert(cxt);

	tgt = mnt_fs_get_target(cxt->fs);
	if (!tgt)
		return 0;
	if (cxt->action != MNT_ACT_MOUNT)
		return 0;

	ol = mnt_context_get_optlist(cxt);
	if (!ol)
		return -EINVAL;

	opt = mnt_optlist_get_named(ol, "rootcontext", NULL);
	if (!opt)
		return 0;
	val = mnt_opt_get_value(opt);
	if (!val || strcmp(val, "@target") != 0)
		return 0;

	rc = getfilecon_raw(tgt, &raw);
	if (rc <= 0 || !raw)
		rc = errno ? -errno : -EINVAL;
	else
		rc = 0;	/* getfilecon_raw(3) returns the size of the extended attribute value */

	if (!rc)
		rc = mnt_opt_set_quoted_value(opt, raw);
	if (raw)
		freecon(raw);

	return rc;
}

const struct libmnt_hookset hookset_selinux_target =
{
	.name = "__selinux_target",

	.firststage = MNT_STAGE_PREP_TARGET,
	.firstcall = hook_prepare_target,

	.deinit = hookset_deinit
};

#endif /* HAVE_LIBSELINUX */
