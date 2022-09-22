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

#define set_syscall_status(_cxt, _name, _x) __extension__ ({ \
		if (!(_x)) { \
			(_cxt)->syscall_status = -errno; \
			(_cxt)->syscall_name = (_name); \
		} else \
			(_cxt)->syscall_status = 0; \
	})


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

static int hook_reconfigure_mount(struct libmnt_context *cxt __attribute__((__unused__)),
			const struct libmnt_hookset *hs __attribute__((__unused__)),
			void *data __attribute__((__unused__)))
{
	return 0;
}

static int hook_create_mount(struct libmnt_context *cxt __attribute__((__unused__)),
			const struct libmnt_hookset *hs __attribute__((__unused__)),
			void *data __attribute__((__unused__)))
{
	return 0;
}

static int hook_set_vfsflags(struct libmnt_context *cxt,
			const struct libmnt_hookset *hs,
			void *data __attribute__((__unused__)))
{
	struct libmnt_sysapi *api;
	struct libmnt_optlist *ol;
	struct mount_attr attr = { .attr_clr = 0 };
	unsigned int callflags = AT_EMPTY_PATH;
	uint64_t set = 0, clr = 0;
	int rc;

	api = get_sysapi(cxt, hs);
	assert(api);
	assert(api->fd_tree >= 0);

	ol = mnt_context_get_optlist(cxt);
	if (!ol)
		return -ENOMEM;

	rc = mnt_optlist_get_attrs(ol, &set, &clr);
	if (rc)
		return rc;

	if (mnt_optlist_is_recursive(ol))
		callflags |= AT_RECURSIVE;

	DBG(HOOK, ul_debugobj(hs,
			"mount_setattr(set=0x%08" PRIx64" clr=0x%08" PRIx64")", set, clr));
	attr.attr_set = set;
	attr.attr_clr = clr;

	rc = mount_setattr(api->fd_tree, "", callflags, &attr, sizeof(attr));
	set_syscall_status(cxt, "move_setattr", rc == 0);

	return rc == 0 ? 0 : -errno;
}

static int hook_set_propagation(struct libmnt_context *cxt __attribute__((__unused__)),
			const struct libmnt_hookset *hs __attribute__((__unused__)),
			void *data __attribute__((__unused__)))
{
	return 0;
}

static int hook_attach_target(struct libmnt_context *cxt,
		const struct libmnt_hookset *hs,
		void *data __attribute__((__unused__)))
{
	struct libmnt_sysapi *api;
	const char *target;
	int rc;

	target = mnt_fs_get_target(cxt->fs);
	if (!target)
		return -EINVAL;

	api = get_sysapi(cxt, hs);
	assert(api);
	assert(api->fd_tree >= 0);

	DBG(HOOK, ul_debugobj(hs, "move_mount(to=%s)", target));

	rc = move_mount(api->fd_tree, "", AT_FDCWD, target, MOVE_MOUNT_F_EMPTY_PATH);
	set_syscall_status(cxt, "move_mount", rc == 0);

	return rc == 0 ? 0 : -errno;
}

/*
 * open_tree() and fsopen()
 */
static int init_sysapi(struct libmnt_context *cxt,
		       const struct libmnt_hookset *hs,
		       unsigned long flags)
{
	struct libmnt_sysapi *api;
	const char *path = NULL;

	assert(cxt);
	assert(hs);

	/* A) tree based operation -- the tree is mount source */
	if ((flags & MS_BIND)
	    || (flags & MS_MOVE)) {
		path = mnt_fs_get_srcpath(cxt->fs);
		if (!path)
			return -EINVAL;

	/* B) tree based operation -- the tree is mount point */
	} else if ((flags & MS_REMOUNT)
	    || mnt_context_propagation_only(cxt)) {
		path = mnt_fs_get_target(cxt->fs);
		if (!path)
			return -EINVAL;
	}

	api = new_hookset_data(cxt, hs);
	if (!api)
		return -ENOMEM;

	if (path) {
		unsigned long oflg = OPEN_TREE_CLOEXEC;

		if (mnt_optlist_is_recursive(cxt->optlist))
			oflg |= AT_RECURSIVE;

		if (flags & MS_BIND)
			oflg |= OPEN_TREE_CLONE;

		DBG(HOOK, ul_debugobj(hs, "open_tree(%s)", path));
		if (mnt_context_is_fake(cxt))
			goto fake;

		api->fd_tree = open_tree(AT_FDCWD, path, oflg);
		set_syscall_status(cxt, "open_tree", api->fd_tree >= 0);
		if (api->fd_tree <= 0)
			goto fail;

	/* C) FS based operation */
	} else {
		const char *type = mnt_fs_get_fstype(cxt->fs);

		if (!type)
			return -EINVAL;
		if (mnt_context_is_fake(cxt))
			goto fake;

		DBG(HOOK, ul_debugobj(hs, "fsopen(%s)", type));
		if (mnt_context_is_fake(cxt))
			goto fake;

		api->fd_fs = fsopen(type, FSOPEN_CLOEXEC);

		set_syscall_status(cxt, "fsopen", api->fd_fs >= 0);
		if (api->fd_fs < 0)
			goto fail;
	}

	return 0;
fail:
	DBG(HOOK, ul_debugobj(hs, "init fs/tree failed [errno=%d %m]", errno));
	return -errno;
fake:
	DBG(CXT, ul_debugobj(cxt, " FAKE (-f)"));
	cxt->syscall_status = 0;
	return 0;
}

/*
 * Analyze library context and register hook to call mount-like syscalls.
 *
 * Note that this function interprets classic MS_* flags by new Linux mount FD
 * based API.
 */
static int hook_prepare(struct libmnt_context *cxt,
			const struct libmnt_hookset *hs,
			void *data __attribute__((__unused__)))
{
	struct libmnt_optlist *ol;
	unsigned long flags = 0;
	uint64_t set = 0, clr = 0;
	int rc = 0;

	assert(cxt);
	assert(hs == &hookset_mount);

	DBG(HOOK, ul_debugobj(hs, "prepare mount"));

	ol = mnt_context_get_optlist(cxt);
	if (!ol)
		return -ENOMEM;

	/* classic MS_* flags (include oprations like MS_REMOUNT, etc) */
	rc = mnt_optlist_get_flags(ol, &flags, cxt->map_linux, 0);

	/* MOUNT_ATTR_* flags for mount_setattr() */
	if (!rc)
		rc = mnt_optlist_get_attrs(ol, &set, &clr);

	/* open_tree() or fsopen() */
	if (!rc)
		rc = init_sysapi(cxt, hs, flags);

	/* check mutually exclusive operations */
	if (!rc && (flags & MS_BIND) && (flags & MS_MOVE))
		return -EINVAL;
	if (!rc && (flags & MS_MOVE) && (flags & MS_REMOUNT))
		return -EINVAL;

	/* classic remount (note -oremount,bind,ro is handled as bind) */
	if (!rc && (flags & MS_REMOUNT) && !(flags & MS_BIND))
		rc = mnt_context_append_hook(cxt, hs, MNT_STAGE_MOUNT, NULL,
					hook_reconfigure_mount);

	/* call mount_setattr() */
	if (!rc && (set != 0 || clr != 0))
		rc = mnt_context_append_hook(cxt, hs, MNT_STAGE_MOUNT, NULL,
					hook_set_vfsflags);

	/* create a new FS instance */
	if (!rc
	    && !(flags & MS_BIND)
	    && !(flags & MS_MOVE)
	    && !(flags & MS_REMOUNT)
	    && !mnt_optlist_is_propagation_only(ol))
		rc = mnt_context_append_hook(cxt, hs, MNT_STAGE_MOUNT, NULL,
					hook_create_mount);

	/* call move_mount() to attach target */
	if (!rc
	    && !(flags & MS_REMOUNT)
	    && !mnt_context_propagation_only(cxt))
		rc = mnt_context_append_hook(cxt, hs, MNT_STAGE_MOUNT_POST, NULL,
					hook_attach_target);

	/* set propagation (has to be attached to VFS) */
	if (!rc && mnt_optlist_get_propagation(ol) != 0)
		rc = mnt_context_append_hook(cxt, hs, MNT_STAGE_MOUNT_POST, NULL,
					hook_set_propagation);

	DBG(HOOK, ul_debugobj(hs, "prepare mount done [rc=%d]", rc));
	return rc;
}

const struct libmnt_hookset hookset_mount =
{
	.name = "__mount",

	.firststage = MNT_STAGE_PREP,
	.firstcall = hook_prepare,

	.deinit = hookset_deinit
};
