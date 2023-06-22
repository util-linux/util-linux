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
 * This is classic mount(2) based mount.
 *
 * Please, see the comment in libmount/src/hooks.c to understand how hooks work.
 */

#include "mountP.h"
#include "strutils.h"

#include <sys/syscall.h>

/* mount(2) flags for additional propagation changes etc. */
struct hook_data {
	unsigned long flags;
};

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

static struct hook_data *new_hook_data(void)
{
	return calloc(1, sizeof(struct hook_data));
}

/* call mount(2) for propagation flags */
static int hook_propagation(struct libmnt_context *cxt,
			    const struct libmnt_hookset *hs,
			    void *data)
{
	int rc;
	struct hook_data *hd = (struct hook_data *) data;
	unsigned long extra = 0;

	assert(hd);
	assert(cxt);
	assert(cxt->fs);
	assert(cxt->optlist);

	DBG(HOOK, ul_debugobj(hs, " calling mount(2) for propagation: 0x%08lx %s",
				hd->flags,
				hd->flags & MS_REC ? " (recursive)" : ""));

	if (mnt_context_is_fake(cxt)) {
		DBG(CXT, ul_debugobj(cxt, "  FAKE (-f)"));
		mnt_context_save_success(cxt);
		return 0;
	}

	/*
	 * hd->flags are propagation flags as set in prepare_propagation()
	 *
	 * @cxt contains global mount flags, may be modified after preparation
	 * stage (for example when libmount blindly tries FS type then it uses
	 * MS_SILENT)
	 */
	if (mnt_optlist_is_silent(cxt->optlist))
		extra |= MS_SILENT;

	rc = mount("none", mnt_fs_get_target(cxt->fs), NULL,
			hd->flags | extra, NULL);

	if (rc) {
		/* Update global syscall status if only this function called */
		if (mnt_context_propagation_only(cxt))
			mnt_context_save_failure(cxt, hs, SYS_mount, errno, 0, NULL);

		DBG(HOOK, ul_debugobj(hs, "  mount(2) failed [errno=%d %m]", errno));
		rc = -MNT_ERR_APPLYFLAGS;
	}
	return rc;
}

/*
 * add additional mount(2) syscalls to set propagation flags after regular mount(2).
 */
static int prepare_propagation(struct libmnt_context *cxt,
				const struct libmnt_hookset *hs)
{
	struct libmnt_optlist *ol;
	struct libmnt_iter itr;
	struct libmnt_opt *opt;

	assert(cxt);
	assert(cxt->fs);

	ol = mnt_context_get_optlist(cxt);
	if (!ol)
		return -ENOMEM;

	mnt_reset_iter(&itr, MNT_ITER_FORWARD);

	while (mnt_optlist_next_opt(ol, &itr, &opt) == 0) {
		int rc;
		struct hook_data *data;
		const struct libmnt_optmap *map = mnt_opt_get_map(opt);
		const struct libmnt_optmap *ent = mnt_opt_get_mapent(opt);

		if (!map || map != cxt->map_linux)
			continue;
		if (!(ent->id & MS_PROPAGATION))
			continue;

		data = new_hook_data();
		if (!data)
			return -ENOMEM;

		data->flags = ent->id;

		DBG(HOOK, ul_debugobj(hs, " adding mount(2) call for %s", ent->name));
		rc = mnt_context_append_hook(cxt, hs,
					MNT_STAGE_MOUNT_POST,
					data,
					hook_propagation);
		if (rc)
			return rc;

		DBG(HOOK, ul_debugobj(hs, " removing '%s' flag from primary mount(2)", ent->name));
		mnt_optlist_remove_opt(ol, opt);
	}

	return 0;
}

/* call mount(2) for bind,remount */
static int hook_bindremount(struct libmnt_context *cxt,
			    const struct libmnt_hookset *hs, void *data)
{
	int rc;
	struct hook_data *hd = (struct hook_data *) data;
	unsigned long extra = 0;

	DBG(HOOK, ul_debugobj(hs, " mount(2) for bind-remount: 0x%08lx %s",
				hd->flags,
				hd->flags & MS_REC ? " (recursive)" : ""));

	if (mnt_context_is_fake(cxt)) {
		DBG(CXT, ul_debugobj(cxt, "  FAKE (-f)"));
		mnt_context_save_success(cxt);
		return 0;
	}

	if (mnt_optlist_is_silent(cxt->optlist))
		extra |= MS_SILENT;

	/* for the flags see comment in hook_propagation() */
	rc = mount("none", mnt_fs_get_target(cxt->fs), NULL,
			hd->flags | extra, NULL);

	if (rc)
		DBG(HOOK, ul_debugobj(hs, "  mount(2) failed"
				  " [rc=%d errno=%d %m]", rc, errno));
	return rc;
}

/*
 * add additional mount(2) syscall request to implement "bind,<flags>", the first regular
 * mount(2) is the "bind" operation, the second is "remount,bind,<flags>" call.
 */
static int prepare_bindremount(struct libmnt_context *cxt,
				const struct libmnt_hookset *hs)
{
	struct hook_data *data;
	int rc;

	assert(cxt);

	DBG(HOOK, ul_debugobj(hs, " adding mount(2) call for bint-remount"));

	data = new_hook_data();
	if (!data)
		return -ENOMEM;

	mnt_context_get_mflags(cxt, &data->flags);

	assert(data->flags & MS_BIND);
	assert(!(data->flags & MS_REMOUNT));

	data->flags |= (MS_REMOUNT | MS_BIND);

	rc = mnt_context_append_hook(cxt, hs,
				MNT_STAGE_MOUNT_POST, data, hook_bindremount);
	return rc;
}




/* call mount(2) for regular FS mount, mount flags and options are read from
 * library context struct. There are no private hook data.
 */
static int hook_mount(struct libmnt_context *cxt,
		      const struct libmnt_hookset *hs,
		      void *data __attribute__((__unused__)))
{
	int rc = 0;
	unsigned long flags = 0;
	struct libmnt_optlist *ol = NULL;
	const char *src, *target, *type, *options = NULL;

	src = mnt_fs_get_srcpath(cxt->fs);
	target = mnt_fs_get_target(cxt->fs);
	type = mnt_fs_get_fstype(cxt->fs);

	ol = mnt_context_get_optlist(cxt);
	if (!ol)
		return -ENOMEM;
	if (!target)
		return -EINVAL;
	if (!src)
		src = "none";

	/* FS specific mount options/data */
	if (cxt->flags & MNT_FL_MOUNTDATA)
		options = cxt->mountdata;
	else
		rc = mnt_optlist_get_optstr(ol, &options,
				NULL, MNT_OL_FLTR_UNKNOWN);
	/* mount flags */
	if (!rc)
		rc = mnt_optlist_get_flags(ol, &flags,
				mnt_get_builtin_optmap(MNT_LINUX_MAP), 0);
	if (rc)
		return rc;

	DBG(HOOK, ul_debugobj(hs, "  mount(2) "
		"[source=%s, target=%s, type=%s, flags=0x%08lx, options=%s]",
		src, target, type, flags,
		options ? (cxt->flags & MNT_FL_MOUNTDATA) ? "binary" :
			  options : "<none>"));

	if (mnt_context_is_fake(cxt)) {
		DBG(HOOK, ul_debugobj(hs, " FAKE (-f)"));
		mnt_context_save_success(cxt);
		return 0;
	}

	if (mount(src, target, type, flags, options)) {
		mnt_context_save_failure(cxt, hs, SYS_mount, errno, 0, NULL);
		DBG(HOOK, ul_debugobj(hs, "  mount(2) failed [errno=%d %m]",
					-cxt->syscall_status));
		rc = -cxt->syscall_status;
		return rc;
	}

	mnt_context_save_success(cxt);
	return rc;
}

/*
 * analyze library context and register hooks to call one or more mount(2) syscalls
 */
static int hook_prepare(struct libmnt_context *cxt,
			const struct libmnt_hookset *hs,
			void *data __attribute__((__unused__)))
{
	int rc = 0;
	unsigned long flags = 0;

	assert(cxt);
	assert(hs == &hookset_mount_legacy);

#ifdef USE_LIBMOUNT_MOUNTFD_SUPPORT
	/* do nothing when a new __mount succesfully registred */
	if (mnt_context_has_hook(cxt, &hookset_mount, 0, NULL))
		return 0;
#endif
	/* append regular FS mount(2) */
	if (!mnt_context_propagation_only(cxt) && !cxt->helper)
		rc = mnt_context_append_hook(cxt, hs,
				MNT_STAGE_MOUNT, NULL, hook_mount);

	if (!rc)
		rc = mnt_context_get_mflags(cxt, &flags);
	if (rc)
		return rc;

	/* add extra mount(2) calls for each propagation flag  */
	if (flags & MS_PROPAGATION) {
		rc = prepare_propagation(cxt, hs);
		if (rc)
			return rc;
	}

	/* add extra mount(2) call to implement "bind,remount,<flags>" */
	if ((flags & MS_BIND)
	    && (flags & MNT_BIND_SETTABLE)
	    && !(flags & MS_REMOUNT)) {
		rc = prepare_bindremount(cxt, hs);
		if (rc)
			return rc;
	}

	return rc;
}

/* mount(2) syscall errors */
static int hookset_mkerrmsg(
                        struct libmnt_context *cxt,
                        const struct libmnt_hookset *hs,
                        char *buf, size_t bufsz)
{
	struct stat st;
	unsigned long uflags = 0, mflags = 0;
	int er = mnt_context_get_syscall_errno(cxt);
	const char *tgt = mnt_context_get_target(cxt);
	const char *src = mnt_context_get_source(cxt);

	DBG(HOOK, ul_debugobj(hs, "calling mkerrmsg"));

	switch(er) {
	case EPERM:
		if (!buf)
			break;
		if (geteuid() == 0) {
			if (mnt_safe_stat(tgt, &st) || !S_ISDIR(st.st_mode))
				snprintf(buf, bufsz, _("mount point is not a directory"));
			else
				snprintf(buf, bufsz, _("permission denied"));
		} else
			snprintf(buf, bufsz, _("must be superuser to use mount"));
		break;

	case EBUSY:
		if (!buf)
			break;
		mnt_context_get_mflags(cxt, &mflags);
		if (mflags & MS_REMOUNT) {
			snprintf(buf, bufsz, _("mount point is busy"));
			break;
		}
		if (src) {
			struct libmnt_fs *fs = mnt_context_get_already_mounted(cxt);

			if (fs && mnt_fs_get_target(fs))
				snprintf(buf, bufsz, _("%s already mounted on %s"),
						src, mnt_fs_get_target(fs));
		}
		if (!*buf)
			snprintf(buf, bufsz, _("%s already mounted or mount point busy"), src);
		break;
	case ENOENT:
		if (tgt && mnt_safe_lstat(tgt, &st)) {
			if (buf)
				snprintf(buf, bufsz, _("mount point does not exist"));
		} else if (tgt && mnt_safe_stat(tgt, &st)) {
			if (buf)
				snprintf(buf, bufsz, _("mount point is a symbolic link to nowhere"));
		} else if (src && !mnt_is_path(src)) {
			mnt_context_get_user_mflags(cxt, &uflags);
			if (uflags & MNT_MS_NOFAIL)
				return MNT_EX_SUCCESS;
			if (buf)
				snprintf(buf, bufsz, _("special device %s does not exist"), src);
		} else
			goto generic_error;
		break;

	case ENOTDIR:
		if (mnt_safe_stat(tgt, &st) || ! S_ISDIR(st.st_mode)) {
			if (buf)
				snprintf(buf, bufsz, _("mount point is not a directory"));
		} else if (src && !mnt_is_path(src)) {
			mnt_context_get_user_mflags(cxt, &uflags);
			if (uflags & MNT_MS_NOFAIL)
				return MNT_EX_SUCCESS;
			if (buf)
				snprintf(buf, bufsz, _("special device %s does not exist "
					 "(a path prefix is not a directory)"), src);
		} else
			goto generic_error;
		break;

	case EINVAL:
		if (!buf)
			break;
		mnt_context_get_mflags(cxt, &mflags);
		if (mflags & MS_REMOUNT)
			snprintf(buf, bufsz, _("mount point not mounted or bad option"));
		else if ((mflags & MS_MOVE) && mnt_is_shared_tree(cxt, src))
			snprintf(buf, bufsz,
				_("bad option; moving a mount "
				  "residing under a shared mount is unsupported"));
		else if (mnt_fs_is_netfs(mnt_context_get_fs(cxt)))
			snprintf(buf, bufsz,
				_("bad option; for several filesystems (e.g. nfs, cifs) "
				  "you might need a /sbin/mount.<type> helper program"));
		else
			snprintf(buf, bufsz,
				_("wrong fs type, bad option, bad superblock on %s, "
				  "missing codepage or helper program, or other error"),
				src);
		break;

	case EMFILE:
		if (buf)
			snprintf(buf, bufsz, _("mount table full"));
		break;

	case EIO:
		if (buf)
			snprintf(buf, bufsz, _("can't read superblock on %s"), src);
		break;

	case ENODEV:
		if (!buf)
			break;
		if (mnt_context_get_fstype(cxt))
			snprintf(buf, bufsz, _("unknown filesystem type '%s'"),
					mnt_context_get_fstype(cxt));
		else
			snprintf(buf, bufsz, _("unknown filesystem type"));
		break;

	case ENOTBLK:
		mnt_context_get_user_mflags(cxt, &uflags);
		if (uflags & MNT_MS_NOFAIL)
			return MNT_EX_SUCCESS;
		if (!buf)
			break;
		if (src && mnt_safe_stat(src, &st))
			snprintf(buf, bufsz, _("%s is not a block device, and stat(2) fails?"), src);
		else if (src && S_ISBLK(st.st_mode))
			snprintf(buf, bufsz,
				_("the kernel does not recognize %s as a block device; "
				  "maybe \"modprobe driver\" is necessary"), src);
		else if (src && S_ISREG(st.st_mode))
			snprintf(buf, bufsz, _("%s is not a block device; try \"-o loop\""), src);
		else
			snprintf(buf, bufsz, _("%s is not a block device"), src);
		break;

	case ENXIO:
		mnt_context_get_user_mflags(cxt, &uflags);
		if (uflags & MNT_MS_NOFAIL)
			return MNT_EX_SUCCESS;
		if (buf)
			snprintf(buf, bufsz, _("%s is not a valid block device"), src);
		break;

	case EACCES:
	case EROFS:
		if (!buf)
			break;
		mnt_context_get_mflags(cxt, &mflags);
		if (mflags & MS_RDONLY)
			snprintf(buf, bufsz, _("cannot mount %s read-only"), src);
		else if (mnt_context_is_rwonly_mount(cxt))
			snprintf(buf, bufsz, _("%s is write-protected but explicit read-write mode requested"), src);
		else if (mflags & MS_REMOUNT)
			snprintf(buf, bufsz, _("cannot remount %s read-write, is write-protected"), src);
		else if (mflags & MS_BIND)
			snprintf(buf, bufsz, _("bind %s failed"), src);
		else
			goto generic_error;
		break;

	case ENOMEDIUM:
		mnt_context_get_user_mflags(cxt, &uflags);
		if (uflags & MNT_MS_NOFAIL)
			return MNT_EX_SUCCESS;
		if (buf)
			snprintf(buf, bufsz, _("no medium found on %s"), src);
		break;

	case EBADMSG:
		/* Bad CRC for classic filesystems (e.g. extN or XFS) */
		if (buf && src && mnt_safe_stat(src, &st) == 0
		    && (S_ISBLK(st.st_mode) || S_ISREG(st.st_mode))) {
			snprintf(buf, bufsz, _("cannot mount; probably corrupted filesystem on %s"), src);
			break;
		}
		/* fallthrough */

	default:
	generic_error:
		if (buf)
			errsnprint(buf, bufsz, er, _("mount failed: %m"));
		break;
	}

	return MNT_EX_FAIL;
}

const struct libmnt_hookset hookset_mount_legacy =
{
	.name = "__legacy-mount",

	.firststage = MNT_STAGE_PREP,
	.firstcall = hook_prepare,

	.mkerrmsg = hookset_mkerrmsg,

	.deinit = hookset_deinit
};
