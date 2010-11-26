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

struct _mnt_update {
	char		*target;
	mnt_fs		*fs;
	unsigned long	mountflags;
	int		userspace_only;
	int		ready;
};

static int utab_new_entry(mnt_fs *fs, unsigned long mountflags, mnt_fs **ent);
static int set_fs_root(mnt_fs *result, mnt_fs *fs, unsigned long mountflags);

/**
 * mnt_new_update:
 * @userspace_only: TRUE/FALSE -- maintain userspace mount options only
 *
 * Returns: newly allocated update handler
 */
mnt_update *mnt_new_update(int userspace_only)
{
	mnt_update *upd;

	upd = calloc(1, sizeof(struct _mnt_update));
	if (!upd)
		return NULL;

	upd->userspace_only = userspace_only;
	DBG(UPDATE, mnt_debug_h(upd, "allocate"));

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

	DBG(UPDATE, mnt_debug_h(upd, "free"));

	mnt_free_fs(upd->fs);
	free(upd->target);
	free(upd);
}

/**
 * mnt_update_is_ready:
 * @upd: update handler
 *
 * Returns: 1 if entry described by @upd is successfully prepared and will be
 * written to mtab/utab file.
 */
int mnt_update_is_ready(mnt_update *upd)
{
	return upd ? upd->ready : FALSE;
}

/**
 * mnt_update_is_userspace_only:
 * @upd: update handler
 *
 * Returns: 1 if @upd cares about userspace mount options only (see
 *          mnt_new_update().
 */
int mnt_update_is_userspace_only(mnt_update *upd)
{
	return upd ? upd->userspace_only : FALSE;
}


/**
 * mnt_update_set_fs:
 * @upd: update handler
 * @mountflags: MS_* flags
 * @target: umount target or MS_MOVE source
 * @fs: mount or MS_REMOUNT filesystem description, or MS_MOVE target
 *
 * Returns: -1 in case on error, 0 on success, 1 if update is unnecessary.
 */
int mnt_update_set_fs(mnt_update *upd, int mountflags,
		      const char *target, mnt_fs *fs)
{
	assert(upd);
	assert(target || fs);

	if (!upd)
		return -EINVAL;

	DBG(UPDATE, mnt_debug_h(upd,
			"reseting FS [fs=0x%p, target=%s, flags=0x%08x]",
			fs, target, mountflags));

	mnt_free_fs(upd->fs);
	free(upd->target);
	upd->ready = FALSE;
	upd->fs = NULL;
	upd->target = NULL;
	upd->mountflags = mountflags;

	if (fs) {
		if (upd->userspace_only && !(mountflags & MS_MOVE)) {
			int rc = utab_new_entry(fs, mountflags, &upd->fs);
			if (rc)
				return rc;
		} else {
			upd->fs = mnt_copy_fs(fs);
			if (!upd->fs)
				return -ENOMEM;
		}
	}

	if (target) {
		upd->target = strdup(target);
		if (!upd->target)
			return -ENOMEM;
	}

	DBG(UPDATE, mnt_debug_h(upd, "ready"));
	upd->ready = TRUE;
	return 0;
}

/*
 * Returns update filesystem or NULL
 */
mnt_fs *mnt_update_get_fs(mnt_update *upd)
{
	return upd ? upd->fs : NULL;
}

/*
 * Allocates (but does not write) utab entry for mount/remount. This function
 * should be called *before* mount(2) syscall.
 *
 * Returns: 0 on success, negative number on error, 1 if utabs update is
 *          unnecessary.
 */
static int utab_new_entry(mnt_fs *fs, unsigned long mountflags, mnt_fs **ent)
{
	int rc = 0;
	const char *o = NULL;
	char *u = NULL;

	assert(fs);
	assert(ent);
	assert(!(mountflags & MS_MOVE));

	if (!fs || !ent)
		return -EINVAL;
	*ent = NULL;

	DBG(UPDATE, mnt_debug("prepare utab entry"));

	o = mnt_fs_get_optstr(fs);
	if (o) {
		rc = mnt_split_optstr(o, &u, NULL, NULL, MNT_NOMTAB, 0);
		if (rc)
			return rc;	/* parse error or so... */
	}
	/* TODO
	if (extra_opts) {
		rc = mnt_optstr_append_option(&u, extra_opts, NULL);
		if (rc)
			goto err;
	}
	*/
	if (!u)
		return 1;	/* don't have mount options */

	/* allocate the entry */
	*ent = mnt_copy_fs(fs);
	if (!*ent) {
		rc = -ENOMEM;
		goto err;
	}

	rc = __mnt_fs_set_optstr_ptr(*ent, u, FALSE);
	if (rc)
		goto err;
	u = NULL;

	if (!(mountflags & MS_REMOUNT)) {
		rc = set_fs_root(*ent, fs, mountflags);
		if (rc)
			goto err;
	}

	DBG(UPDATE, mnt_debug("utab entry OK"));
	return 0;
err:
	free(u);
	mnt_free_fs(*ent);
	return rc;
}

static int set_fs_root(mnt_fs *result, mnt_fs *fs, unsigned long mountflags)
{
	char *root = NULL, *mnt = NULL;
	const char *fstype, *optstr;
	mnt_tab *tb = NULL;
	int rc = -ENOMEM;

	assert(fs);
	assert(result);

	DBG(UPDATE, mnt_debug("setting FS root"));

	optstr = mnt_fs_get_optstr(fs);
	fstype = mnt_fs_get_fstype(fs);

	/*
	 * bind-mount -- get fs-root and source device for the source filesystem
	 */
	if (mountflags & MS_BIND) {
		const char *src, *src_root;
		mnt_fs *src_fs;

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

		tb = __mnt_new_tab_from_file(_PATH_PROC_MOUNTINFO, MNT_FMT_MOUNTINFO);
		if (!tb)
			goto dflt;
		src_fs = mnt_tab_find_target(tb, mnt, MNT_ITER_BACKWARD);
		if (!src_fs)
			goto dflt;

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

		if (mnt_optstr_get_option((char *) optstr, "subvol", &vol, &volsz))
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
static int fprintf_mtab_fs(FILE *f, mnt_fs *fs)
{
	char *m1, *m2, *m3, *m4;
	int rc;

	assert(fs);
	assert(f);

	m1 = mangle(mnt_fs_get_source(fs));
	m2 = mangle(mnt_fs_get_target(fs));
	m3 = mangle(mnt_fs_get_fstype(fs));
	m4 = mangle(mnt_fs_get_optstr(fs));

	if (m1 && m2 && m3 && m4)
		rc = !fprintf(f, "%s %s %s %s %d %d\n",
				m1, m2, m3, m4,
				mnt_fs_get_freq(fs),
				mnt_fs_get_passno(fs));
	else
		rc = -ENOMEM;

	free(m1);
	free(m2);
	free(m3);
	free(m4);

	return rc;
}

static int fprintf_utab_fs(FILE *f, mnt_fs *fs)
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
	p = mangle(mnt_fs_get_optstr(fs));
	if (p) {
		fprintf(f, "OPTS=%s", p);
		free(p);
	}

	fputc('\n', f);

	return 0;
}

static int update_tab(mnt_update *upd, const char *filename, mnt_tab *tb)
{
	FILE *f;
	int rc, fd;
	char *uq = NULL;

	if (!tb || !filename)
		return -EINVAL;

	DBG(UPDATE, mnt_debug_h(upd, "%s: updating", filename));

	fd = mnt_open_uniq_filename(filename, &uq, O_WRONLY);
	if (fd < 0)
		return fd;	/* error */

	f = fdopen(fd, "w");
	if (f) {
		struct stat st;
		mnt_iter itr;
		mnt_fs *fs;
		int fd;

		mnt_reset_iter(&itr, MNT_ITER_FORWARD);
		while(mnt_tab_next_fs(tb, &itr, &fs) == 0) {
			if (upd->userspace_only)
				fprintf_utab_fs(f, fs);
			else
				fprintf_mtab_fs(f, fs);
		}
		fd = fileno(f);
		rc = fchmod(fd, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) ? -errno : 0;

		if (!rc &&stat(filename, &st) == 0)
			/* Copy uid/gid from the present file before renaming. */
			rc = fchown(fd, st.st_uid, st.st_gid) ? -errno : 0;

		fclose(f);
		rc = rename(uq, filename) ? -errno : 0;
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

static int update_add_entry(mnt_update *upd, const char *filename, mnt_lock *lc)
{
	FILE *f;
	int rc = 0, u_lc = -1;

	assert(upd);
	assert(upd->fs);

	DBG(UPDATE, mnt_debug_h(upd, "%s: add entry", filename));

	if (lc)
		mnt_lock_file(lc);
	else if (upd->userspace_only)
		u_lc = utab_lock(filename);

	f = fopen(filename, "a+");
	if (f) {
		rc = upd->userspace_only ? fprintf_utab_fs(f, upd->fs) :
					   fprintf_mtab_fs(f, upd->fs);
		DBG(UPDATE, mnt_debug_h(upd, "%s: add [rc=%d]", filename, rc));
		fclose(f);
	} else {
		DBG(UPDATE, mnt_debug_h(upd, "%s: failed: %m", filename));
		rc = -errno;
	}
	if (lc)
		mnt_unlock_file(lc);
	else if (u_lc != -1)
		utab_unlock(u_lc);
	return rc;
}

static int update_remove_entry(mnt_update *upd, const char *filename, mnt_lock *lc)
{
	mnt_tab *tb;
	int rc = 0, u_lc = -1;

	assert(upd);
	assert(upd->target);

	DBG(UPDATE, mnt_debug_h(upd, "%s: remove entry", filename));

	if (lc)
		mnt_lock_file(lc);
	else if (upd->userspace_only)
		u_lc = utab_lock(filename);

	tb = __mnt_new_tab_from_file(filename,
			upd->userspace_only ? MNT_FMT_UTAB : MNT_FMT_MTAB);
	if (tb) {
		mnt_fs *rem = mnt_tab_find_target(tb, upd->target, MNT_ITER_BACKWARD);
		if (rem) {
			mnt_tab_remove_fs(tb, rem);
			rc = update_tab(upd, filename, tb);
			mnt_free_fs(rem);
		}
		mnt_free_tab(tb);
	}
	if (lc)
		mnt_unlock_file(lc);
	else if (u_lc != -1)
		utab_unlock(u_lc);
	return rc;
}

static int update_modify_target(mnt_update *upd, const char *filename, mnt_lock *lc)
{
	mnt_tab *tb = NULL;
	int rc = 0, u_lc = -1;

	DBG(UPDATE, mnt_debug_h(upd, "%s: modify target", filename));

	if (lc)
		mnt_lock_file(lc);
	else if (upd->userspace_only)
		u_lc = utab_lock(filename);

	tb = __mnt_new_tab_from_file(filename,
			upd->userspace_only ? MNT_FMT_UTAB : MNT_FMT_MTAB);
	if (tb) {
		mnt_fs *cur = mnt_tab_find_target(tb, upd->target, MNT_ITER_BACKWARD);
		if (cur) {
			rc = mnt_fs_set_target(cur, mnt_fs_get_target(upd->fs));
			if (!rc)
				rc = update_tab(upd, filename, tb);
		}
		mnt_free_tab(tb);
	}
	if (lc)
		mnt_unlock_file(lc);
	else if (u_lc != -1)
		utab_unlock(u_lc);
	return rc;
}

static int update_modify_options(mnt_update *upd, const char *filename, mnt_lock *lc)
{
	mnt_tab *tb = NULL;
	int rc = 0, u_lc = -1;

	assert(upd);
	assert(upd->fs);

	DBG(UPDATE, mnt_debug_h(upd, "%s: modify options", filename));

	if (lc)
		mnt_lock_file(lc);
	else if (upd->userspace_only)
		u_lc = utab_lock(filename);

	tb = __mnt_new_tab_from_file(filename,
			upd->userspace_only ? MNT_FMT_UTAB : MNT_FMT_MTAB);
	if (tb) {
		mnt_fs *cur = mnt_tab_find_target(tb,
					mnt_fs_get_target(upd->fs),
					MNT_ITER_BACKWARD);
		if (cur) {
			rc = mnt_fs_set_optstr(cur, mnt_fs_get_optstr(upd->fs));
			if (!rc)
				rc = update_tab(upd, filename, tb);
		}
		mnt_free_tab(tb);
	}
	if (lc)
		mnt_unlock_file(lc);
	else if (u_lc != -1)
		utab_unlock(u_lc);

	return rc;
}

/**
 * mnt_update_tab:
 * @filename: mtab of utab filename
 * @lc: lock
 *
 * High-level API to update /etc/mtab or /dev/.mount/utab.
 *
 * Returns: 0 on success, negative number on error.
 */
int mnt_update_tab(mnt_update *upd, const char *filename, mnt_lock *lc)
{
	int rc = -EINVAL;

	assert(upd);
	assert(filename);

	DBG(UPDATE, mnt_debug_h(upd, "%s: update tab", filename));

	if (!filename || !upd)
		return -EINVAL;
	if (!upd->ready)
		return 0;

	if (!upd->fs && upd->target)
		rc = update_remove_entry(upd, filename, lc);	/* umount */
	else if (upd->mountflags & MS_MOVE)
		rc = update_modify_target(upd, filename, lc);	/* move */
	else if (upd->mountflags & MS_REMOUNT)
		rc = update_modify_options(upd, filename, lc);	/* remount */
	else if (upd->fs)
		rc = update_add_entry(upd, filename, lc);	/* mount */

	upd->ready = FALSE;
	DBG(UPDATE, mnt_debug_h(upd, "%s: update tab: done [rc=%d]", filename, rc));
	return rc;
}

#ifdef TEST_PROGRAM

#include <errno.h>

mnt_lock *lock;

static void lock_fallback(void)
{
	if (lock)
		mnt_unlock_file(lock);
}

static int update(const char *target, mnt_fs *fs, unsigned long mountflags)
{
	int rc, writable = 0;
	const char *filename = NULL;
	mnt_update *upd;
	mnt_lock *lock = NULL;

	DBG(UPDATE, mnt_debug("update test"));

	rc = mnt_has_regular_mtab(&filename, &writable);
	if (rc && writable) {
		upd = mnt_new_update(FALSE);
		lock = mnt_new_lock(filename, 0);

		/* note that proper solution is to call mnt_unlock_file() from
		 * signal handler. The atexit() could be ignore if program ends
		 * by _exit(). The _exit() function is usually used in signal
		 * handlers.
		 */
		atexit(lock_fallback);

	} else {
		filename = NULL;
		rc = mnt_has_regular_utab(&filename, &writable);

		if (rc && writable)
			upd = mnt_new_update(TRUE);
		else {
			fprintf(stderr, "utab useless: %m\n");
			return -1;
		}
	}

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

	rc = mnt_update_tab(upd, filename, lock);
done:
	return rc;
}

static int test_add(struct mtest *ts, int argc, char *argv[])
{
	mnt_fs *fs = mnt_new_fs();
	int rc;

	if (argc < 5 || !fs)
		return -1;
	mnt_fs_set_source(fs, argv[1]);
	mnt_fs_set_target(fs, argv[2]);
	mnt_fs_set_fstype(fs, argv[3]);
	mnt_fs_set_optstr(fs, argv[4]);

	rc = update(NULL, fs, 0);
	mnt_free_fs(fs);
	return rc;
}

static int test_remove(struct mtest *ts, int argc, char *argv[])
{
	if (argc < 2)
		return -1;
	return update(argv[1], NULL, 0);
}

static int test_move(struct mtest *ts, int argc, char *argv[])
{
	mnt_fs *fs = mnt_new_fs();
	int rc;

	if (argc < 3)
		return -1;
	mnt_fs_set_target(fs, argv[2]);
	rc = update(argv[1], fs, MS_MOVE);

	mnt_free_fs(fs);
	return rc;
}

static int test_remount(struct mtest *ts, int argc, char *argv[])
{
	mnt_fs *fs = mnt_new_fs();
	int rc;

	if (argc < 3)
		return -1;
	mnt_fs_set_target(fs, argv[1]);
	mnt_fs_set_optstr(fs, argv[2]);

	rc = update(NULL, fs, MS_REMOUNT);
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
	{ NULL }
	};

	return mnt_run_test(tss, argc, argv);
}

#endif /* TEST_PROGRAM */
