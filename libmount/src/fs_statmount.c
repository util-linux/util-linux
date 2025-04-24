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

/**
 * SECTION: statmount
 * @title: statmount setting
 * @short_description: Fetches information about mount node from the kernel.
 */
#include "mountP.h"

#include "mangle.h"

/**
 * mnt_new_statmnt:
 *
 * The initial refcount is 1, and needs to be decremented to
 * release the resources of the filesystem.
 *
 * Returns: newly allocated struct libmnt_statmnt.
 */
struct libmnt_statmnt *mnt_new_statmnt(void)
{
#ifdef HAVE_STATMOUNT_API
	struct libmnt_statmnt *sm;

	errno = 0;
	if (ul_statmount(0, 0, 0, NULL, 0, 0) < 0 && errno == ENOSYS) {
		DBG(FS, ul_debug("statmount: unsuppported"));
		return NULL;
	}

	sm = calloc(1, sizeof(*sm));
	if (!sm)
		return NULL;

	sm->refcount = 1;
	DBG(STATMNT, ul_debugobj(sm, "alloc"));
	return sm;
#else
	errno = ENOSYS;
	return NULL;
#endif
}

/**
 * mnt_ref_statmount:
 * @sm: statmount setting
 *
 * Increments reference counter.
 */
void mnt_ref_statmnt(struct libmnt_statmnt *sm)
{
	if (sm) {
		sm->refcount++;
		/*DBG(STATMNT, ul_debugobj(sm, "ref=%d", sm->refcount));*/
	}
}

/**
 * mnt_unref_statmnt:
 * @sm: statmount setting
 *
 * De-increments reference counter, on zero the @sm is automatically
 * deallocated.
 */
void mnt_unref_statmnt(struct libmnt_statmnt *sm)
{
	if (sm) {
		sm->refcount--;
		/*DBG(STATMNT, ul_debugobj(sm, "unref=%d", sm->refcount));*/
		if (sm->refcount <= 0) {
			free(sm->buf);
			free(sm);
		}
	}
}

/**
 * mnt_statmnt_set_mask:
 * @sm: statmount setting
 * @mask: default mask for statmount() or 0
 *
 * Returns: 0 on succees or  or <0 on error.
 */
int mnt_statmnt_set_mask(struct libmnt_statmnt *sm, uint64_t mask)
{
	if (!sm)
		return -EINVAL;
	sm->mask = mask;

	DBG(STATMNT, ul_debugobj(sm, "mask=0x%" PRIx64, sm->mask));
	return 0;
}

/**
 * mnt_statmnt_disable_fetching:
 * @sm: statmount setting
 * @disable: 0 or 1
 *
 * Disable or enable on-demand statmount() in all libmnt_table of libmnt_fs
 * onjects that references this @sm.
 *
 * Returns: current setting (0 or 1) or <0 on error.
 *
 * Since: 2.41
 */
int mnt_statmnt_disable_fetching(struct libmnt_statmnt *sm, int disable)
{
	int old;

	if (!sm)
		return -EINVAL;
	old = sm->disabled;
	sm->disabled = disable ? 1 : 0;

	/*
	DBG(STATMNT, ul_debugobj(sm, "statmount() %s",
				sm->disabled ? "off" : "on"));
	*/
	return old;
}

/**
 * mnt_fs_refer_statmnt:
 * @fs: filesystem
 * @sm: statmount() setting
 *
 * Add a reference to the statmount() setting. This setting can be overwritten
 * if you add @fs into a table that uses a different statmount() setting. See
 * mnt_table_refer_statmnt(). It is recommended to use the statmount() setting
 * on a table level if you do not want to work with complete mount table.
 *
 * The function mnt_reset_fs() removes this reference too.
 *
 * Returns: 0 on success or negative number in case of error.
 *
 * Since: 2.41
 */
int mnt_fs_refer_statmnt(struct libmnt_fs *fs, struct libmnt_statmnt *sm)
{
	if (!fs)
		return -EINVAL;
	if (fs->stmnt == sm)
		return 0;

	mnt_unref_statmnt(fs->stmnt);
	mnt_ref_statmnt(sm);

	fs->stmnt = sm;
	return 0;
}

/**
 * mnt_fs_get_statmnt:
 * @fs: filesystem
 *
 * Returns: pointer to linmnt_statmnt instance used for the filesystem or NULL
 *
 * Since: 2.41
 */
struct libmnt_statmnt *mnt_fs_get_statmnt(struct libmnt_fs *fs)
{
	return fs ? fs->stmnt : NULL;
}

#ifdef HAVE_STATMOUNT_API

static inline const char *sm_str(struct ul_statmount *sm, uint32_t offset)
{
	return sm->str + offset;
}

static int apply_statmount(struct libmnt_fs *fs, struct ul_statmount *sm)
{
	int rc = 0;

	if (!sm || !sm->size || !fs)
		return -EINVAL;

	if ((sm->mask & STATMOUNT_FS_TYPE) && !fs->fstype)
		rc = mnt_fs_set_fstype(fs, sm_str(sm, sm->fs_type));

	if (!rc && (sm->mask & STATMOUNT_MNT_POINT) && !fs->target)
		rc = mnt_fs_set_target(fs, sm_str(sm, sm->mnt_point));

	if (!rc && (sm->mask & STATMOUNT_MNT_ROOT) && !fs->root)
		rc = mnt_fs_set_root(fs, sm_str(sm, sm->mnt_root));

	if (!rc && (sm->mask & STATMOUNT_SB_SOURCE) && !fs->source)
		rc = mnt_fs_set_source(fs, sm_str(sm, sm->sb_source));

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
			free(fs->optstr);
			fs->optstr = NULL;
		}
	}

	if (!rc && (sm->mask & STATMOUNT_MNT_NS_ID) && !fs->ns_id)
		fs->ns_id = sm->mnt_ns_id;

	if (!rc && (sm->mask & STATMOUNT_MNT_OPTS) && !fs->fs_optstr) {
		fs->fs_optstr = unmangle(sm_str(sm, sm->mnt_opts), NULL);
		free(fs->optstr);
		fs->optstr = NULL;
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
			free(fs->optstr);
			fs->optstr = NULL;
		}
	}

	fs->flags |= MNT_FS_KERNEL;

	return rc;
}

/**
 * mnt_fs_fetch_statmount:
 * @fs: filesystem instance
 * @mask: extends the default statmount() mask.
 *
 * This function retrieves mount node information from the kernel and applies it
 * to the @fs. If the @fs is associated with any libmnt_statmnt object (see
 * mnt_fs_refer_statmnt()), then this object is used to reduce the overhead of
 * allocating statmount buffer and to define default statmount() mask.
 *
 * The @mask is extended (bitwise-OR) by the mask specified for
 * mnt_statmnt_set_mask() if on-demand fetching is enabled. If the mask is
 * still 0, then a mask is generated for all missing data in @fs.
 *
 * The default namespace is the current namespace. This default can be
 * overwritten by mnt_fs_set_ns_id(). The namespace ID is also set when @fs
 * has been created by mnt_table_fetch_statmount() or on-demand (see
 * mnt_table_enable_listmount()).
 *
 * Returns: 0 or negative number in case of error (if @fs is NULL).
 *
 * Since: 2.41
 */
int mnt_fs_fetch_statmount(struct libmnt_fs *fs, uint64_t mask)
{
	int rc = 0, status = 0;
	struct ul_statmount *buf = NULL;
	size_t bufsiz = 0;
	uint64_t ns = 0;

	if (!fs)
		return -EINVAL;

	DBG(FS, ul_debugobj(fs, "statmount fetch"));

	/* add default mask if on-demand enabled */
	if (fs->stmnt
	    && !fs->stmnt->disabled
	    && fs->stmnt->mask)
		mask |= fs->stmnt->mask;

	/* call only for missing stuff */
	if (mask && fs->stmnt_done) {
		mask &= ~fs->stmnt_done;	/* remove what is already done */
		if (!mask)
			return 0;
	}

	/* ignore repeated requests */
	if (mask && fs->stmnt_done & mask)
		return 0;

	/* temporary disable statmount() to avoid recursive
	 * mnt_fs_fetch_statmount() from mnt_fs_get...() functions */
	if (fs->stmnt)
		status = mnt_statmnt_disable_fetching(fs->stmnt, 1);

	if (!fs->uniq_id) {
		if (!fs->target) {
			rc = -EINVAL;
			goto done;
		}
		rc = mnt_id_from_path(fs->target, &fs->uniq_id, NULL);
		if (rc)
			goto done;
		DBG(FS, ul_debugobj(fs, " uniq-ID=%" PRIu64, fs->uniq_id));
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
		if (!fs->fs_optstr)
			mask |= STATMOUNT_MNT_OPTS;
		if (!fs->ns_id)
			mask |= STATMOUNT_MNT_NS_ID;
		if (!fs->source)
			mask |= STATMOUNT_SB_SOURCE;
	}

	if (fs->ns_id)
		ns = fs->ns_id;

	if (fs->stmnt) {
		DBG(FS, ul_debugobj(fs, " reuse libmnt_stmnt"));

		/* note that sys_statmount (re)allocates the buffer */
		if (fs->stmnt->buf && fs->stmnt->bufsiz > 0)
			memset(fs->stmnt->buf, 0, fs->stmnt->bufsiz);

		rc = sys_statmount(fs->uniq_id, 0, mask,
				   &fs->stmnt->buf, &fs->stmnt->bufsiz, 0);
		buf = fs->stmnt->buf;
		bufsiz = fs->stmnt->bufsiz;
	} else {
		DBG(FS, ul_debugobj(fs, " use private buffer"));
		rc = sys_statmount(fs->uniq_id, 0, mask, &buf, &bufsiz, 0);
	}
	DBG(FS, ul_debugobj(fs, " statmount [rc=%d bufsiz=%zu ns=%" PRIu64 " mask: %s%s%s%s%s%s%s]",
				rc, bufsiz, ns,
				mask & STATMOUNT_SB_BASIC ? "sb-basic " : "",
				mask & STATMOUNT_MNT_BASIC ? "mnt-basic " : "",
				mask & STATMOUNT_MNT_ROOT ? "mnt-root " : "",
				mask & STATMOUNT_MNT_POINT ? "mnt-point " : "",
				mask & STATMOUNT_FS_TYPE ? "fs-type " : "",
				mask & STATMOUNT_MNT_OPTS ? "mnt-opts " : "",
				mask & STATMOUNT_SB_SOURCE ? "sb-source " : ""));

	if (!rc)
		rc = apply_statmount(fs, buf);
done:

	if (fs->stmnt)
		mnt_statmnt_disable_fetching(fs->stmnt, status);
	else
		free(buf);

	fs->stmnt_done |= mask;
	return rc;
}

#else /* HAVE_STATMOUNT_API */

int mnt_fs_fetch_statmount(struct libmnt_fs *fs __attribute__((__unused__)),
			   uint64_t mask __attribute__((__unused__)))
{
	return -ENOTSUP;
}

#endif /* HAVE_STATMOUNT_API */
