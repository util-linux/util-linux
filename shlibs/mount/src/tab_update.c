/*
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: update
 * @title: mtab (fstab) managment
 * @short_description: userspace mount information management
 *
 * The libmount library allows to use more modes for mtab management:
 *
 * - /etc/mtab is regular file
 *
 *   then libmount manages the file in classical way (all mounts are added to
 *   the file). This mode is always used for /etc/fstab updates as well.
 *
 * - /etc/mtab is symlink
 *
 *   then libmount ignores mtab at all
 *
 * - /etc/mtab is symlink and /var/run/mount/ directory exists
 *
 *   then libmount stores userspace specific mount options to the
 *   /var/run/mount/mountinfo file (the file format compatible to
 *   /proc/self/mountinfo)
 *
 *
 * The mtab is always updated in two steps. The first step is to prepare a new
 * update entry -- mnt_prepare_update(), this step has to be done before
 * mount(2) syscall. The second step is to update the file --
 * mnt_update_file(), this step should be done after mount(2) syscall.
 *
 * The mnt_update_file() behaviour is undefined if mnt_prepare_update() has
 * not been used.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "c.h"
#include "mountP.h"
#include "mangle.h"
#include "pathnames.h"



/*
 * mtab update description
 */
struct _mnt_update {
	int		action;		/* MNT_ACT_{MOUNT,UMOUNT} */
	unsigned long	mountflags;	/* MS_* flags */
	char		*filename;	/* usually /etc/mtab or /var/run/mount/mountinfo */
	char		*old_target;	/* for MS_MOVE */
	int		format;		/* MNT_FMT_{MTAB,FSTAB,MOUNTINFO} */
	int		nolock;		/* don't alloca private mnt_lock */
	mnt_fs		*fs;		/* entry */
	mnt_lock	*lc;		/* lock or NULL */
};

/**
 * mnt_new_update:
 * @action: MNT_ACT_{MOUNT,UMOUNT}
 * @mountflags: MS_{REMOUNT,BIND,MOVE}
 * @fs: FS description
 *
 * Returns: newly allocated update description
 */
mnt_update *mnt_new_update(int action, unsigned long mountflags, const mnt_fs *fs)
{
	mnt_update *upd;

	upd = calloc(1, sizeof(struct _mnt_update));
	if (!upd)
		return NULL;

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: update %p: allocate\n", upd));

	if (action)
		mnt_update_set_action(upd, action);
	if (mountflags)
		mnt_update_set_mountflags(upd, mountflags);
	if (fs)
		mnt_update_set_fs(upd, fs);
	return upd;
}

/**
 * mnt_free_update:
 * @upd: update
 *
 * Deallocates mnt_update handler.
 */
void mnt_free_update(mnt_update *upd)
{
	if (!upd)
		return;

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: update %p: deallacate\n", upd));

	mnt_free_lock(upd->lc);
	free(upd->filename);
	free(upd->old_target);
	free(upd);
}

/**
 * mnt_update_set_filename:
 * @upd: update
 * @filename: path to update (default is /etc/update or /var/run/mount/mountinfo)
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_update_set_filename(mnt_update *upd, const char *filename)
{
	char *p = NULL;

	assert(upd);
	if (!upd)
		return -1;
	if (filename) {
		p = strdup(filename);
		if (!p)
			return -1;
	}
	free(upd->filename);
	upd->filename = p;
	return 0;
}

/**
 * mnt_update_set_action:
 * @upd: update
 * @action: MNT_ACT_{MOUNT,UMOUNT}
 *
 * Overwrites the previously defined (usually by mnt_new_update()) action.
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_update_set_action(mnt_update *upd, int action)
{
	assert(upd);
	if (!upd)
		return -1;
	upd->action = action;
	return 0;
}

/**
 * mnt_update_set_format:
 * @upd: update
 * @format: MNT_FMT_{MTAB,FSTAB,MOUNTINFO}
 *
 * Sets update file format, default is MNT_FMT_MTAB for paths that end with
 * "update", MNT_FMT_MOUNTINFO for paths that end with "mountinfo" and
 * MNT_FMT_FSTAB for paths that end with "fstab".
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_update_set_format(mnt_update *upd, int format)
{
	assert(upd);
	if (!upd)
		return -1;
	upd->format = format;
	return 0;
}

/**
 * mnt_update_set_fs:
 * @upd: update
 * @fs: filesystem to write to file
 *
 * Returns; 0 on success, -1 in case of error.
 */
int mnt_update_set_fs(mnt_update *upd, const mnt_fs *fs)
{
	mnt_fs *x = NULL;

	assert(upd);
	if (!upd)
		return -1;
	if (fs) {
		x = mnt_copy_fs(fs);
		if (!x)
			return -1;
	}

	mnt_free_fs(upd->fs);
	upd->fs = x;
	return 0;
}

/**
 * mnt_update_set_mountflags:
 * @upd: update
 * @flags: MS_{REMOUNT,MOVE,BIND,...}
 *
 * Sets mount flags for mount/umount action. The flags are also
 * extracted from mount options by mnt_prepare_update(). The mount flags
 * are used for mtab update to differentiate between move, remount, ...
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_update_set_mountflags(mnt_update *upd, unsigned long flags)
{
	assert(upd);
	if (!upd)
		return -1;
	upd->mountflags = flags;
	return 0;
}

/**
 * mnt_update_get_lock:
 * @upd: update
 *
 * This function should not be called before mnt_prepare_update(). The lock
 * is initialized when mtab update is required only.
 *
 * Note that after mnt_update_disable_lock(mt, TRUE) or after mnt_free_update()
 * the lock will be automatically deallocated.
 *
 * Returns: libmount lock handler or NULL if locking is disabled.
 */
mnt_lock *mnt_update_get_lock(mnt_update *upd)
{
	return upd ? upd->lc : NULL;
}

/**
 * mnt_update_disable_lock:
 * @upd: update
 * @disable: TRUE/FALSE
 *
 * Enable or disable update locking, the locking is enabled by default.
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_update_disable_lock(mnt_update *upd, int disable)
{
	if (!upd)
		return -1;
	if (disable) {
		mnt_free_lock(upd->lc);
		upd->lc = NULL;
	}
	upd->nolock = disable;
	return 0;
}

/**
 * mnt_update_set_old_target:
 * @upd: update
 * @target: old mountpoint
 *
 * Sets the original target for the MS_MOVE operation.
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_update_set_old_target(mnt_update *upd, const char *target)
{
	char *p = NULL;

	if (!upd)
		return -1;
	if (p) {
		p = strdup(target);
		if (!p)
			return -1;
	}
	free(upd->old_target);
	upd->old_target = p;
	return 0;
}

/*
 * The format is same as /proc/self/mountinfo, but it contains userspace
 * mount options and some unnecessary fields are ignored.
 */
static int fprintf_mountinfo_fs(FILE *f, mnt_fs *fs)
{
	char *root = NULL, *target = NULL, *optstr = NULL,
	     *fstype = NULL, *source = NULL;
	int rc = -1;
	dev_t devno;

	assert(fs);
	assert(f);

	if (!fs || !f)
		return -1;
	devno = mnt_fs_get_devno(fs);
	source = mangle(mnt_fs_get_source(fs));
	root = mangle(mnt_fs_get_root(fs));
	target = mangle(mnt_fs_get_target(fs));
	fstype = mangle(mnt_fs_get_fstype(fs));
	optstr = mangle(mnt_fs_get_optstr(fs));

	if (!root || !target || !optstr)
		goto done;

	rc = fprintf(f, "%i %i %u:%u %s %s %s - %s %s %s\n",
			mnt_fs_get_id(fs),
			mnt_fs_get_parent_id(fs),
			major(devno), minor(devno),
			root,
			target,
			optstr,
			fstype ? fstype : "auto",
			source ? source : "none",
			"none");
	rc = 0;
done:
	free(root);
	free(target);
	free(optstr);
	free(fstype);
	free(source);
	return rc;
}

/* mtab and fstab update */
static int fprintf_mtab_fs(FILE *f, mnt_fs *fs)
{
	char *m1 = NULL, *m2 = NULL, *m3 = NULL, *m4 = NULL;
	int rc = -1;

	assert(fs);
	assert(f);

	if (!fs || !f)
		return -1;

	m1 = mangle(mnt_fs_get_source(fs));
	m2 = mangle(mnt_fs_get_target(fs));
	m3 = mangle(mnt_fs_get_fstype(fs));
	m4 = mangle(mnt_fs_get_optstr(fs));

	if (!m1 || !m2 || !m3 || !m4)
		goto done;

	rc = fprintf(f, "%s %s %s %s %d %d\n",
				m1, m2, m3, m4,
				mnt_fs_get_freq(fs),
				mnt_fs_get_passno(fs));
	rc = 0;
done:
	free(m1);
	free(m2);
	free(m3);
	free(m4);

	return rc;
}

static int update_file(const char *filename, int fmt, mnt_tab *tb)
{
	mnt_iter itr;
	mnt_fs *fs;
	FILE *f = NULL;
	char tmpname[PATH_MAX];
	struct stat st;
	int fd;
	int (*line_fn)(FILE *, mnt_fs *) = fprintf_mountinfo_fs;

	assert(tb);
	if (!tb)
		goto error;

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: update %s: update from tab %p\n", filename, tb));

	if (snprintf(tmpname, sizeof(tmpname), "%s.tmp", filename)
						>= sizeof(tmpname))
		goto error;

	f = fopen(tmpname, "w");
	if (!f)
		goto error;


	if (fmt == MNT_FMT_MTAB || fmt == MNT_FMT_FSTAB)
		line_fn = fprintf_mtab_fs;

	mnt_reset_iter(&itr, MNT_ITER_FORWARD);
	while(mnt_tab_next_fs(tb, &itr, &fs) == 0)
		line_fn(f, fs);

	fd = fileno(f);

	if (fchmod(fd, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) < 0)
		goto error;

	/* Copy uid/gid from the present file before renaming. */
	if (stat(filename, &st) == 0) {
		if (fchown(fd, st.st_uid, st.st_gid) < 0)
			goto error;
	}

	fclose(f);
	f = NULL;

	if (rename(tmpname, filename) < 0)
		goto error;

	return 0;
error:
	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: update %s: update from tab %p failed\n", filename, tb));
	if (f)
		fclose(f);
	return -1;
}

static int set_fs_root(mnt_update *upd, mnt_fs *fs)
{
	char *root = NULL, *mnt = NULL;
	const char *fstype;
	char *optstr;
	mnt_tab *tb = NULL;

	if (upd->mountflags & MS_REMOUNT)
		return 0;

	optstr = (char *) mnt_fs_get_optstr(fs);
	fstype = mnt_fs_get_fstype(fs);

	/*
	 * bind-mount -- get fs-root and source device for the source filesystem
	 */
	if (upd->mountflags & MS_BIND) {
		const char *src, *src_root;
		mnt_fs *src_fs;

		src = mnt_fs_get_srcpath(fs);
		if (!src)
			goto err;
		mnt = mnt_get_mountpoint(src);
		if (!mnt)
			goto err;
		root = mnt_get_fs_root(src, mnt);

		tb = mnt_new_tab_from_file(_PATH_PROC_MOUNTINFO);
		if (!tb)
			goto dflt;
		src_fs = mnt_tab_find_target(tb, mnt, MNT_ITER_BACKWARD);
		if (!src_fs)
			goto dflt;

		/* set device name and fs */
		src = mnt_fs_get_srcpath(src_fs);
		mnt_fs_set_source(fs, src);

		mnt_fs_set_fstype(fs, mnt_fs_get_fstype(src_fs));

		/* on btrfs the subvolume is used as fs-root in
		 * /proc/self/mountinfo, so we have get the original subvolume
		 * name from src_fs and prepend the subvolume name to the
		 * fs-root path
		 */
		src_root = mnt_fs_get_root(src_fs);
		if (src_root && !startswith(root, src_root)) {
			size_t sz = strlen(root) + strlen(src_root) + 1;
			char *tmp = malloc(sz);

			if (!tmp)
				goto err;
			snprintf(tmp, sz, "%s%s", src_root, root);
			free(root);
			root = tmp;
		}
	}

	/*
	 * btrfs-subvolume mount -- get subvolume name and use it as a root-fs path
	 */
	else if (fstype && !strcmp(fstype, "btrfs")) {
		char *vol = NULL, *p;
		size_t sz, volsz = 0;

		if (mnt_optstr_get_option(optstr, "subvol", &vol, &volsz))
			goto dflt;

		sz = volsz;
		if (*vol != '/')
			sz++;
		root = malloc(sz + 1);
		if (!root)
			goto err;
		p = root;
		if (*vol != '/')
			*p++ = '/';
		memcpy(p, vol, volsz);
		*(root + sz) = '\0';
	}

dflt:
	mnt_free_tab(tb);
	if (!root)
		root = strdup("/");
	if (!root)
		goto err;
	fs->root = root;
	free(mnt);
	return 0;
err:
	free(root);
	free(mnt);
	return -1;
}

/**
 * mnt_prepare_update:
 * @upd: update
 *
 * Prepares internal data for mtab update:
 * - set filename if mnt_update_set_filename() wasn't called
 * - set file format if mnt_update_set_format() wasn't called
 * - bitwise-OR mountflags from mount options
 * - for /var/run/mount/mountinfo:
 *   * evaluate if the update is necessary
 *   * set fs root and devname for bind mount and btrfs subvolumes
 *   * removes unnecessary mount options
 * - allocate update_lock if necessary
 *
 * This function has to be always called before mount(2). The mnt_update_file()
 * should not be called if mnt_prepare_update() returns non-zero value.
 *
 * Returns: 0 on success, 1 if update is unnecessary, -1 in case of error
 */
int mnt_prepare_update(mnt_update *upd)
{
	char *u = NULL;
	const char *o = NULL;

	assert(upd);
	assert(upd->fs);

	if (!upd || !upd->fs)
		return -1;

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: update %p: prepare update (target %s, source %s, optstr %s)\n",
		upd, mnt_fs_get_target(upd->fs), mnt_fs_get_source(upd->fs),
		mnt_fs_get_optstr(upd->fs)));

	if (!upd->filename) {
		const char *p = mnt_get_writable_mtab_path();
		if (!p) {
			if (errno)
			       goto err;	/* EACCES? */
			goto nothing;		/* no mtab */
		}
		upd->filename = strdup(p);
		if (!upd->filename)
			goto err;
	}
	if (!upd->format) {
		if (endswith(upd->filename, "mountinfo"))
			upd->format = MNT_FMT_MOUNTINFO;
		else if (endswith(upd->filename, "fstab"))
			upd->format = MNT_FMT_FSTAB;
		else
			upd->format = MNT_FMT_MTAB;
	}

	/* TODO: cannonicalize source and target paths on mnt->fs */

	if (upd->format != MNT_FMT_FSTAB) {
		o = mnt_fs_get_optstr(upd->fs);
		if (o)
			mnt_optstr_get_mountflags(o, &upd->mountflags);
	}

	/* umount */
	if (upd->action == MNT_ACT_UMOUNT)
		return 0;

	/*
	 * A) classic /etc/mtab
	 */
	if (upd->format != MNT_FMT_MOUNTINFO)
		return 0;

	/*
	 * B) /var/run/mount/mountinfo
	 */
	if (upd->mountflags & MS_REMOUNT) {
		/* remount */
		if (mnt_split_optstr(o, &u, NULL, NULL, MNT_NOMTAB, 0))
			goto err;
		if (__mnt_fs_set_optstr(upd->fs, u, FALSE))
			goto err;

	} else {
		if (!o)
			goto nothing;	/* no options */
		if (mnt_split_optstr(o, &u, NULL, NULL, MNT_NOMTAB, 0))
			goto err;
		if (!u)
			goto nothing;	/* no userpsace options */
		if (set_fs_root(upd, upd->fs))
			goto err;
		__mnt_fs_set_optstr(upd->fs, u, FALSE);
	}

	if (!upd->nolock && !upd->lc) {
		upd->lc = mnt_new_lock(upd->filename, 0);
		if (!upd->lc)
			goto err;
	}

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: update %p: prepare update: success\n", upd));
	free(u);
	return 0;
err:
	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: update %p: prepare update: failed\n", upd));
	free(u);
	return -1;
nothing:
	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: update %p: prepare update: unnecessary\n", upd));
	free(u);
	return 1;
}

static int add_entry(mnt_update *upd)
{
	FILE *f;
	int rc = -1;

	assert(upd);

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: update %p: add entry\n", upd));
	if (upd->lc)
		mnt_lock_file(upd->lc);
	f = fopen(upd->filename, "a+");
	if (f) {
		if (upd->format == MNT_FMT_MOUNTINFO)
			rc = fprintf_mountinfo_fs(f, upd->fs);
		else
			rc = fprintf_mtab_fs(f, upd->fs);
		fclose(f);
	}
	if (upd->lc)
		mnt_unlock_file(upd->lc);
	return rc;
}

static int remove_entry(mnt_update *upd)
{
	const char *target;
	mnt_tab *tb = NULL;
	mnt_fs *fs = NULL;
	int rc = -1;

	assert(upd);
	assert(upd->filename);

	target = mnt_fs_get_target(upd->fs);
	assert(target);

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: update %p: remove entry (target %s)\n", upd, target));

	if (upd->lc)
		mnt_lock_file(upd->lc);
	tb = mnt_new_tab_from_file(upd->filename);
	if (!tb)
		goto done;
	fs = mnt_tab_find_target(tb, target, MNT_ITER_BACKWARD);
	if (!fs) {
		rc = 0;	/* no error if the file does not contain the target */
		goto done;
	}
	mnt_tab_remove_fs(tb, fs);

	if (!update_file(upd->filename, upd->format, tb))
		rc = 0;
done:
	if (upd->lc)
		mnt_unlock_file(upd->lc);
	mnt_free_tab(tb);
	mnt_free_fs(fs);
	return rc;
}

static int modify_target(mnt_update *upd)
{
	mnt_tab *tb = NULL;
	mnt_fs *fs = NULL;
	int rc = -1;

	assert(upd);
	assert(upd->old_target);
	assert(upd->filename);
	assert(mnt_fs_get_target(upd->fs));

	if (!upd->old_target)
		return -1;

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: update %p: modify target (%s->%s)\n", upd,
		upd->old_target, mnt_fs_get_target(upd->fs)));

	if (upd->lc)
		mnt_lock_file(upd->lc);
	tb = mnt_new_tab_from_file(upd->filename);
	if (!tb)
		goto done;
	fs = mnt_tab_find_target(tb, upd->old_target, MNT_ITER_BACKWARD);
	if (!fs) {
		rc = 0;	/* no error if the file does not contain the target */
		goto done;
	}

	mnt_fs_set_target(fs, mnt_fs_get_target(upd->fs));

	if (!update_file(upd->filename, upd->format, tb))
		rc = 0;
done:
	if (upd->lc)
		mnt_unlock_file(upd->lc);
	mnt_free_tab(tb);
	return rc;
}

static int modify_options(mnt_update *upd)
{
	mnt_tab *tb = NULL;
	mnt_fs *fs = NULL, *rem_fs = NULL;
	int rc = -1;
	const char *target = mnt_fs_get_target(upd->fs);

	assert(target);
	assert(upd->filename);

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: update %p: modify options (target %s)\n", upd, target));

	if (upd->lc)
		mnt_lock_file(upd->lc);
	tb = mnt_new_tab_from_file(upd->filename);
	if (!tb)
		goto done;
	fs = mnt_tab_find_target(tb, target, MNT_ITER_BACKWARD);
	if (!fs) {
		rc = 0;	/* no error if the file does not contain the target */
		goto done;
	}
	if (upd->format == MNT_FMT_MOUNTINFO && !mnt_fs_get_optstr(upd->fs)) {
		mnt_tab_remove_fs(tb, fs);
		rem_fs = fs;
	} else
		__mnt_fs_set_optstr(fs, mnt_fs_get_optstr(upd->fs), FALSE);

	if (!update_file(upd->filename, upd->format, tb))
		rc = 0;
done:
	if (upd->lc)
		mnt_unlock_file(upd->lc);
	mnt_free_tab(tb);
	mnt_free_fs(rem_fs);
	return rc;
}

/**
 * mnt_update_file:
 * @upd: update
 *
 * Updates the update file. The behavior of this function is undefined if
 * mnt_prepare_update() has not been called.
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_update_file(mnt_update *upd)
{
	assert(upd);
	assert(upd->filename);
	assert(upd->format);
	assert(upd->fs);

	if (!upd || !upd->fs)
		return -1;

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: update %p: update (target %s)\n", upd,
		mnt_fs_get_target(upd->fs)));
	/*
	 * umount
	 */
	if (upd->action == MNT_ACT_UMOUNT)
		return remove_entry(upd);
	/*
	 * mount
	 */
	if (upd->action == MNT_ACT_MOUNT) {
		if (upd->mountflags & MS_REMOUNT)
			return modify_options(upd);

		if (upd->mountflags & MS_MOVE)
			return modify_target(upd);

		return add_entry(upd);	/* mount */
	}
	return -1;
}

#ifdef TEST_PROGRAM

#include <errno.h>

mnt_lock *lock;

static void lock_fallback(void)
{
	if (lock)
		mnt_unlock_file(lock);
}

static int update(mnt_update *upd)
{
	int rc;

	/*
	 * Note that mount(2) syscal should be called *after*
	 * mnt_prepare_update() and *before* mnt_update_file()
	 */
	rc = mnt_prepare_update(upd);
	if (!rc) {
		/* setup lock fallback */
		lock = mnt_update_get_lock(upd);
		atexit(lock_fallback);

		return mnt_update_file(upd);
	}
	if (rc == 1) {
		printf("update: update is not reuquired\n");
		return 0;
	}
	fprintf(stderr, "update: failed to prepare update\n");
	return -1;
}

int test_add(struct mtest *ts, int argc, char *argv[])
{
	mnt_fs *fs = mnt_new_fs();
	mnt_update *upd;
	int rc = -1;

	if (argc < 5 || !fs)
		return -1;
	mnt_fs_set_source(fs, argv[1]);
	mnt_fs_set_target(fs, argv[2]);
	mnt_fs_set_fstype(fs, argv[3]);
	mnt_fs_set_optstr(fs, argv[4]);

	upd = mnt_new_update(MNT_ACT_MOUNT, 0, fs);
	if (!upd)
		return -1;

	rc = update(upd);

	mnt_free_update(upd);
	mnt_free_fs(fs);
	return rc;
}

int test_add_fstab(struct mtest *ts, int argc, char *argv[])
{
	mnt_fs *fs = mnt_new_fs();
	mnt_update *upd;
	int rc = -1;

	if (argc < 7 || !fs)
		return -1;
	mnt_fs_set_source(fs, argv[1]);
	mnt_fs_set_target(fs, argv[2]);
	mnt_fs_set_fstype(fs, argv[3]);
	mnt_fs_set_optstr(fs, argv[4]);
	mnt_fs_set_freq(fs, atoi(argv[5]));
	mnt_fs_set_passno(fs, atoi(argv[6]));

	/* this is tricky -- to add to fstab use "MNT_ACT_MOUNT" */
	upd = mnt_new_update(MNT_ACT_MOUNT, 0, fs);
	if (!upd)
		return -1;

	mnt_update_disable_lock(upd, TRUE);		/* lock is unnecessary */
	mnt_update_set_filename(upd, _PATH_MNTTAB);	/* fstab */
	mnt_update_set_format(upd, MNT_FMT_FSTAB);

	rc = update(upd);

	mnt_free_update(upd);
	mnt_free_fs(fs);
	return rc;
}

int test_remove(struct mtest *ts, int argc, char *argv[])
{
	mnt_fs *fs = mnt_new_fs();
	mnt_update *upd;
	int rc = -1;

	if (argc < 2 || !fs)
		return -1;
	mnt_fs_set_target(fs, argv[1]);

	upd = mnt_new_update(MNT_ACT_UMOUNT, 0, fs);
	if (!upd)
		return -1;

	rc = update(upd);

	mnt_free_update(upd);
	mnt_free_fs(fs);
	return rc;
}

int test_move(struct mtest *ts, int argc, char *argv[])
{
	mnt_fs *fs = mnt_new_fs();
	mnt_update *upd;
	int rc = -1;

	if (argc < 3 || !fs)
		return -1;
	mnt_fs_set_target(fs, argv[2]);

	upd = mnt_new_update(MNT_ACT_MOUNT, MS_MOVE, fs);
	if (!upd)
		return -1;
	mnt_update_set_old_target(upd, argv[1]);

	rc = update(upd);

	mnt_free_update(upd);
	mnt_free_fs(fs);
	return rc;
}

int test_remount(struct mtest *ts, int argc, char *argv[])
{
	mnt_fs *fs = mnt_new_fs();
	mnt_update *upd;
	int rc = -1;

	if (argc < 3 || !fs)
		return -1;

	mnt_fs_set_target(fs, argv[1]);
	mnt_fs_set_optstr(fs, argv[2]);

	upd = mnt_new_update(MNT_ACT_MOUNT, MS_REMOUNT, fs);
	if (!upd)
		return -1;

	rc = update(upd);

	mnt_free_update(upd);
	mnt_free_fs(fs);
	return rc;
}

int main(int argc, char *argv[])
{
	struct mtest tss[] = {
	{ "--add",    test_add,     "<src> <target> <type> <options>  add line to mtab" },
	{ "--remove", test_remove,  "<target>                      MS_REMOUNT mtab change" },
	{ "--move",   test_move,    "<old_target>  <target>        MS_MOVE mtab change" },
	{ "--remount",test_remount, "<target>  <options>           MS_REMOUNT mtab change" },

	{ "--add-fstab", test_add_fstab, "<src> <target> <type> <options> <freq> <passno>  add line to fstab" },

	{ NULL }
	};

	return mnt_run_test(tss, argc, argv);
}

#endif /* TEST_PROGRAM */
