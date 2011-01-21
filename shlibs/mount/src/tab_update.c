/*
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: update
 * @title: mtab managment
 * @short_description: userspace mount information management.
 *
 * The struct libmnt_update provides abstraction to manage mount options in userspace independently on
 * system configuration. This low-level API works on system with and without /etc/mtab. On 
 * systems without the regular /etc/mtab file are userspace mount options (e.g. user=)
 * stored to the /dev/.mount/utab file.
 *
 * It's recommended to use high-level struct libmnt_context API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "c.h"
#include "mountP.h"
#include "mangle.h"
#include "pathnames.h"

struct libmnt_update {
	char		*target;
	struct libmnt_fs *fs;
	char		*filename;
	unsigned long	mountflags;
	int		userspace_only;
	int		ready;
};

static int utab_new_entry(struct libmnt_fs *fs, unsigned long mountflags, struct libmnt_fs **ent);
static int set_fs_root(struct libmnt_fs *result, struct libmnt_fs *fs, unsigned long mountflags);

/**
 * mnt_new_update:
 *
 * Returns: newly allocated update handler
 */
struct libmnt_update *mnt_new_update(void)
{
	struct libmnt_update *upd;

	upd = calloc(1, sizeof(*upd));
	if (!upd)
		return NULL;

	DBG(UPDATE, mnt_debug_h(upd, "allocate"));

	return upd;
}

/**
 * mnt_free_update:
 * @upd: update
 *
 * Deallocates struct libmnt_update handler.
 */
void mnt_free_update(struct libmnt_update *upd)
{
	if (!upd)
		return;

	DBG(UPDATE, mnt_debug_h(upd, "free"));

	mnt_free_fs(upd->fs);
	free(upd->target);
	free(upd->filename);
	free(upd);
}

/*
 * Returns 0 on success, 1 if not file available, -1 in case of error.
 */
int mnt_update_set_filename(struct libmnt_update *upd, const char *filename, int userspace_only)
{
	const char *path = NULL;
	int rw = 0;

	assert(upd);

	/* filename explicitly defined */
	if (filename) {
		char *p = strdup(filename);
		if (!p)
			return -ENOMEM;

		upd->userspace_only = userspace_only;
		free(upd->filename);
		upd->filename = p;
	}

	if (upd->filename)
		return 0;

	/* detect tab filename -- /etc/mtab or /dev/.mount/utab
	 */
	mnt_has_regular_mtab(&path, &rw);
	if (!rw) {
		path = NULL;
		mnt_has_regular_utab(&path, &rw);
		if (!rw)
			return 1;
		upd->userspace_only = TRUE;
	}
	upd->filename = strdup(path);
	if (!upd->filename)
		return -ENOMEM;

	return 0;
}

/**
 * mnt_update_get_filename:
 * @upd: update
 *
 * This function returns file name (e.g. /etc/mtab) if the update
 * should be covered by mnt_lock, otherwise returne NULL.
 *
 * Returns: pointer to filename that will be updated or NULL in case of error.
 */
const char *mnt_update_get_filename(struct libmnt_update *upd)
{
	return upd && !upd->userspace_only ? upd->filename : NULL;
}

/**
 * mnt_update_is_ready:
 * @upd: update handler
 *
 * Returns: 1 if entry described by @upd is successfully prepared and will be
 * written to mtab/utab file.
 */
int mnt_update_is_ready(struct libmnt_update *upd)
{
	return upd ? upd->ready : FALSE;
}

/**
 * mnt_update_set_fs:
 * @upd: update handler
 * @mountflags: MS_* flags
 * @target: umount target, must be num for mount
 * @fs: mount filesystem description, must be NULL for umount
 *
 * Returns: -1 in case on error, 0 on success, 1 if update is unnecessary.
 */
int mnt_update_set_fs(struct libmnt_update *upd, unsigned long mountflags,
		      const char *target, struct libmnt_fs *fs)
{
	int rc;

	assert(upd);
	assert(target || fs);

	if (!upd)
		return -EINVAL;
	if ((mountflags & MS_MOVE) && (!fs || !mnt_fs_get_srcpath(fs)))
		return -EINVAL;
	if (target && fs)
		return -EINVAL;

	DBG(UPDATE, mnt_debug_h(upd,
			"reseting FS [fs=0x%p, target=%s, flags=0x%08lx]",
			fs, target, mountflags));
	if (fs) {
		DBG(UPDATE, mnt_debug_h(upd, "FS template:"));
		DBG(UPDATE, mnt_fs_print_debug(fs, stderr));
	}

	mnt_free_fs(upd->fs);
	free(upd->target);
	upd->ready = FALSE;
	upd->fs = NULL;
	upd->target = NULL;
	upd->mountflags = 0;

	if (mountflags & MS_PROPAGATION)
		return 1;

	upd->mountflags = mountflags;

	rc = mnt_update_set_filename(upd, NULL, 0);
	if (rc)
		return rc;	/* error or no file available (rc = 1) */

	if (target) {
		upd->target = strdup(target);
		if (!upd->target)
			return -ENOMEM;

	} else if (fs) {
		if (upd->userspace_only && !(mountflags & MS_MOVE)) {
			int rc = utab_new_entry(fs, mountflags, &upd->fs);
			if (rc)
				return rc;
		} else {
			upd->fs = mnt_copy_mtab_fs(fs);
			if (!upd->fs)
				return -ENOMEM;

		}
	}


	DBG(UPDATE, mnt_debug_h(upd, "ready"));
	upd->ready = TRUE;
	return 0;
}

/**
 * mnt_update_get_fs:
 * @upd: update
 *
 * Returns: update filesystem entry or NULL
 */
struct libmnt_fs *mnt_update_get_fs(struct libmnt_update *upd)
{
	return upd ? upd->fs : NULL;
}

/**
 * mnt_update_get_mflags:
 * @upd: update
 *
 * Returns: mount flags as was set by mnt_update_set_fs()
 */
unsigned long mnt_update_get_mflags(struct libmnt_update *upd)
{
	return upd ? upd->mountflags : 0;
}

/**
 * mnt_update_force_rdonly:
 * @upd: update
 * @rdonly: is read-only?
 *
 * Returns: 0 on success and negative number in case of error.
 */
int mnt_update_force_rdonly(struct libmnt_update *upd, int rdonly)
{
	int rc = 0;

	if (!upd || !upd->fs)
		return -EINVAL;

	if (rdonly && (upd->mountflags & MS_RDONLY))
		return 0;
	if (!rdonly && !(upd->mountflags & MS_RDONLY))
		return 0;

	if (!upd->userspace_only) {
		/* /etc/mtab -- we care about VFS options there */
		const char *o = mnt_fs_get_vfs_options(upd->fs);
		char *n = o ? strdup(o) : NULL;

		if (n)
			mnt_optstr_remove_option(&n, rdonly ? "rw" : "ro");
		if (!mnt_optstr_prepend_option(&n, rdonly ? "ro" : "rw", NULL))
			rc = mnt_fs_set_vfs_options(upd->fs, n);

		free(n);
	}

	if (rdonly)
		upd->mountflags &= ~MS_RDONLY;
	else
		upd->mountflags |= MS_RDONLY;

	return rc;
}

/*
 * Allocates (but does not write) utab entry for mount/remount. This function
 * should be called *before* mount(2) syscall.
 *
 * Returns: 0 on success, negative number on error, 1 if utabs update is
 *          unnecessary.
 */
static int utab_new_entry(struct libmnt_fs *fs, unsigned long mountflags, struct libmnt_fs **ent)
{
	int rc = 0;
	const char *o = NULL, *a = NULL;
	char *u = NULL;

	assert(fs);
	assert(ent);
	assert(!(mountflags & MS_MOVE));

	if (!fs || !ent)
		return -EINVAL;
	*ent = NULL;

	DBG(UPDATE, mnt_debug("prepare utab entry"));

	o = mnt_fs_get_user_options(fs);
	a = mnt_fs_get_attributes(fs);

	if (o) {
		/* remove non-mtab options */
		rc = mnt_optstr_get_options(o, &u,
				mnt_get_builtin_optmap(MNT_USERSPACE_MAP),
				MNT_NOMTAB);
		if (rc)
			goto err;
	}

	if (!u && !a) {
		DBG(UPDATE, mnt_debug("utab entry unnecessary (no options)"));
		return 1;
	}

	/* allocate the entry */
	*ent = mnt_copy_fs(fs);
	if (!*ent) {
		rc = -ENOMEM;
		goto err;
	}

	rc = mnt_fs_set_user_options(*ent, u);
	if (rc)
		goto err;
	rc = mnt_fs_set_attributes(*ent, a);
	if (rc)
		goto err;

	if (!(mountflags & MS_REMOUNT)) {
		rc = set_fs_root(*ent, fs, mountflags);
		if (rc)
			goto err;
	}

	free(u);
	DBG(UPDATE, mnt_debug("utab entry OK"));
	return 0;
err:
	mnt_free_fs(*ent);
	free(u);
	*ent = NULL;
	return rc;
}

static int set_fs_root(struct libmnt_fs *result, struct libmnt_fs *fs, unsigned long mountflags)
{
	char *root = NULL, *mnt = NULL;
	const char *fstype;
	struct libmnt_table *tb = NULL;
	int rc = -ENOMEM;

	assert(fs);
	assert(result);

	DBG(UPDATE, mnt_debug("setting FS root"));

	fstype = mnt_fs_get_fstype(fs);

	/*
	 * bind-mount -- get fs-root and source device for the source filesystem
	 */
	if (mountflags & MS_BIND) {
		const char *src, *src_root;
		struct libmnt_fs *src_fs;

		DBG(UPDATE, mnt_debug("setting FS root: bind"));

		src = mnt_fs_get_srcpath(fs);
		if (src) {
			rc = mnt_fs_set_bindsrc(result, src);
			if (rc)
				goto err;
			mnt = mnt_get_mountpoint(src);
		}
		if (!mnt) {
			rc = -EINVAL;
			goto err;
		}
		root = mnt_get_fs_root(src, mnt);

		tb = __mnt_new_table_from_file(_PATH_PROC_MOUNTINFO, MNT_FMT_MOUNTINFO);
		if (!tb) {
			DBG(UPDATE, mnt_debug("failed to parse mountinfo -- using default"));
			goto dflt;
		}
		src_fs = mnt_table_find_target(tb, mnt, MNT_ITER_BACKWARD);
		if (!src_fs)  {
			DBG(UPDATE, mnt_debug("not found '%s' in mountinfo -- using default", mnt));
			goto dflt;
		}

		/* set device name and fs */
		src = mnt_fs_get_srcpath(src_fs);
		rc = mnt_fs_set_source(result, src);
		if (rc)
			goto err;

		mnt_fs_set_fstype(result, mnt_fs_get_fstype(src_fs));

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

		if (mnt_fs_get_option(fs, "subvol", &vol, &volsz))
			goto dflt;

		DBG(UPDATE, mnt_debug("setting FS root: btrfs subvol"));

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
	mnt_free_table(tb);
	if (!root) {
		root = strdup("/");
		if (!root)
			goto err;
	}
	result->root = root;

	DBG(UPDATE, mnt_debug("FS root result: %s", root));

	free(mnt);
	return 0;
err:
	free(root);
	free(mnt);
	return rc;
}

/* mtab and fstab update */
static int fprintf_mtab_fs(FILE *f, struct libmnt_fs *fs)
{
	char *o;
	char *m1, *m2, *m3, *m4;
	int rc;

	assert(fs);
	assert(f);

	o = mnt_fs_strdup_options(fs);
	if (!o)
		return -ENOMEM;

	m1 = mangle(mnt_fs_get_source(fs));
	m2 = mangle(mnt_fs_get_target(fs));
	m3 = mangle(mnt_fs_get_fstype(fs));
	m4 = mangle(o);

	if (m1 && m2 && m3 && m4)
		rc = !fprintf(f, "%s %s %s %s %d %d\n",
				m1, m2, m3, m4,
				mnt_fs_get_freq(fs),
				mnt_fs_get_passno(fs));
	else
		rc = -ENOMEM;

	free(o);
	free(m1);
	free(m2);
	free(m3);
	free(m4);

	return rc;
}

static int fprintf_utab_fs(FILE *f, struct libmnt_fs *fs)
{
	char *p;

	assert(fs);
	assert(f);

	if (!fs || !f)
		return -EINVAL;

	p = mangle(mnt_fs_get_source(fs));
	if (p) {
		fprintf(f, "SRC=%s ", p);
		free(p);
	}
	p = mangle(mnt_fs_get_target(fs));
	if (p) {
		fprintf(f, "TARGET=%s ", p);
		free(p);
	}
	p = mangle(mnt_fs_get_root(fs));
	if (p) {
		fprintf(f, "ROOT=%s ", p);
		free(p);
	}
	p = mangle(mnt_fs_get_bindsrc(fs));
	if (p) {
		fprintf(f, "BINDSRC=%s ", p);
		free(p);
	}
	p = mangle(mnt_fs_get_attributes(fs));
	if (p) {
		fprintf(f, "ATTRS=%s ", p);
		free(p);
	}
	p = mangle(mnt_fs_get_user_options(fs));
	if (p) {
		fprintf(f, "OPTS=%s", p);
		free(p);
	}
	fputc('\n', f);

	return 0;
}

static int update_table(struct libmnt_update *upd, struct libmnt_table *tb)
{
	FILE *f;
	int rc, fd;
	char *uq = NULL;

	if (!tb || !upd->filename)
		return -EINVAL;

	DBG(UPDATE, mnt_debug_h(upd, "%s: updating", upd->filename));

	fd = mnt_open_uniq_filename(upd->filename, &uq, O_WRONLY);
	if (fd < 0)
		return fd;	/* error */

	f = fdopen(fd, "w");
	if (f) {
		struct stat st;
		struct libmnt_iter itr;
		struct libmnt_fs *fs;
		int fd;

		mnt_reset_iter(&itr, MNT_ITER_FORWARD);
		while(mnt_table_next_fs(tb, &itr, &fs) == 0) {
			if (upd->userspace_only)
				fprintf_utab_fs(f, fs);
			else
				fprintf_mtab_fs(f, fs);
		}
		fd = fileno(f);
		rc = fchmod(fd, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) ? -errno : 0;

		if (!rc && stat(upd->filename, &st) == 0)
			/* Copy uid/gid from the present file before renaming. */
			rc = fchown(fd, st.st_uid, st.st_gid) ? -errno : 0;

		fclose(f);
		rc = rename(uq, upd->filename) ? -errno : 0;
	} else {
		rc = -errno;
		close(fd);
	}

	unlink(uq);	/* be paranoid */
	free(uq);
	return rc;
}

static int utab_lock(const char *filename)
{
	char *lfile;
	int fd;

	assert(filename);

	if (asprintf(&lfile, "%s.lock", filename) == -1)
		return -1;

	DBG(UPDATE, mnt_debug("%s: locking", lfile));

	fd = open(lfile, O_RDONLY|O_CREAT|O_CLOEXEC, S_IWUSR|
			                             S_IRUSR|S_IRGRP|S_IROTH);
	free(lfile);

	if (fd < 0)
		return -errno;
	if (flock(fd, LOCK_EX)) {
		int errsv = errno;
		close(fd);
		return -errsv;
	}
	return fd;
}

static void utab_unlock(int fd)
{
	if (fd >= 0) {
		DBG(UPDATE, mnt_debug("unlocking utab"));
		close(fd);
	}
}

static int update_add_entry(struct libmnt_update *upd, struct libmnt_lock *lc)
{
	FILE *f;
	int rc = 0, u_lc = -1;

	assert(upd);
	assert(upd->fs);

	DBG(UPDATE, mnt_debug_h(upd, "%s: add entry", upd->filename));

	if (lc)
		mnt_lock_file(lc);
	else if (upd->userspace_only)
		u_lc = utab_lock(upd->filename);

	f = fopen(upd->filename, "a+");
	if (f) {
		rc = upd->userspace_only ? fprintf_utab_fs(f, upd->fs) :
					   fprintf_mtab_fs(f, upd->fs);
		DBG(UPDATE, mnt_debug_h(upd, "%s: add [rc=%d]", upd->filename, rc));
		fclose(f);
	} else {
		DBG(UPDATE, mnt_debug_h(upd, "%s: failed: %m", upd->filename));
		rc = -errno;
	}
	if (lc)
		mnt_unlock_file(lc);
	else if (u_lc != -1)
		utab_unlock(u_lc);
	return rc;
}

static int update_remove_entry(struct libmnt_update *upd, struct libmnt_lock *lc)
{
	struct libmnt_table *tb;
	int rc = 0, u_lc = -1;

	assert(upd);
	assert(upd->target);

	DBG(UPDATE, mnt_debug_h(upd, "%s: remove entry", upd->filename));

	if (lc)
		mnt_lock_file(lc);
	else if (upd->userspace_only)
		u_lc = utab_lock(upd->filename);

	tb = __mnt_new_table_from_file(upd->filename,
			upd->userspace_only ? MNT_FMT_UTAB : MNT_FMT_MTAB);
	if (tb) {
		struct libmnt_fs *rem = mnt_table_find_target(tb, upd->target, MNT_ITER_BACKWARD);
		if (rem) {
			mnt_table_remove_fs(tb, rem);
			rc = update_table(upd, tb);
			mnt_free_fs(rem);
		}
		mnt_free_table(tb);
	}
	if (lc)
		mnt_unlock_file(lc);
	else if (u_lc != -1)
		utab_unlock(u_lc);
	return rc;
}

static int update_modify_target(struct libmnt_update *upd, struct libmnt_lock *lc)
{
	struct libmnt_table *tb = NULL;
	int rc = 0, u_lc = -1;

	DBG(UPDATE, mnt_debug_h(upd, "%s: modify target", upd->filename));

	if (lc)
		mnt_lock_file(lc);
	else if (upd->userspace_only)
		u_lc = utab_lock(upd->filename);

	tb = __mnt_new_table_from_file(upd->filename,
			upd->userspace_only ? MNT_FMT_UTAB : MNT_FMT_MTAB);
	if (tb) {
		struct libmnt_fs *cur = mnt_table_find_target(tb,
				mnt_fs_get_srcpath(upd->fs), MNT_ITER_BACKWARD);
		if (cur) {
			rc = mnt_fs_set_target(cur, mnt_fs_get_target(upd->fs));
			if (!rc)
				rc = update_table(upd, tb);
		}
		mnt_free_table(tb);
	}
	if (lc)
		mnt_unlock_file(lc);
	else if (u_lc != -1)
		utab_unlock(u_lc);
	return rc;
}

static int update_modify_options(struct libmnt_update *upd, struct libmnt_lock *lc)
{
	struct libmnt_table *tb = NULL;
	int rc = 0, u_lc = -1;
	struct libmnt_fs *fs;

	assert(upd);
	assert(upd->fs);

	DBG(UPDATE, mnt_debug_h(upd, "%s: modify options", upd->filename));

	fs = upd->fs;

	if (lc)
		mnt_lock_file(lc);
	else if (upd->userspace_only)
		u_lc = utab_lock(upd->filename);

	tb = __mnt_new_table_from_file(upd->filename,
			upd->userspace_only ? MNT_FMT_UTAB : MNT_FMT_MTAB);
	if (tb) {
		struct libmnt_fs *cur = mnt_table_find_target(tb,
					mnt_fs_get_target(fs),
					MNT_ITER_BACKWARD);
		if (cur) {
			if (upd->userspace_only)
				rc = mnt_fs_set_attributes(cur,	mnt_fs_get_attributes(fs));
			if (!rc && !upd->userspace_only)
				rc = mnt_fs_set_vfs_options(cur, mnt_fs_get_vfs_options(fs));
			if (!rc && !upd->userspace_only)
				rc = mnt_fs_set_fs_options(cur, mnt_fs_get_fs_options(fs));
			if (!rc)
				rc = mnt_fs_set_user_options(cur,
						mnt_fs_get_user_options(fs));
			if (!rc)
				rc = update_table(upd, tb);
		}
		mnt_free_table(tb);
	}

	if (lc)
		mnt_unlock_file(lc);
	else if (u_lc != -1)
		utab_unlock(u_lc);
	return rc;
}

/**
 * mnt_update_table:
 * @upd: update
 * @lc: lock
 *
 * High-level API to update /etc/mtab (or private /dev/.mount/utab file).
 *
 * Returns: 0 on success, negative number on error.
 */
int mnt_update_table(struct libmnt_update *upd, struct libmnt_lock *lc)
{
	int rc = -EINVAL;

	assert(upd);

	if (!upd->filename || !upd)
		return -EINVAL;
	if (!upd->ready)
		return 0;

	DBG(UPDATE, mnt_debug_h(upd, "%s: update tab", upd->filename));
	if (upd->fs) {
		DBG(UPDATE, mnt_fs_print_debug(upd->fs, stderr));
	}

	if (!upd->fs && upd->target)
		rc = update_remove_entry(upd, lc);	/* umount */
	else if (upd->mountflags & MS_MOVE)
		rc = update_modify_target(upd, lc);	/* move */
	else if (upd->mountflags & MS_REMOUNT)
		rc = update_modify_options(upd, lc);	/* remount */
	else if (upd->fs)
		rc = update_add_entry(upd, lc);	/* mount */

	upd->ready = FALSE;
	DBG(UPDATE, mnt_debug_h(upd, "%s: update tab: done [rc=%d]",
				upd->filename, rc));
	return rc;
}

#ifdef TEST_PROGRAM

struct libmnt_lock *lock;

static void lock_fallback(void)
{
	if (lock)
		mnt_unlock_file(lock);
}

static int update(const char *target, struct libmnt_fs *fs, unsigned long mountflags)
{
	int rc;
	struct libmnt_update *upd;
	const char *filename;

	DBG(UPDATE, mnt_debug("update test"));

	upd = mnt_new_update();
	if (!upd)
		return -ENOMEM;

	rc = mnt_update_set_fs(upd, mountflags, target, fs);
	if (rc == 1) {
		/* update is unnecessary */
		rc = 0;
		goto done;
	}
	if (rc) {
		fprintf(stderr, "failed to set FS\n");
		goto done;
	}

	/* [... here should be mount(2) call ...]  */

	filename = mnt_update_get_filename(upd);
	if (filename) {
		lock = mnt_new_lock(filename, 0);
		if (lock)
			atexit(lock_fallback);
	}
	rc = mnt_update_table(upd, lock);
done:
	return rc;
}

static int test_add(struct libmnt_test *ts, int argc, char *argv[])
{
	struct libmnt_fs *fs = mnt_new_fs();
	int rc;

	if (argc < 5 || !fs)
		return -1;
	mnt_fs_set_source(fs, argv[1]);
	mnt_fs_set_target(fs, argv[2]);
	mnt_fs_set_fstype(fs, argv[3]);
	mnt_fs_set_options(fs, argv[4]);

	rc = update(NULL, fs, 0);
	mnt_free_fs(fs);
	return rc;
}

static int test_remove(struct libmnt_test *ts, int argc, char *argv[])
{
	if (argc < 2)
		return -1;
	return update(argv[1], NULL, 0);
}

static int test_move(struct libmnt_test *ts, int argc, char *argv[])
{
	struct libmnt_fs *fs = mnt_new_fs();
	int rc;

	if (argc < 3)
		return -1;
	mnt_fs_set_source(fs, argv[1]);
	mnt_fs_set_target(fs, argv[2]);

	rc = update(NULL, fs, MS_MOVE);

	mnt_free_fs(fs);
	return rc;
}

static int test_remount(struct libmnt_test *ts, int argc, char *argv[])
{
	struct libmnt_fs *fs = mnt_new_fs();
	int rc;

	if (argc < 3)
		return -1;
	mnt_fs_set_target(fs, argv[1]);
	mnt_fs_set_options(fs, argv[2]);

	rc = update(NULL, fs, MS_REMOUNT);
	mnt_free_fs(fs);
	return rc;
}

int main(int argc, char *argv[])
{
	struct libmnt_test tss[] = {
	{ "--add",    test_add,     "<src> <target> <type> <options>  add line to mtab" },
	{ "--remove", test_remove,  "<target>                      MS_REMOUNT mtab change" },
	{ "--move",   test_move,    "<old_target>  <target>        MS_MOVE mtab change" },
	{ "--remount",test_remount, "<target>  <options>           MS_REMOUNT mtab change" },
	{ NULL }
	};

	return mnt_run_test(tss, argc, argv);
}

#endif /* TEST_PROGRAM */
