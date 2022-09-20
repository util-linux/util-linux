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
 * This is fsconfig/fsopen based mount.
 *
 * Please, see the comment in libmount/src/hooks.c to understand how hooks work.
 *
 * Operations: functions and STAGE, all is prepared in hook_prepare():
 *
 * mount:
 *	- fsopen	PRE
 *	- fsmount	MOUNT
 *	- mount_setattr MOUNT (VFS flags)
 *	- mount_move	POST
 *	- mount_setattr POST (propagation)
 *
 * remount:
 *	- open_tree	PRE
 *	- reconfigure   MOUNT
 *	- mount_setattr	MOUNT (VFS flags)
 *	- mount_setattr POST (propagation)
 *
 * propagation-only:
 *	- open_tree	PRE
 *	- mount_setattr	POST (propagation)
 *
 * move:
 *	- open_tree	PRE
 *	- mount_move	POST
 *
 * bind:
 *	- open_tree	PRE (clone)
 *	- mount_setattr MOUNT (VFS flags)
 *	- mount_move	POST
 */

#include "mountP.h"

#include <inttypes.h>

/*
 * This hookset uses 'struct libmnt_sysapi' (mountP.h) as hookset data.
 */
static void free_hookset_data(	struct libmnt_context *cxt,
				const struct libmnt_hookset *hs)
{
	struct libmnt_sysapi *api = mnt_context_get_hookset_data(cxt, hs);

	if (!api)
		return;
	if (api->fd_fs >= 0)
		close(api->fd_fs);
	if (api->fd_tree >= 0)
		close(api->fd_tree);

	free(api);
	mnt_context_set_hookset_data(cxt, hs, NULL);
}

/* global data, used by all callbacks */
static struct libmnt_sysapi *new_hookset_data(
				struct libmnt_context *cxt,
				const struct libmnt_hookset *hs)
{
	struct libmnt_sysapi *api = calloc(1, sizeof(struct libmnt_sysapi));

	if (!api)
		return NULL;
	api->fd_fs = api->fd_tree = -1;

	if (mnt_context_set_hookset_data(cxt, hs, api) != 0) {
		/* probably ENOMEM problem */
		free(api);
		api = NULL;
	}
	return api;
}

/* de-initiallize this module */
static int hookset_deinit(struct libmnt_context *cxt, const struct libmnt_hookset *hs)
{
	DBG(HOOK, ul_debugobj(hs, "deinit '%s'", hs->name));

	/* remove all our hooks */
	while (mnt_context_remove_hook(cxt, hs, 0, NULL) == 0);

	/* free and remove global hookset data */
	free_hookset_data(cxt, hs);

	return 0;
}

static inline struct libmnt_sysapi *get_sysapi(struct libmnt_context *cxt,
					const struct libmnt_hookset *hs)
{
	return mnt_context_get_hookset_data(cxt, hs);
}

static int set_vfs_flags(int fd, struct libmnt_context *cxt, int recursive)
{
	struct libmnt_optlist *ol;
	struct mount_attr attr = { .attr_clr = 0 };
	unsigned int callflags = AT_EMPTY_PATH;
	uint64_t mask = 0;
	int rc;

	ol= mnt_context_get_optlist(cxt);
	if (!ol)
		return -ENOMEM;

	mnt_optlist_get_attrs(ol, &mask);
	attr.attr_set = mask;
	if (recursive)
		callflags |= AT_RECURSIVE;

	DBG(HOOK, ul_debug(" mount_setattr(set=0x%" PRIx64")", mask));
	rc = mount_setattr(fd, "", callflags, &attr, sizeof(attr));
	if (rc)
		return -errno;

	return 0;
}

static int is_recursive_bind(struct libmnt_context *cxt)
{
	struct libmnt_optlist *ol = mnt_context_get_optlist(cxt);

	if (!ol)
		return 0;

	return mnt_optlist_is_rbind(ol);
}

static int hook_bind_attach(struct libmnt_context *cxt,
		const struct libmnt_hookset *hs,
		void *data __attribute__((__unused__)))
{
	struct libmnt_sysapi *api;
	const char *target;

	DBG(HOOK, ul_debugobj(hs, "bind: attach"));

	target = mnt_fs_get_target(cxt->fs);
	if (!target)
		return -EINVAL;

	api = get_sysapi(cxt, hs);
	if (!api || api->fd_tree <= 0)
		return -EINVAL;

	DBG(HOOK, ul_debugobj(hs, " move_mount(to=%s)", target));
	if (move_mount(api->fd_tree, "", AT_FDCWD, target, MOVE_MOUNT_F_EMPTY_PATH))
		return -errno;

	return 0;
}

static int hook_bind_setflags(struct libmnt_context *cxt,
			const struct libmnt_hookset *hs,
			void *data __attribute__((__unused__)))
{
	struct libmnt_sysapi *api;
	int rc;

	DBG(HOOK, ul_debugobj(hs, "bind: setting VFS"));

	api = get_sysapi(cxt, hs);
	if (!api || api->fd_tree <= 0)
		return -EINVAL;

	rc = set_vfs_flags(api->fd_tree, cxt, is_recursive_bind(cxt));
	if (rc)
		return rc;

	return mnt_context_append_hook(cxt, hs,
			MNT_STAGE_MOUNT_POST, NULL, hook_bind_attach);
}

/*
static int hook_remount(struct libmnt_context *cxt,
			const struct libmnt_hookset *hs, void *data)
{
	return 0;
}


static int hook_move(struct libmnt_context *cxt,
			const struct libmnt_hookset *hs, void *data)
{
	return 0;
}

static int hook_propagation(struct libmnt_context *cxt,
			const struct libmnt_hookset *hs, void *data)
{
	return 0;
}

static int hook_newmount(struct libmnt_context *cxt,
			const struct libmnt_hookset *hs, void *data)
{
	return 0;
}
*/

/*
 * analyze library context and register hook to call mount-like syscalls
 */
static int hook_prepare(struct libmnt_context *cxt,
			const struct libmnt_hookset *hs,
			void *data __attribute__((__unused__)))
{
	int rc = 0, next_stage = 0;
	int (*next_hook)(struct libmnt_context *, const struct libmnt_hookset *, void *) = NULL;
	unsigned long flags = 0, open_flags = 0;
	const char *tree_path = NULL, *src = NULL, *target = NULL;
	struct libmnt_sysapi *api;

	assert(cxt);
	assert(hs == &hookset_mount);

	DBG(HOOK, ul_debugobj(hs, "prepare new API mount"));

	target = mnt_fs_get_target(cxt->fs);
	if (!target)
		return -EINVAL;

	src = mnt_fs_get_srcpath(cxt->fs);

	rc = mnt_context_get_mflags(cxt, &flags);
	if (rc)
		return rc;

	api = new_hookset_data(cxt, hs);
	if (!api)
		return -ENOMEM;

	open_flags = OPEN_TREE_CLOEXEC;

	if (flags & MS_REMOUNT) {
		DBG(HOOK, ul_debugobj(hs, " prepare remount"));
		tree_path = target;
		next_stage = MNT_STAGE_MOUNT;
		/*next_hook = hook_remount;*/

	} else if (flags & MS_BIND) {
		DBG(HOOK, ul_debugobj(hs, " prepare bind"));
		tree_path = src;
		next_stage = MNT_STAGE_MOUNT;
		next_hook = (flags & MNT_BIND_SETTABLE) ? hook_bind_setflags :
							  hook_bind_attach;
		open_flags |= OPEN_TREE_CLONE;
		if (is_recursive_bind(cxt))
			open_flags |= AT_RECURSIVE;

	} else if (flags & MS_MOVE) {
		DBG(HOOK, ul_debugobj(hs, " prepare move"));
		tree_path = src;
		next_stage = MNT_STAGE_MOUNT_POST;
		/*next_hook = hook_move;*/

	} else if (mnt_context_propagation_only(cxt)) {
		DBG(HOOK, ul_debugobj(hs, " prepare propagation change"));
		tree_path = target;
		next_stage = MNT_STAGE_MOUNT_POST;
		/*next_hook = hook_propagation;*/

	} else {
		const char *type;

		DBG(HOOK, ul_debugobj(hs, " prepare mount"));
		next_stage = MNT_STAGE_MOUNT;
		/*next_hook = hook_newmount;*/

		type = mnt_fs_get_fstype(cxt->fs);
		if (!type)
			return -EINVAL;

		DBG(HOOK, ul_debugobj(hs, "fsopen(%s)", type));
		api->fd_fs = fsopen(type, FSOPEN_CLOEXEC);
		if (api->fd_fs < 0)
			goto nothing;
	}

	if (api->fd_fs == -1) {
		if (!tree_path) {
			DBG(HOOK, ul_debugobj(hs, "tree path undefined"));
			return -EINVAL;
		}
		DBG(HOOK, ul_debugobj(hs, "open_tree(path=%s, flags=0x%lx)",
					tree_path, open_flags));
		api->fd_tree = open_tree(AT_FDCWD, tree_path, open_flags);
		if (api->fd_tree <= 0)
			goto nothing;
	}

	return mnt_context_append_hook(cxt, hs, next_stage, NULL, next_hook);
nothing:
	/* let's assume that fsopen/open_tree() is not supported */
	DBG(HOOK, ul_debugobj(hs, " open fs/tree failed [errno=%d %m]", errno));
	free_hookset_data(cxt, hs);
	return 0;
}

const struct libmnt_hookset hookset_mount =
{
	.name = "__mount",

	.firststage = MNT_STAGE_PREP,
	.firstcall = hook_prepare,

	.deinit = hookset_deinit
};
