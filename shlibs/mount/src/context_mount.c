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

#ifdef HAVE_LIBSELINUX
#include <selinux/selinux.h>
#include <selinux/context.h>
#endif

#include <sys/wait.h>
#include <sys/mount.h>

#include "c.h"
#include "mountP.h"

/*
 * this has to be called after mnt_context_evaluate_permissions()
 */
static int fix_optstr(mnt_context *cxt)
{
	int rc = 0, rem_se = 0;
	char *next, **optstr;
	char *name, *val;
	size_t namesz, valsz;

	if (!cxt)
		return -EINVAL;
	if (!cxt->fs)
		return 0;

	assert((cxt->flags & MNT_FL_MOUNTFLAGS_MERGED));

	/*
	 * we directly work with optstr pointer here
	 */
	optstr = &cxt->fs->optstr;
	if (!optstr)
		return 0;

	/* The propagation flags should not be used together with any other flags */
	if (cxt->mountflags & MS_PROPAGATION)
		cxt->mountflags &= MS_PROPAGATION;

	if (!mnt_optstr_get_option(*optstr, "user", &val, &valsz)) {
		if (val) {
			cxt->orig_user = strndup(val, valsz);
			if (!cxt->orig_user) {
				rc = -ENOMEM;
				goto done;
			}
		}
		cxt->flags |= MNT_FL_SAVED_USER;
	}

	/*
	 * Sync mount options with mount flags
	 */
	rc = mnt_optstr_apply_flags(optstr, cxt->mountflags,
				mnt_get_builtin_optmap(MNT_LINUX_MAP));
	if (rc)
		goto done;

	rc = mnt_optstr_apply_flags(optstr, cxt->user_mountflags,
				mnt_get_builtin_optmap(MNT_USERSPACE_MAP));
	if (rc)
		goto done;

	next = *optstr;

#ifdef HAVE_LIBSELINUX
	rem_se = (cxt->mountflags & MS_REMOUNT) || !is_selinux_enabled();
#endif
	DBG(CXT, mnt_debug_h(cxt, "fixing mount options: '%s'", *optstr));

	while (!mnt_optstr_next_option(&next, &name, &namesz, &val, &valsz)) {

		if (namesz == 3 && !strncmp(name, "uid", 3))
			rc = mnt_optstr_fix_uid(optstr, val, valsz, &next);
		else if (namesz == 3 && !strncmp(name, "gid", 3))
			rc = mnt_optstr_fix_gid(optstr, val, valsz, &next);
#ifdef HAVE_LIBSELINUX
		else if (namesz >= 7 && (!strncmp(name, "context", 7) ||
					 !strncmp(name, "fscontext", 9) ||
					 !strncmp(name, "defcontext", 10) ||
					 !strncmp(name, "rootcontext", 11))) {
			if (rem_se) {
				/* remove context= option */
				next = name;
				rc = mnt_optstr_remove_option_at(optstr,
							name, val + valsz);
			} else
				rc = mnt_optstr_fix_secontext(optstr,
							val, valsz, &next);
		}
#endif
		else if (namesz == 4 && (cxt->user_mountflags && MNT_MS_USER) &&
			 !strncmp(name, "user", 4)) {

			rc = mnt_optstr_fix_user(optstr,
						 val ? val : name + namesz,
						 valsz, &next);
		}
		if (rc)
			goto done;
	}

done:
	__mnt_fs_set_optstr_ptr(cxt->fs, *optstr, TRUE);
	DBG(CXT, mnt_debug_h(cxt, "fixed options [rc=%d]: '%s'", rc, *optstr));
	return rc;
}

/*
 * Converts already evalulated and fixed options to the form that is comaptible
 * with /sbin/mount.<type> helpers.
 *
 * Retursn newly allocated string.
 */
static int generate_helper_optstr(mnt_context *cxt, char **optstr)
{
	const char *o;
	int rc = 0;

	assert(cxt);
	assert(cxt->fs);
	assert(optstr);

	*optstr = NULL;
	o = mnt_fs_get_optstr(cxt->fs);

	if (o)
		rc = mnt_optstr_append_option(optstr, o, NULL);
	if (!rc && (cxt->flags & MNT_FL_SAVED_USER))
		rc = mnt_optstr_set_option(optstr, "user", cxt->orig_user);
	if (rc) {
		free(*optstr);
		*optstr = NULL;
	}
	return rc;
}


/*
 * this has to be called before fix_optstr()
 */
static int evaluate_permissions(mnt_context *cxt)
{
	unsigned long u_flags;
	const char *srcpath;

	if (!cxt)
		return -EINVAL;
	if (!cxt->fs)
		return 0;

	mnt_context_get_userspace_mountflags(cxt, &u_flags);

	if (!mnt_context_is_restricted(cxt)) {
		/*
		 * superuser mount
		 */
		cxt->user_mountflags &= ~MNT_MS_OWNER;
		cxt->user_mountflags &= ~MNT_MS_GROUP;
		cxt->user_mountflags &= ~MNT_MS_USER;
		cxt->user_mountflags &= ~MNT_MS_USERS;
	} else {
		/*
		 * user mount
		 */
		if (u_flags & (MNT_MS_OWNER | MNT_MS_GROUP))
			cxt->mountflags |= MS_OWNERSECURE;

		if (u_flags & (MNT_MS_USER | MNT_MS_USERS))
			cxt->mountflags |= MS_SECURE;

		srcpath = mnt_fs_get_srcpath(cxt->fs);
		if (!srcpath)
			return -EINVAL;

		/*
		 * MS_OWNER: Allow owners to mount when fstab contains the
		 * owner option.  Note that this should never be used in a high
		 * security environment, but may be useful to give people at
		 * the console the possibility of mounting a floppy.  MS_GROUP:
		 * Allow members of device group to mount. (Martin Dickopp)
		 */
		if (u_flags & (MNT_MS_OWNER | MNT_MS_GROUP)) {
			struct stat sb;

			if (strncmp(srcpath, "/dev/", 5) == 0 &&
			    stat(srcpath, &sb) == 0 &&
			    (((u_flags & MNT_MS_OWNER) && getuid() == sb.st_uid) ||
			     ((u_flags & MNT_MS_GROUP) && mnt_in_group(sb.st_gid))))

				cxt->user_mountflags |= MNT_MS_USER;
		}

		if (!(cxt->user_mountflags & (MNT_MS_USER | MNT_MS_USERS))) {
			DBG(CXT, mnt_debug_h(cxt, "permissions evaluation ends with -EPERMS"));
			return -EPERM;
		}
	}

	return 0;
}

static int merge_mountflags(mnt_context *cxt)
{
	unsigned long fl = 0;
	int rc;

	rc = mnt_context_get_mountflags(cxt, &fl);
	if (rc)
		return rc;
	cxt->mountflags = fl;

	fl = 0;
	rc = mnt_context_get_userspace_mountflags(cxt, &fl);
	if (rc)
		return rc;
	cxt->user_mountflags = fl;

	cxt->flags |= MNT_FL_MOUNTFLAGS_MERGED;
	return 0;
}

static int exec_helper(mnt_context *cxt)
{
	char *o = NULL;
	int rc;

	assert(cxt);
	assert(cxt->fs);
	assert(cxt->helper);

	rc = generate_helper_optstr(cxt, &o);
	if (rc)
		goto done;

	DBG_FLUSH;

	switch (fork()) {
	case 0:
	{
		const char *args[12], *type;
		int i = 0;

		if (setgid(getgid()) < 0)
			exit(EXIT_FAILURE);

		if (setuid(getuid()) < 0)
			exit(EXIT_FAILURE);

		type = mnt_fs_get_fstype(cxt->fs);

		args[i++] = cxt->helper;			/* 1 */
		args[i++] = mnt_fs_get_srcpath(cxt->fs);	/* 2 */
		args[i++] = mnt_fs_get_target(cxt->fs);	/* 3 */

		if (cxt->flags & MNT_FL_SLOPPY)
			args[i++] = "-s";		/* 4 */
		if (cxt->flags & MNT_FL_FAKE)
			args[i++] = "-f";		/* 5 */
		if (cxt->flags & MNT_FL_NOMTAB)
			args[i++] = "-n";		/* 6 */
		if (cxt->flags & MNT_FL_VERBOSE)
			args[i++] = "-v";		/* 7 */
		if (o) {
			args[i++] = "-o";		/* 8 */
			args[i++] = o;			/* 9 */
		}
		if (type && !endswith(cxt->helper, type)) {
			args[i++] = "-t";		/* 10 */
			args[i++] = type;		/* 11 */
		}
		args[i] = NULL;				/* 12 */
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
		rc = 0;
		break;
	}

	case -1:
		rc = -errno;
		DBG(CXT, mnt_debug_h(cxt, "fork() failed"));
		break;
	}

done:
	free(o);
	return rc;
}

/*
 * The default is to use fstype from cxt->fs, this could be overwritten by
 * @try_type argument.
 */
static int do_mount(mnt_context *cxt, const char *try_type)
{
	int rc;
	const char *src, *target, *type;
	unsigned long flags;

	assert(cxt);
	assert(cxt->fs);

	if (try_type && !cxt->helper) {
		rc = mnt_context_prepare_helper(cxt, "mount", try_type);
		if (!rc)
			return rc;
	}
	if (cxt->helper)
		return exec_helper(cxt);

	type = try_type ? : mnt_fs_get_fstype(cxt->fs);

	flags = cxt->mountflags;
	src = mnt_fs_get_srcpath(cxt->fs);
	target = mnt_fs_get_target(cxt->fs);

	if (!src || !target)
		return -EINVAL;

	if (!(flags & MS_MGC_MSK))
		flags |= MS_MGC_VAL;

	DBG(CXT, mnt_debug_h(cxt, "calling mount(2) "
			"[source=%s, target=%s, type=%s, "
			" mountflags=%08lx, mountdata=%s]",
			src, target, type,
			flags, cxt->mountdata ? "yes" : "<none>"));

	if (mount(src, target, type, flags, cxt->mountdata)) {
		cxt->syscall_errno = errno;
		DBG(CXT, mnt_debug_h(cxt, "mount(2) failed [errno=%d]",
						cxt->syscall_errno));
		return -cxt->syscall_errno;
	}

	DBG(CXT, mnt_debug_h(cxt, "mount(2) success"));
	return 0;
}

/**
 * mnt_context_prepare_mount:
 * @cxt: mount context
 *
 * This function:
 *	- read information from fstab (if necessary)
 *	- cleanup mount options
 *	- check premissions
 *	- prepare device (e.g. loop device)
 *	- detect FS type (if necessary)
 *	- generate mount flags and mount data (if not set yet)
 *	- prepare for mtab update (if necessary)
 *
 * It's strongly recommended to use this function before mnt_context_mount_fs().
 *
 * Returns: 0 on success, and negative number in case of error.
 */
int mnt_context_prepare_mount(mnt_context *cxt)
{
	int rc = 0;

	if (!cxt)
		return -EINVAL;

	if (!cxt->fs || (!mnt_fs_get_source(cxt->fs) &&
			 !mnt_fs_get_target(cxt->fs)))
		return -EINVAL;

	rc = mnt_context_apply_fstab(cxt);
	if (!rc)
		rc = merge_mountflags(cxt);
	if (!rc)
		rc = evaluate_permissions(cxt);
	if (!rc)
		rc = fix_optstr(cxt);
	if (!rc)
		rc = mnt_context_prepare_srcpath(cxt);
	if (!rc)
		rc = mnt_context_guess_fstype(cxt);
	if (!rc)
		rc = mnt_context_prepare_helper(cxt, "mount", NULL);
	if (!rc)
		rc = mnt_context_prepare_update(cxt, MNT_ACT_MOUNT);

	if (!rc) {
		DBG(CXT, mnt_debug_h(cxt, "sucessfully prepared"));
		return 0;
	}

	DBG(CXT, mnt_debug_h(cxt, "prepare failed"));
	return rc;
}

/**
 * mnt_context_do_mount:
 * @cxt: mount context
 *
 * Mount filesystem by mount(2) or fork()+exec(/sbin/mount.<type>).
 *
 * See also mnt_context_disable_helpers().
 *
 * Returns: 0 on success, and negative number in case of error.
 */
int mnt_context_do_mount(mnt_context *cxt)
{
	int rc = -EINVAL;
	const char *type;

	if (!cxt || !cxt->fs || (cxt->fs->flags & MNT_FS_SWAP))
		return -EINVAL;

	if (!(cxt->flags & MNT_FL_MOUNTDATA))
		cxt->mountdata = (char *) mnt_fs_get_fs_optstr(cxt->fs);

	type = mnt_fs_get_fstype(cxt->fs);

	if (type && !strchr(type, ',')) {
		rc = do_mount(cxt, NULL);
		if (rc)
			return rc;
	}

	/* TODO: try all filesystems from comma separated list of types */

	/* TODO: try all filesystems from /proc/filesystems and /etc/filesystems */

	return rc;
}

/**
 * mnt_context_post_mount:
 * @cxt: mount context
 *
 * Updates mtab, etc. This function should be always called after
 * mnt_context_do_mount().
 *
 * Returns: 0 on success, and negative number in case of error.
 */
int mnt_context_post_mount(mnt_context *cxt)
{
	int rc = 0;

	if (!cxt)
		return -EINVAL;
	/*
	 * Update /etc/mtab or /var/run/mount/mountinfo
	 */
	if (!cxt->syscall_errno && !cxt->helper &&
	    !(cxt->flags & MNT_FL_NOMTAB) &&
	    cxt->update && !mnt_update_is_pointless(cxt->update)) {

		/* TODO: if mtab update is expected then checkif the target is really
		 *       mounted read-write to avoid 'ro' in mtab and 'rw' in /proc/mounts.
		 */
		rc = mnt_update_file(cxt->update);
	}

	return rc;
}

/**
 * mnt_context_mount_strerror
 * @cxt: mount context
 * @buf: buffer
 * @bufsiz: size of the buffer
 *
 * Returns: 0 or negative number in case of error.
 */
int mnt_context_mount_strerror(mnt_context *cxt, char *buf, size_t bufsiz)
{
	/* TODO: based on cxt->syscall_errno or cxt->helper_status */
	return 0;
}

#ifdef TEST_PROGRAM

mnt_lock *lock;

static void lock_fallback(void)
{
	if (lock) {
		mnt_unlock_file(lock);
		mnt_free_lock(lock);
	}
}

int test_mount(struct mtest *ts, int argc, char *argv[])
{
	int idx = 1, rc = 0;
	mnt_context *cxt;

	if (argc < 2)
		return -EINVAL;

	cxt = mnt_new_context();
	if (!cxt)
		return -ENOMEM;

	if (!strcmp(argv[idx], "-o")) {
		mnt_context_set_optstr(cxt, argv[idx + 1]);
		idx += 2;
	}
	if (!strcmp(argv[idx], "-t")) {
		/* TODO: use mnt_context_set_fstype_pattern() */
		mnt_context_set_fstype(cxt, argv[idx + 1]);
		idx += 2;
	}

	if (argc == idx + 1)
		/* mount <mountpont>|<device> */
		mnt_context_set_target(cxt, argv[idx++]);

	else if (argc == idx + 2) {
		/* mount <device> <mountpoint> */
		mnt_context_set_source(cxt, argv[idx++]);
		mnt_context_set_target(cxt, argv[idx++]);
	}

	rc = mnt_context_prepare_mount(cxt);
	if (rc)
		printf("failed to prepare mount\n");
	else {
		lock = mnt_context_get_lock(cxt);
		if (lock)
			atexit(lock_fallback);

		rc = mnt_context_do_mount(cxt);
		if (rc)
			printf("failed to mount\n");
		else {
			printf("successfully mounted");
			rc = mnt_context_post_mount(cxt);
			if (rc)
				printf("mtab update failed\n");
		}
	}

	mnt_free_context(cxt);
	return rc;
}

int main(int argc, char *argv[])
{
	struct mtest tss[] = {
	{ "--mount",    test_mount,        "[-o <opts>] [-t <type>] <spec> | <src> <target>" },
	{ NULL }
	};

	return mnt_run_test(tss, argc, argv);
}

#endif /* TEST_PROGRAM */
