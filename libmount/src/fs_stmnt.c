/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * This file is part of libmount from util-linux project.
 *
 * Copyright (C) 2024 Karel Zak <kzak@redhat.com>
 *
 * libmount is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 */
#include "mountP.h"
#include "mount-api-utils.h"

#ifdef HAVE_STATMOUNT_API

/**
 * mnt_fs_enable_statmount:
 * @fs: filesystem instance
 * @enable: 0 or 1
 * @mask: default statmount() mask
 *
 * Enable or disable on demand fetching mount node information by statmount().
 * The setting is possible to reset by mnt_reset_fs().
 *
 * The default behavior of on-demand fetching is to minimize the size of data read
 * from the kernel. For example, mnt_fs_get_fstype() will only read the filesystem
 * type, but nothing else. However, this behavior may introduce unwanted overhead
 * (a large number of syscalls) in some use cases. In such cases, you can define
 * the @mask to force libmount to fetch (and save) more data with one statmount()
 * call.
 *
 * See mnt_fs_fetch_statmount() for details about the @mask.
 *
 * Returns: 0 or negative number in case of error
 *
 * Since: 2.41
 */
int mnt_fs_enable_statmount(struct libmnt_fs *fs, int enable, uint64_t mask)
{
	if (!fs)
		return -EINVAL;

	fs->stmnt_mask = mask;
	fs->stmnt_enabled = enable ? 1 : 0;
	return 0;

}

# define sm_str(_sm, member)    ((_sm)->str + member)

static int apply_statmount(struct libmnt_fs *fs, struct statmount *sm)
{
	int rc = 0, merge = !fs->vfs_optstr || fs->fs_optstr;

	if ((sm->mask & STATMOUNT_FS_TYPE) && !fs->fstype)
		rc = mnt_fs_set_fstype(fs, sm_str(sm, sm->fs_type));

	if (!rc && (sm->mask & STATMOUNT_MNT_POINT) && !fs->target)
		rc = mnt_fs_set_target(fs, sm_str(sm, sm->mnt_point));

	if (!rc && (sm->mask & STATMOUNT_MNT_ROOT) && !fs->root)
		rc = mnt_fs_set_root(fs, sm_str(sm, sm->mnt_root));

	if (!rc && (sm->mask & STATMOUNT_MNT_BASIC)) {
		if (!fs->propagation)
			fs->propagation = sm->mnt_propagation;
		if (!fs->parent)
			fs->parent = sm->mnt_parent_id_old;
		if (!fs->uniq_parent)
			fs->uniq_parent = sm->mnt_parent_id;
		if (!fs->id)
			fs->id = sm->mnt_id_old;
		if (!fs->uniq_id)
			fs->uniq_id = sm->mnt_id;
		if (!fs->vfs_optstr) {
			rc = mnt_optstr_append_option(&fs->vfs_optstr,
					sm->mnt_attr & MOUNT_ATTR_RDONLY ? "ro" : "rw", NULL);
			if (!rc && (sm->mnt_attr & MOUNT_ATTR_NOSUID))
				rc = mnt_optstr_append_option(&fs->vfs_optstr, "nosuid", NULL);
			if (!rc && (sm->mnt_attr & MOUNT_ATTR_NODEV))
				rc = mnt_optstr_append_option(&fs->vfs_optstr, "nodev", NULL);
			if (!rc && (sm->mnt_attr & MOUNT_ATTR_NOEXEC))
				rc = mnt_optstr_append_option(&fs->vfs_optstr, "noexec", NULL);
			if (!rc && (sm->mnt_attr & MOUNT_ATTR_NODIRATIME))
				rc = mnt_optstr_append_option(&fs->vfs_optstr, "nodiratime", NULL);
			if (!rc && (sm->mnt_attr & MOUNT_ATTR_NOSYMFOLLOW))
				rc = mnt_optstr_append_option(&fs->vfs_optstr, "nosymfollow", NULL);

			switch (sm->mnt_attr & MOUNT_ATTR__ATIME) {
			case MOUNT_ATTR_STRICTATIME:
				rc = mnt_optstr_append_option(&fs->vfs_optstr, "strictatime", NULL);
				break;
			case MOUNT_ATTR_NOATIME:
				rc = mnt_optstr_append_option(&fs->vfs_optstr, "noatime", NULL);
				break;
			case MOUNT_ATTR_RELATIME:
				rc = mnt_optstr_append_option(&fs->vfs_optstr, "relatime", NULL);
				break;
			}

		}
	}

	if (!rc && (sm->mask & STATMOUNT_SB_BASIC)) {
		if (!fs->devno)
			fs->devno = makedev(sm->sb_dev_major, sm->sb_dev_minor);
		if (!fs->fs_optstr) {
			rc = mnt_optstr_append_option(&fs->fs_optstr,
					sm->sb_flags & SB_RDONLY ? "ro" : "rw", NULL);
			if (!rc && (sm->sb_flags & SB_SYNCHRONOUS))
				rc = mnt_optstr_append_option(&fs->fs_optstr, "sync", NULL);
			if (!rc && (sm->sb_flags & SB_DIRSYNC))
				rc = mnt_optstr_append_option(&fs->fs_optstr, "dirsync", NULL);
			if (!rc && (sm->sb_flags & SB_LAZYTIME))
				rc = mnt_optstr_append_option(&fs->fs_optstr, "lazytime", NULL);
		}
	}

	if (!rc && merge) {
		free(fs->optstr);

		/* merge VFS and FS options to one string (we do the same in mountinfo parser) */
		fs->optstr = mnt_fs_strdup_options(fs);
	}
	return rc;
}

/**
 * mnt_fs_fetch_statmount:
 * @fs: filesystem instance
 * @mask: extends the default statmount() mask.
 *
 * This function retrieves mount node information from the kernel. The @mask is
 * extended (bitwise-OR) by the mask specified for mnt_fs_enable_statmount() if
 * on-demand fetching is enabled. If the mask is still 0, then a mask is
 * generated for all missing data in @fs.
 *
 * Returns: 0 or negative number in case of error (if @fs is NULL).
 *
 * Since: 2.41
 */
int mnt_fs_fetch_statmount(struct libmnt_fs *fs, uint64_t mask)
{
	int rc = 0, status;
	char buf[BUFSIZ];
	struct statmount *sm = (struct statmount *) buf;

	if (!fs)
		return -EINVAL;

	/* add default mask if on-demand enabled */
	if (fs->stmnt_enabled
	    && fs->stmnt_mask
	    && !(fs->stmnt_done & fs->stmnt_mask))
		mask |= fs->stmnt_mask;

	/* ignore repeated requests */
	if (mask && fs->stmnt_done & mask)
		return 0;

	/* temporary disable statmount() to avoid recursive
	 * mnt_fs_fetch_statmount() from mnt_fs_get...() functions */
	status = fs->stmnt_enabled;
	fs->stmnt_enabled = 0;

	if (!fs->uniq_id) {
		if (!fs->target) {
			rc = -EINVAL;
			goto done;
		}
		rc = mnt_id_from_path(fs->target, &fs->uniq_id, NULL);
		if (rc)
			goto done;
		DBG(FS, ul_debugobj(fs, "uniq-ID=%" PRIu64, fs->uniq_id));
	}


	/* fetch all missing information by default */
	if (!mask) {
		mask = STATMOUNT_SB_BASIC | STATMOUNT_MNT_BASIC;
		if (!fs->fstype)
			mask |= STATMOUNT_FS_TYPE;
		if (!fs->target)
			mask |= STATMOUNT_MNT_POINT;
		if (!fs->root)
			mask |= STATMOUNT_MNT_ROOT;
	}
	if (!mask)
		goto done;

	rc = statmount(fs->uniq_id, mask, sm, sizeof(buf), 0);
	DBG(FS, ul_debugobj(fs, "statmount [rc=%d  mask: %s%s%s%s%s]", rc,
				mask & STATMOUNT_SB_BASIC ? "sb-basic " : "",
				mask & STATMOUNT_MNT_BASIC ? "mnt-basic " : "",
				mask & STATMOUNT_MNT_ROOT ? "mnt-root " : "",
				mask & STATMOUNT_MNT_POINT ? "mnt-point " : "",
				mask & STATMOUNT_FS_TYPE ? "fs-type " : ""));

	if (!rc)
		rc = apply_statmount(fs, sm);
done:
	fs->stmnt_enabled = status;
	fs->stmnt_done |= mask;
	return rc;
}

#else /* HAVE_STATMOUNT_API */

int mnt_fs_enable_statmount(struct libmnt_fs *fs __attribute__((__unused__)),
			    int enable __attribute__((__unused__)),
			    uint64_t mask __attribute__((__unused__)))
{
	return -ENOTSUP;
}

int mnt_fs_fetch_statmount(struct libmnt_fs *fs __attribute__((__unused__)),
			   uint64_t mask __attribute__((__unused__)))
{
	return -ENOTSUP;
}

#endif /* HAVE_STATMOUNT_API */
