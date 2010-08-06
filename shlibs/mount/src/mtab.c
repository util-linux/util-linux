/*
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: mtab
 * @title: mtab managment
 * @short_description: userspace mount information management
 *
 * The libmount library allows to use more modes for mtab management:
 *
 * - /etc/mtab is regular file
 *
 *   then libmount manages the file in classical way (all mounts are
 *   added to the file)
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
 * mtab entry -- mnt_mtab_prepare_update(), this step has to be done before
 * mount(2) syscall. The second step is to update the mtab file --
 * mnt_update_mtab(), this step should be done after mount(2) syscall.
 *
 * The mnt_update_mtab() behaviour is undefined if mnt_mtab_prepare_update() has
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
struct _mnt_mtab {
	int		action;		/* MNT_ACT_{MOUNT,UMOUNT} */
	unsigned long	mountflags;	/* MS_* flags */
	char		*filename;	/* usually /etc/mtab or /var/run/mount/mountinfo */
	char		*old_target;	/* for MS_MOVE */
	int		format;		/* MNT_FMT_{MTAB,MOUNTINFO} */
	int		nolock;		/* don't alloca private mnt_lock */
	mnt_fs		*fs;		/* entry */
	mnt_lock	*lc;		/* lock or NULL */
};

/**
 * mnt_new_mtab:
 * @action: MNT_ACT_{MOUNT,UMOUNT}
 *
 * Returns: newly allocated mtab description
 */
mnt_mtab *mnt_new_mtab(int action)
{
	mnt_mtab *mt;

	mt = calloc(1, sizeof(struct _mnt_mtab));
	if (!mt)
		return NULL;

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: mtab %p: allocate\n", mt));

	mt->action = action;
	mt->fs = mnt_new_fs();
	if (!mt->fs)
		goto err;
	return mt;
err:
	mnt_free_fs(mt->fs);
	free(mt);

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: mtab: failed to allocate handler\n"));

	return NULL;
}

/**
 * mnt_free_mtab:
 * @mt: mtab
 *
 * Deallocates mnt_mtab handler.
 */
void mnt_free_mtab(mnt_mtab *mt)
{
	if (!mt)
		return;

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: mtab %p: deallacate\n", mt));

	mnt_free_lock(mt->lc);
	free(mt->filename);
	free(mt);
}

/**
 * mnt_mtab_set_filename:
 * @mt: mtab
 * @filename: path to mtab (default is /etc/mtab or /var/run/mount/mountinfo)
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_mtab_set_filename(mnt_mtab *mt, const char *filename)
{
	char *p = NULL;
	assert(mt);

	if (!mt)
		return -1;
	if (filename) {
		p = strdup(filename);
		if (!p)
			return -1;
	}
	free(mt->filename);
	mt->filename = p;
	return 0;
}

/**
 * mnt_mtab_set_action:
 * @mt: mtab
 * @action: MNT_ACT_{MOUNT,UMOUNT}
 *
 * Overwrites the previously defined action by mnt_new_mtab().
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_mtab_set_action(mnt_mtab *mt, int action)
{
	assert(mt);
	if (!mt)
		return -1;
	mt->action = action;
	return 0;
}

/**
 * mnt_mtab_set_format:
 * @mt: mtab
 * @format: MNT_FMT_{MTAB,MOUNTINFO}
 *
 * Sets mtab file format, default is MNT_FMT_MTAB for paths that end with
 * "mtab" and MNT_FMT_MOUNTINFO for paths that end with "mountinfo".
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_mtab_set_format(mnt_mtab *mt, int format)
{
	assert(mt);
	if (!mt)
		return -1;
	mt->format = format;
	return 0;
}

/**
 * mnt_mtab_set_optstr:
 * @mt: mtab
 * @optstr: mount options that will be used for mount(2)
 *
 * Note that mnt_mtab_prepare_update() will remove options that does not belong
 * to mtab.
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_mtab_set_optstr(mnt_mtab *mt, const char *optstr)
{
	if (!mt)
		return -1;
	return mnt_fs_set_optstr(mt->fs, optstr);
}

/**
 * mnt_mtab_set_mountflags:
 * @mt: mtab
 * @flags: MS_{REMOUNT,MOVE}
 *
 * Sets mount flags for mount/umount action. The flags are also
 * extracted from mount options by mnt_mtab_prepare_update().
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_mtab_set_mountflags(mnt_mtab *mt, unsigned long flags)
{
	assert(mt);
	if (!mt)
		return -1;
	mt->mountflags = flags;
	return 0;
}

/**
 * mnt_mtab_get_lock:
 * @mt: mtab
 *
 * This function should not be called before mnt_mtab_prepare_update(). The lock
 * is initialized when mtab update is required only.
 *
 * Note that after mnt_mtab_disable_lock(mt, TRUE) or after mnt_free_mtab()
 * the lock will be automaticaly deallocated.
 *
 * Returns: libmount lock handler or NULL if locking is disabled.
 */
mnt_lock *mnt_mtab_get_lock(mnt_mtab *mt)
{
	return mt ? mt->lc : NULL;
}

/**
 * mnt_mtab_disable_lock:
 * @mt: mtab
 * @disable: TRUE/FALSE
 *
 * Enable or disable mtab locking, the locking is enabled by default.
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_mtab_disable_lock(mnt_mtab *mt, int disable)
{
	if (!mt)
		return -1;
	if (disable) {
		mnt_free_lock(mt->lc);
		mt->lc = NULL;
	}
	mt->nolock = disable;
	return 0;
}

/**
 * mnt_mtab_set_source:
 * @mt: mtab
 * @source: device or directory name
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_mtab_set_source(mnt_mtab *mt, const char *source)
{
	return mt ? mnt_fs_set_source(mt->fs, source) : -1;
}

/**
 * mnt_mtab_set_target:
 * @mt: mtab
 * @target: mountpoint
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_mtab_set_target(mnt_mtab *mt, const char *target)
{
	return mt ? mnt_fs_set_target(mt->fs, target) : -1;
}

/**
 * mnt_mtab_set_old_target:
 * @mt: mtab
 * @target: old mountpoint
 *
 * Sets the original target for the MS_MOVE operation.
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_mtab_set_old_target(mnt_mtab *mt, const char *target)
{
	if (!mt)
		return -1;
	mt->old_target = strdup(target);
	if (!mt->old_target)
		return -1;
	return 0;
}

/**
 * mnt_mtab_set_fstype:
 * @mt: mtab
 * @fstype: filesystem type (e.g. "ext3")
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_mtab_set_fstype(mnt_mtab *mt, const char *fstype)
{
	if (!mt)
		return -1;
	return mnt_fs_set_fstype(mt->fs, fstype);
}


/*
 * The format is same as /proc/self/mountinfo, but it contains userspace
 * mount options and some unncessary fields are ignored.
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
	int (*line_fn)(FILE *, mnt_fs *);

	assert(tb);
	if (!tb)
		goto error;

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: mtab %s: update from tab %p\n", filename, tb));

	if (snprintf(tmpname, sizeof(tmpname), "%s.tmp", filename)
						>= sizeof(tmpname))
		goto error;

	f = fopen(tmpname, "w");
	if (!f)
		goto error;

	line_fn = fmt == MNT_FMT_MTAB ? fprintf_mtab_fs : fprintf_mountinfo_fs;

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
		"libmount: mtab %s: update from tab %p failed\n", filename, tb));
	if (f)
		fclose(f);
	return -1;
}

static int set_fs_root(mnt_mtab *mt, mnt_fs *fs)
{
	char *root = NULL, *mnt = NULL;
	const char *fstype;
	char *optstr;
	mnt_tab *tb = NULL;

	if (mt->mountflags & MS_REMOUNT)
		return 0;

	optstr = (char *) mnt_fs_get_optstr(fs);
	fstype = mnt_fs_get_fstype(fs);

	/*
	 * bind-mount -- get fs-root and source device for the source filesystem
	 */
	if (mt->mountflags & MS_BIND) {
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
 * mnt_mtab_prepare_update:
 * @mt: mtab
 *
 * Prepares internal data for mtab update:
 * - set mtab filename if mnt_mtab_set_filename() was't called
 * - set mtab file format if mnt_mtab_set_format() was't called
 * - (bitwise) OR mountflags from mount options
 * - for /var/run/mount/mountinfo:
 *   * evaluate if mtab update is necessary
 *   * set fs root and devname for bind mount and btrfs subvolumes
 * - allocate mtab_lock if necessary
 *
 * This function has to be always called before mount(2). The mnt_update_mtab()
 * should not be called is mnt_mtab_prepare_update() returns non-zero value.
 *
 * Returns: 0 on success, 1 if update is unncessary, -1 in case of error
 */
int mnt_mtab_prepare_update(mnt_mtab *mt)
{
	char *u = NULL;
	const char *o = NULL;

	assert(mt);

	if (!mt)
		return -1;

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: mtab %p: prepare update (target %s, source %s, optstr %s)\n",
		mt, mnt_fs_get_target(mt->fs), mnt_fs_get_source(mt->fs),
		mnt_fs_get_optstr(mt->fs)));

	if (!mt->filename) {
		const char *p = mnt_get_writable_mtab_path();
		if (!p) {
			if (errno)
			       goto err;	/* EACCES? */
			goto nothing;		/* no mtab */
		}
		mt->filename = strdup(p);
		if (!mt->filename)
			goto err;
	}
	if (!mt->format)
		mt->format = endswith(mt->filename, "mountinfo") ?
					MNT_FMT_MOUNTINFO : MNT_FMT_MTAB;


	/* TODO: cannonicalize source and target paths on mnt->fs */

	o = mnt_fs_get_optstr(mt->fs);
	if (o)
		mnt_optstr_get_mountflags(o, &mt->mountflags);

	/* umount */
	if (mt->action == MNT_ACT_UMOUNT)
		return 0;

	/*
	 * A) classic /etc/mtab
	 */
	if (mt->format != MNT_FMT_MOUNTINFO)
		return 0;

	/*
	 * B) /var/run/mount/mountinfo
	 */
	if (mt->mountflags & MS_REMOUNT) {
		/* remount */
		if (mnt_split_optstr(o, &u, NULL, NULL, MNT_NOMTAB, 0))
			goto err;
		if (mnt_fs_set_optstr(mt->fs, u))
			goto err;

	} else {
		if (!o)
			goto nothing;	/* no options */
		if (mnt_split_optstr(o, &u, NULL, NULL, MNT_NOMTAB, 0))
			goto err;
		if (!u)
			goto nothing;	/* no userpsace options */
		if (set_fs_root(mt, mt->fs))
			goto err;
		mnt_fs_set_optstr(mt->fs, u);
	}

	if (!mt->nolock && !mt->lc) {
		mt->lc = mnt_new_lock(mt->filename, 0);
		if (!mt->lc)
			goto err;
	}

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: mtab %p: prepare update: success\n", mt));
	free(u);
	return 0;
err:
	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: mtab %p: prepare update: failed\n", mt));
	free(u);
	return -1;
nothing:
	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: mtab %p: prepare update: unnecessary\n", mt));
	free(u);
	return 1;
}

static int add_entry(mnt_mtab *mt)
{
	FILE *f;
	int rc = -1;

	assert(mt);

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: mtab %p: add entry\n", mt));
	if (mt->lc)
		mnt_lock_file(mt->lc);
	f = fopen(mt->filename, "a+");
	if (f) {
		if (mt->format == MNT_FMT_MOUNTINFO)
			rc = fprintf_mountinfo_fs(f, mt->fs);
		else
			rc = fprintf_mtab_fs(f, mt->fs);
		fclose(f);
	}
	if (mt->lc)
		mnt_unlock_file(mt->lc);
	return rc;
}

static int remove_entry(mnt_mtab *mt)
{
	const char *target;
	mnt_tab *tb = NULL;
	mnt_fs *fs = NULL;
	int rc = -1;

	assert(mt);
	assert(mt->filename);

	target = mnt_fs_get_target(mt->fs);
	assert(target);

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: mtab %p: remove entry (target %s)\n", mt, target));

	if (mt->lc)
		mnt_lock_file(mt->lc);
	tb = mnt_new_tab_from_file(mt->filename);
	if (!tb)
		goto done;
	fs = mnt_tab_find_target(tb, target, MNT_ITER_BACKWARD);
	if (!fs) {
		rc = 0;	/* no error if the file does not contain the target */
		goto done;
	}
	mnt_tab_remove_fs(tb, fs);

	if (!update_file(mt->filename, mt->format, tb))
		rc = 0;
done:
	if (mt->lc)
		mnt_unlock_file(mt->lc);
	mnt_free_tab(tb);
	mnt_free_fs(fs);
	return rc;
}

static int modify_target(mnt_mtab *mt)
{
	mnt_tab *tb = NULL;
	mnt_fs *fs = NULL;
	int rc = -1;

	assert(mt);
	assert(mt->old_target);
	assert(mt->filename);
	assert(mnt_fs_get_target(mt->fs));

	if (!mt->old_target)
		return -1;

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: mtab %p: modify target (%s->%s)\n", mt,
		mt->old_target, mnt_fs_get_target(mt->fs)));

	if (mt->lc)
		mnt_lock_file(mt->lc);
	tb = mnt_new_tab_from_file(mt->filename);
	if (!tb)
		goto done;
	fs = mnt_tab_find_target(tb, mt->old_target, MNT_ITER_BACKWARD);
	if (!fs) {
		rc = 0;	/* no error if the file does not contain the target */
		goto done;
	}

	mnt_fs_set_target(fs, mnt_fs_get_target(mt->fs));

	if (!update_file(mt->filename, mt->format, tb))
		rc = 0;
done:
	if (mt->lc)
		mnt_unlock_file(mt->lc);
	mnt_free_tab(tb);
	return rc;
}

static int modify_options(mnt_mtab *mt)
{
	mnt_tab *tb = NULL;
	mnt_fs *fs = NULL, *rem_fs = NULL;
	int rc = -1;
	const char *target = mnt_fs_get_target(mt->fs);

	assert(target);
	assert(mt->filename);

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: mtab %p: modify options (target %s)\n", mt, target));

	if (mt->lc)
		mnt_lock_file(mt->lc);
	tb = mnt_new_tab_from_file(mt->filename);
	if (!tb)
		goto done;
	fs = mnt_tab_find_target(tb, target, MNT_ITER_BACKWARD);
	if (!fs) {
		rc = 0;	/* no error if the file does not contain the target */
		goto done;
	}
	if (mt->format == MNT_FMT_MOUNTINFO && !mnt_fs_get_optstr(mt->fs)) {
		mnt_tab_remove_fs(tb, fs);
		rem_fs = fs;
	} else
		mnt_fs_set_optstr(fs, mnt_fs_get_optstr(mt->fs));

	if (!update_file(mt->filename, mt->format, tb))
		rc = 0;
done:
	if (mt->lc)
		mnt_unlock_file(mt->lc);
	mnt_free_tab(tb);
	mnt_free_fs(rem_fs);
	return rc;
}

/**
 * mnt_update_mtab:
 * @mt: mtab
 *
 * Updates the mtab file. The behavior of this function is undefined if
 * mnt_mtab_prepare_update() has not been called.
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_update_mtab(mnt_mtab *mt)
{
	assert(mt);
	assert(mt->filename);
	assert(mt->format);

	if (!mt)
		return -1;

	DBG(DEBUG_MTAB, fprintf(stderr,
		"libmount: mtab %p: update (target %s)\n", mt,
		mnt_fs_get_target(mt->fs)));
	/*
	 * umount
	 */
	if (mt->action == MNT_ACT_UMOUNT)
		return remove_entry(mt);
	/*
	 * mount
	 */
	if (mt->action == MNT_ACT_MOUNT) {
		if (mt->mountflags & MS_REMOUNT)
			return modify_options(mt);

		if (mt->mountflags & MS_MOVE)
			return modify_target(mt);

		return add_entry(mt);	/* mount */
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

static int update(mnt_mtab *mt)
{
	int rc;

	/*
	 * Note that mount(2) syscal should be called *after*
	 * mnt_mtab_prepare_update() and *before* mnt_update_mtab()
	 */
	rc = mnt_mtab_prepare_update(mt);
	if (!rc) {
		/* setup lock fallback */
		lock = mnt_mtab_get_lock(mt);
		atexit(lock_fallback);

		return mnt_update_mtab(mt);
	}
	if (rc == 1) {
		printf("mtab: update is not reuquired\n");
		return 0;
	}
	fprintf(stderr, "mtab: failed to prepare update\n");
	return -1;
}

int test_add(struct mtest *ts, int argc, char *argv[])
{
	mnt_mtab *mt;
	int rc = -1;

	if (argc < 5)
		return -1;
	mt = mnt_new_mtab(MNT_ACT_MOUNT);
	if (!mt)
		return -1;
	mnt_mtab_set_source(mt, argv[1]);
	mnt_mtab_set_target(mt, argv[2]);
	mnt_mtab_set_fstype(mt, argv[3]);
	mnt_mtab_set_optstr(mt, argv[4]);

	rc = update(mt);

	mnt_free_mtab(mt);
	return rc;
}

int test_remove(struct mtest *ts, int argc, char *argv[])
{
	mnt_mtab *mt;
	int rc = -1;

	if (argc < 2)
		return -1;
	mt = mnt_new_mtab(MNT_ACT_UMOUNT);
	if (!mt)
		return -1;
	mnt_mtab_set_target(mt, argv[1]);

	rc = update(mt);

	mnt_free_mtab(mt);
	return rc;
}

int test_move(struct mtest *ts, int argc, char *argv[])
{
	mnt_mtab *mt;
	int rc = -1;

	if (argc < 3)
		return -1;
	mt = mnt_new_mtab(MNT_ACT_MOUNT);
	if (!mt)
		return -1;
	mnt_mtab_set_mountflags(mt, MS_MOVE);
	mnt_mtab_set_old_target(mt, argv[1]);
	mnt_mtab_set_target(mt, argv[2]);

	rc = update(mt);

	mnt_free_mtab(mt);
	return rc;
}

int test_remount(struct mtest *ts, int argc, char *argv[])
{
	mnt_mtab *mt;
	int rc = -1;

	if (argc < 3)
		return -1;
	mt = mnt_new_mtab(MNT_ACT_MOUNT);
	if (!mt)
		return -1;
	mnt_mtab_set_mountflags(mt, MS_REMOUNT);
	mnt_mtab_set_target(mt, argv[1]);
	mnt_mtab_set_optstr(mt, argv[2]);

	rc = update(mt);

	mnt_free_mtab(mt);
	return rc;
}

int main(int argc, char *argv[])
{
	struct mtest tss[] = {
	{ "--add",    test_add,     "<src> <target> <type> <options>  add line to mtab" },
	{ "--remove", test_remove,  "<target>                      MS_REMOUNT mtab change" },
	{ "--move",   test_move,    "<old_target>  <target>        MS_MOVE mtab change" },
	{ "--remount",test_remount, "<target>  <options>           MS_REMOUNT mtab change" },
	{ NULL }
	};

	return mnt_run_test(tss, argc, argv);
}

#endif /* TEST_PROGRAM */
