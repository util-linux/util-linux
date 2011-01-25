/*
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/wait.h>
#include <sys/mount.h>

#include "c.h"
#include "pathnames.h"
#include "strutils.h"
#include "mountP.h"

static int lookup_umount_fs(struct libmnt_context *cxt)
{
	int rc;
	const char *tgt;
	struct libmnt_table *mtab;
	struct libmnt_fs *fs;

	assert(cxt);
	assert(cxt->fs);

	DBG(CXT, mnt_debug_h(cxt, "umount: lookup FS"));

	tgt = mnt_fs_get_target(cxt->fs);
	if (!tgt) {
		DBG(CXT, mnt_debug_h(cxt, "umount: undefined target"));
		return -EINVAL;
	}
	rc = mnt_context_get_mtab(cxt, &mtab);
	if (rc) {
		DBG(CXT, mnt_debug_h(cxt, "umount: failed to read mtab"));
		return rc;
	}
	fs = mnt_table_find_target(mtab, tgt, MNT_ITER_BACKWARD);
	if (!fs) {
		/* maybe the option is source rather than target (mountpoint) */
		fs = mnt_table_find_source(mtab, tgt, MNT_ITER_BACKWARD);

		if (fs) {
			struct libmnt_fs *fs1 = mnt_table_find_target(mtab,
							mnt_fs_get_target(fs),
							MNT_ITER_BACKWARD);
			if (!fs1) {
				DBG(CXT, mnt_debug_h(cxt, "mtab is broken?!?!"));
				return -EINVAL;
			}
			if (fs != fs1) {
				/* Something was stacked over `file' on the
				 * same mount point. */
				DBG(CXT, mnt_debug_h(cxt,
						"umount: %s: %s is mounted "
						"over it on the same point",
						tgt, mnt_fs_get_source(fs1)));
				return -EINVAL;
			}
		}
	}

	if (!fs) {
		DBG(CXT, mnt_debug_h(cxt, "cannot found %s in mtab", tgt));
		return 0;
	}

	/* copy from mtab/fstab to our FS description
	 */
	rc = mnt_fs_set_source(cxt->fs, mnt_fs_get_source(fs));
	if (!rc)
		rc = mnt_fs_set_target(cxt->fs, mnt_fs_get_target(fs));

	if (!rc && !mnt_fs_get_fstype(cxt->fs))
		rc = mnt_fs_set_fstype(cxt->fs, mnt_fs_get_fstype(fs));

	if (!rc)
		rc = mnt_fs_set_vfs_options(cxt->fs, mnt_fs_get_vfs_options(fs));
	if (!rc)
		rc = mnt_fs_set_fs_options(cxt->fs, mnt_fs_get_fs_options(fs));
	if (!rc)
		rc = mnt_fs_set_user_options(cxt->fs, mnt_fs_get_user_options(fs));

	if (!rc && mnt_fs_get_bindsrc(fs))
		rc = mnt_fs_set_bindsrc(cxt->fs, mnt_fs_get_bindsrc(fs));

	DBG(CXT, mnt_debug_h(cxt, "umount: mtab applied"));
	cxt->flags |= MNT_FL_TAB_APPLIED;
	return rc;
}

/* check if @devname is loopdev and if the device is associated
 * with a source from @fstab_fs
 *
 * TODO : move this to loopdev.c
 */
static int mnt_loopdev_associated_fs(const char *devname, struct libmnt_fs *fs)
{
	uintmax_t offset = 0;
	const char *src;
	char *val, *optstr;
	size_t valsz;

	/* check if it begins with /dev/loop */
	if (strncmp(devname, _PATH_DEV_LOOP, sizeof(_PATH_DEV_LOOP)))
		return 0;

	src = mnt_fs_get_srcpath(fs);
	if (!src)
		return 0;

	/* check for offset option in @fs */
	optstr = (char *) mnt_fs_get_user_options(fs);
	if (optstr && !mnt_optstr_get_option(optstr, "offset=", &val, &valsz)) {
		int rc;

		val = strndup(val, valsz);
		if (!val)
			return 0;
		rc = strtosize(val, &offset);
		free(val);
		if (rc)
			return 0;
	}

	/* TODO:
	 * if (mnt_loopdev_associated_file(devname, src, offset))
	 *	return 1;
	 */
	return 0;
}

/*
 * Note that cxt->fs contains relevant mtab entry!
 */
static int evaluate_permissions(struct libmnt_context *cxt)
{
	struct libmnt_table *fstab;
	unsigned long u_flags = 0;
	const char *tgt, *src, *optstr;
	int rc, ok = 0;
	struct libmnt_fs *fs;

	assert(cxt);
	assert(cxt->fs);
	assert((cxt->flags & MNT_FL_MOUNTFLAGS_MERGED));

	if (!cxt || !cxt->fs)
		return -EINVAL;

	if (!mnt_context_is_restricted(cxt))
		 return 0;		/* superuser mount */

	DBG(CXT, mnt_debug_h(cxt, "umount: evaluating permissions"));

	if (!(cxt->flags & MNT_FL_TAB_APPLIED)) {
		DBG(CXT, mnt_debug_h(cxt,
				"cannot found %s in mtab and you are not root",
				mnt_fs_get_target(cxt->fs)));
		goto eperm;
	}

	if (!(cxt->flags & MNT_FL_NOHELPERS) &&
	     (cxt->user_mountflags & MNT_MS_UHELPER)) {

		char *suffix = NULL;
		char *o = (char *) mnt_fs_get_user_options(cxt->fs);
		size_t valsz;

		rc = mnt_optstr_get_option(o, "uhelper", &suffix, &valsz);
		if (!rc) {
			suffix = strndup(suffix, valsz);
			if (!suffix)
				return -ENOMEM;
			rc = mnt_context_prepare_helper(cxt, "umount", suffix);
		}
		if (rc < 0)
			return rc;
		if (cxt->helper)
			return 0;	/* we'll call /sbin/umount.<uhelper> */
	}

	/*
	 * User mounts has to be in /etc/fstab
	 */
	rc = mnt_context_get_fstab(cxt, &fstab);
	if (rc)
		return rc;

	tgt = mnt_fs_get_target(cxt->fs);
	src = mnt_fs_get_source(cxt->fs);

	if (mnt_fs_get_bindsrc(cxt->fs)) {
		src = mnt_fs_get_bindsrc(cxt->fs);
		DBG(CXT, mnt_debug_h(cxt,
				"umount: using bind source: %s", src));
	}

	/* If fstab contains the two lines
	 *	/dev/sda1 /mnt/zip auto user,noauto  0 0
	 *	/dev/sda4 /mnt/zip auto user,noauto  0 0
	 * then "mount /dev/sda4" followed by "umount /mnt/zip" used to fail.
	 * So, we must not look for file, but for the pair (dev,file) in fstab.
	  */
	fs = mnt_table_find_pair(fstab, src, tgt, MNT_ITER_FORWARD);
	if (!fs) {
		/*
		 * It's possible that there is /path/file.img in fstab and
		 * /dev/loop0 in mtab -- then we have to check releation
		 * between loopdev and the file.
		 */
		fs = mnt_table_find_target(fstab, tgt, MNT_ITER_FORWARD);
		if (fs) {
			const char *dev = mnt_fs_get_srcpath(cxt->fs);		/* devname from mtab */

			if (!dev || !mnt_loopdev_associated_fs(dev, fs))
				fs = NULL;
		}
		if (!fs) {
			DBG(CXT, mnt_debug_h(cxt,
					"umount %s: mtab disagrees with fstab",
					tgt));
			goto eperm;
		}
	}

	/*
	 * User mounting and unmounting is allowed only if fstab contains one
	 * of the options `user', `users' or `owner' or `group'.
	 *
	 * The option `users' allows arbitrary users to mount and unmount -
	 * this may be a security risk.
	 *
	 * The options `user', `owner' and `group' only allow unmounting by the
	 * user that mounted (visible in mtab).
	 */
	optstr = mnt_fs_get_user_options(fs);	/* FSTAB mount options! */
	if (!optstr)
		goto eperm;

	if (mnt_optstr_get_flags(optstr, &u_flags,
				mnt_get_builtin_optmap(MNT_USERSPACE_MAP)))
		goto eperm;

	if (u_flags & MNT_MS_USERS) {
		DBG(CXT, mnt_debug_h(cxt,
			"umount: promiscuous setting ('users') in fstab"));
		return 0;
	}
	/*
	 * Check user=<username> setting from mtab if there is user, owner or
	 * group option in /etc/fstab
	 */
	if ((u_flags & MNT_MS_USER) || (u_flags & MNT_MS_OWNER) ||
	    (u_flags & MNT_MS_GROUP)) {

		char *curr_user = NULL;
		char *mtab_user = NULL;
		size_t sz;

		DBG(CXT, mnt_debug_h(cxt,
				"umount: checking user=<username> from mtab"));

		curr_user = mnt_get_username(getuid());

		if (!curr_user) {
			DBG(CXT, mnt_debug_h(cxt, "umount %s: cannot "
				"convert %d to username", tgt, getuid()));
			goto eperm;
		}

		/* get options from mtab */
		optstr = mnt_fs_get_user_options(cxt->fs);
		if (optstr && !mnt_optstr_get_option((char *) optstr,
					"user", &mtab_user, &sz) && sz)
			ok = !strncmp(curr_user, mtab_user, sz);
	}

	if (ok) {
		DBG(CXT, mnt_debug_h(cxt, "umount %s is allowed", tgt));
		return 0;
	}
eperm:
	DBG(CXT, mnt_debug_h(cxt, "umount is not allowed for you"));
	return -EPERM;
}

static int exec_helper(struct libmnt_context *cxt)
{
	int rc;

	assert(cxt);
	assert(cxt->fs);
	assert(cxt->helper);
	assert((cxt->flags & MNT_FL_MOUNTFLAGS_MERGED));
	assert(cxt->helper_exec_status == 1);

	DBG_FLUSH;

	switch (fork()) {
	case 0:
	{
		const char *args[10], *type;
		int i = 0;

		if (setgid(getgid()) < 0)
			exit(EXIT_FAILURE);

		if (setuid(getuid()) < 0)
			exit(EXIT_FAILURE);

		type = mnt_fs_get_fstype(cxt->fs);

		args[i++] = cxt->helper;			/* 1 */
		args[i++] = mnt_fs_get_target(cxt->fs);		/* 2 */

		if (cxt->flags & MNT_FL_NOMTAB)
			args[i++] = "-n";			/* 3 */
		if (cxt->flags & MNT_FL_LAZY)
			args[i++] = "-l";			/* 4 */
		if (cxt->flags & MNT_FL_FORCE)
			args[i++] = "-f";			/* 5 */
		if (cxt->flags & MNT_FL_VERBOSE)
			args[i++] = "-v";			/* 6 */
		if (cxt->flags & MNT_FL_RDONLY_UMOUNT)
			args[i++] = "-r";			/* 7 */
		if (type && !endswith(cxt->helper, type)) {
			args[i++] = "-t";			/* 8 */
			args[i++] = (char *) type;	/* 9 */
		}

		args[i] = NULL;					/* 10 */
#ifdef CONFIG_LIBMOUNT_DEBUG
		i = 0;
		for (i = 0; args[i]; i++)
			DBG(CXT, mnt_debug_h(cxt, "argv[%d] = \"%s\"",
							i, args[i]));
#endif
		DBG_FLUSH;
		execv(cxt->helper, (char * const *) args);
		exit(EXIT_FAILURE);
	}
	default:
	{
		int st;
		wait(&st);
		cxt->helper_status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;

		DBG(CXT, mnt_debug_h(cxt, "%s executed [status=%d]",
					cxt->helper, cxt->helper_status));
		cxt->helper_exec_status = rc = 0;
		break;
	}

	case -1:
		cxt->helper_exec_status = rc = -errno;
		DBG(CXT, mnt_debug_h(cxt, "fork() failed"));
		break;
	}

	return rc;
}

static int do_umount(struct libmnt_context *cxt)
{
	int rc = 0;
	const char *src, *target;

	assert(cxt);
	assert(cxt->fs);
	assert((cxt->flags & MNT_FL_MOUNTFLAGS_MERGED));
	assert(cxt->syscall_status == 1);

	if (cxt->helper)
		return exec_helper(cxt);

	src = mnt_fs_get_srcpath(cxt->fs);
	target = mnt_fs_get_target(cxt->fs);

	if (!target)
		return -EINVAL;

	if (cxt->flags & MNT_FL_FAKE)
		return 0;

	if (cxt->flags & MNT_FL_LAZY)
		rc = umount2(target, MNT_DETACH);

	else if (cxt->flags & MNT_FL_FORCE) {
		rc = umount2(target, MNT_FORCE);

		if (rc < 0 && errno == ENOSYS)
			rc = umount(target);
	} else
		rc = umount(target);

	if (rc < 0)
		cxt->syscall_status = -errno;
	/*
	 * try remount read-only
	 */
	if (rc < 0 && cxt->syscall_status == -EBUSY &&
	    (cxt->flags & MNT_FL_RDONLY_UMOUNT) && src) {

		cxt->mountflags |= MS_REMOUNT | MS_RDONLY;
		cxt->flags &= ~MNT_FL_LOOPDEL;
		DBG(CXT, mnt_debug_h(cxt, "umount(2) failed [errno=%d] -- "
					"tring remount read-only",
					-cxt->syscall_status));

		rc = mount(src, target, NULL,
			    MS_MGC_VAL | MS_REMOUNT | MS_RDONLY, NULL);
		if (rc < 0) {
			cxt->syscall_status = -errno;
			DBG(CXT, mnt_debug_h(cxt, "read-only re-mount(2) failed "
					"[errno=%d]",
					-cxt->syscall_status));
			return cxt->syscall_status;
		}
		cxt->syscall_status = 0;
		DBG(CXT, mnt_debug_h(cxt, "read-only re-mount(2) success"));
		return 0;
	}

	if (rc < 0) {
		DBG(CXT, mnt_debug_h(cxt, "umount(2) failed [errno=%d]",
					-cxt->syscall_status));
		return -cxt->syscall_status;
	}
	cxt->syscall_status = 0;
	DBG(CXT, mnt_debug_h(cxt, "umount(2) success"));
	return 0;
}

/**
 * mnt_context_do_umount:
 * @cxt: mount context
 *
 * Umount filesystem by umount(2) or fork()+exec(/sbin/umount.type).
 *
 * See also mnt_context_disable_helpers().
 *
 * Returns: 0 on success, and negative number in case of error.
 */
int mnt_context_do_umount(struct libmnt_context *cxt)
{
	int rc;

	if (!cxt || !cxt->fs || (cxt->fs->flags & MNT_FS_SWAP))
		return -EINVAL;
	if (!mnt_fs_get_source(cxt->fs) && !mnt_fs_get_target(cxt->fs))
		return -EINVAL;

	free(cxt->helper);	/* be paranoid */
	cxt->helper = NULL;

	cxt->action = MNT_ACT_UMOUNT;

	rc = lookup_umount_fs(cxt);
	if (!rc)
		rc = mnt_context_merge_mflags(cxt);
	if (!rc)
		rc = evaluate_permissions(cxt);
	if (!rc)
	       rc = mnt_context_prepare_target(cxt);
	if (!rc && !cxt->helper)
		rc = mnt_context_prepare_helper(cxt, "umount", NULL);
/* TODO
	if ((cxt->flags & MNT_FL_LOOPDEL) &&
	    (!mnt_is_loopdev(src) || mnt_loopdev_is_autoclear(src)))
		cxt->flags &= ~MNT_FL_LOOPDEL;
*/
	if (!rc)
		rc = mnt_context_prepare_update(cxt);
	if (rc) {
		DBG(CXT, mnt_debug_h(cxt, "prepared umount failed"));
		return rc;
	}

	rc = do_umount(cxt);
	if (rc)
		return rc;
/* TODO
	if (cxt->flags & MNT_FL_LOOPDEL)
		rc = mnt_loopdev_clean(mnt_fs_get_source(cxt->fs));
*/
	if (cxt->flags & MNT_FL_NOMTAB)
		return rc;

	if ((cxt->flags & MNT_FL_RDONLY_UMOUNT) &&
	    (cxt->mountflags & (MS_RDONLY | MS_REMOUNT))) {
		/*
		 * remount --> read-only mount
		 */
		const char *o = mnt_fs_get_vfs_options(cxt->fs);
		char *n = o ? strdup(o) : NULL;

		DBG(CXT, mnt_debug_h(cxt, "fix remount-on-umount update"));

		if (n)
			mnt_optstr_remove_option(&n, "rw");
		rc = mnt_optstr_prepend_option(&n, "ro", NULL);
		if (!rc)
			rc = mnt_fs_set_vfs_options(cxt->fs, n);

		/* use "remount" instead of "umount" in /etc/mtab */
		if (!rc && cxt->update && cxt->mtab_writable)
			rc = mnt_update_set_fs(cxt->update,
					       cxt->mountflags, NULL, cxt->fs);
	}

	return rc ? : mnt_context_update_tabs(cxt);
}
