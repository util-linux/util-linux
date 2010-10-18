/*
 * Copyright (C) 2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>

#include "nls.h"
#include "at.h"
#include "mangle.h"
#include "mountP.h"
#include "pathnames.h"

static inline char *skip_spaces(char *s)
{
	assert(s);

	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

static int next_number(char **s, int *num)
{
	char *end = NULL;

	assert(num);
	assert(s);

	*s = skip_spaces(*s);
	if (!**s)
		return -1;
	*num = strtol(*s, &end, 10);
	if (end == NULL || *s == end)
	       return -1;

	*s = end;

	/* valid end of number is space or terminator */
	if (*end == ' ' || *end == '\t' || *end == '\0')
		return 0;
	return -1;
}

/*
 * Parses one line from {fs,m}tab
 */
static int mnt_tab_parse_file_line(mnt_fs *fs, char *s)
{
	int rc, n = 0;
	char *src, *fstype, *optstr;

	rc = sscanf(s,	"%ms "	/* (1) source */
			"%ms "	/* (2) target */
			"%ms "	/* (3) FS type */
			"%ms "  /* (4) options */
			"%n",	/* byte count */
			&src,
			&fs->target,
			&fstype,
			&optstr,
			&n);

	if (rc == 4) {
		unmangle_string(src);
		unmangle_string(fs->target);
		unmangle_string(fstype);
		unmangle_string(optstr);

		rc = __mnt_fs_set_source_ptr(fs, src);
		if (!rc)
			rc = __mnt_fs_set_fstype_ptr(fs, fstype);
		if (!rc)
			rc = __mnt_fs_set_optstr_ptr(fs, optstr, TRUE);
	} else {
		DBG(TAB, mnt_debug( "parse error: [sscanf rc=%d]: '%s'", rc, s));
		rc = -EINVAL;
	}

	if (rc)
		return rc;	/* error */

	fs->passno = fs->freq = 0;
	s = skip_spaces(s + n);
	if (*s) {
		if (next_number(&s, &fs->freq) != 0) {
			if (*s)
				rc = -EINVAL;
		} else if (next_number(&s, &fs->passno) != 0 && *s)
			rc = -EINVAL;
	}

	return rc;
}

/*
 * Parses one line from mountinfo file
 */
static int mnt_parse_mountinfo_line(mnt_fs *fs, char *s)
{
	int rc;
	unsigned int maj, min;
	char *fstype, *src;

	rc = sscanf(s,	"%u "		/* (1) id */
			"%u "		/* (2) parent */
			"%u:%u "	/* (3) maj:min */
			"%ms "		/* (4) mountroot */
			"%ms "		/* (5) target */
			"%ms"		/* (6) vfs options (fs-independent) */
			"%*[^-]"	/* (7) optional fields */
			"- "		/* (8) separator */
			"%ms "		/* (9) FS type */
			"%ms "		/* (10) source */
			"%ms",		/* (11) fs options (fs specific) */

			&fs->id,
			&fs->parent,
			&maj, &min,
			&fs->root,
			&fs->target,
			&fs->vfs_optstr,
			&fstype,
			&src,
			&fs->fs_optstr);

	if (rc == 10) {
		fs->devno = makedev(maj, min);

		unmangle_string(fs->root);
		unmangle_string(fs->target);
		unmangle_string(fs->vfs_optstr);
		unmangle_string(fstype);

		if (!strcmp(src, "none")) {
			free(src);
			src = NULL;
		} else
			unmangle_string(src);

		unmangle_string(fs->fs_optstr);

		rc = __mnt_fs_set_fstype_ptr(fs, fstype);
		if (!rc)
			rc = __mnt_fs_set_source_ptr(fs, src);
	} else {
		DBG(TAB, mnt_debug("parse error [field=%d]: '%s'", rc, s));
		rc = -EINVAL;
	}
	return rc;
}

/*
 * Returns {m,fs}tab or mountinfo file format (MNT_FMT_*)
 *
 * The "mountinfo" format is always: "<number> <number> ... "
 */
static int detect_fmt(char *line)
{
	unsigned int a, b;

	return sscanf(line, "%u %u", &a, &b) == 2 ?
			MNT_FMT_MOUNTINFO : MNT_FMT_FSTAB;
}


/*
 * Merges @vfs and @fs options strings into a new string.
 * This function cares about 'ro/rw' options. The 'ro' is
 * always used if @vfs or @fs is read-only.
 * For example:
 *
 *    mnt_merge_optstr("rw,noexec", "ro,journal=update")
 *
 *           returns: "ro,noexec,journal=update"
 *
 *    mnt_merge_optstr("rw,noexec", "rw,journal=update")
 *
 *           returns: "rw,noexec,journal=update"

 * We need this function for /proc/self/mountinfo parsing.
 */
static char *merge_optstr(const char *vfs, const char *fs)
{
	char *res, *p;
	size_t sz;
	int ro = 0, rw = 0;

	if (!vfs && !fs)
		return NULL;
	if (!vfs || !fs)
		return strdup(fs ? fs : vfs);
	if (!strcmp(vfs, fs))
		return strdup(vfs);		/* e.g. "aaa" and "aaa" */

	/* leave space for leading "r[ow],", "," and trailing zero */
	sz = strlen(vfs) + strlen(fs) + 5;
	res = malloc(sz);
	if (!res)
		return NULL;
	p = res + 3;			/* make a room for rw/ro flag */

	snprintf(p, sz - 3, "%s,%s", vfs, fs);

	/* remove 'rw' flags */
	rw += !mnt_optstr_remove_option(&p, "rw");	/* from vfs */
	rw += !mnt_optstr_remove_option(&p, "rw");	/* from fs */

	/* remove 'ro' flags if necessary */
	if (rw != 2) {
		ro += !mnt_optstr_remove_option(&p, "ro");
		if (ro + rw < 2)
			ro += !mnt_optstr_remove_option(&p, "ro");
	}

	if (!strlen(p))
		memcpy(res, ro ? "ro" : "rw", 3);
	else
		memcpy(res, ro ? "ro," : "rw,", 3);
	return res;
}

/*
 * Read and parse the next line from {fs,m}tab or mountinfo
 */
static int mnt_tab_parse_next(mnt_tab *tb, FILE *f, mnt_fs *fs,
				const char *filename, int *nlines)
{
	char buf[BUFSIZ];
	char *s;

	assert(tb);
	assert(f);
	assert(fs);

	/* read the next non-blank non-comment line */
	do {
		if (fgets(buf, sizeof(buf), f) == NULL)
			return -EINVAL;
		++*nlines;
		s = index (buf, '\n');
		if (!s) {
			/* Missing final newline?  Otherwise extremely */
			/* long line - assume file was corrupted */
			if (feof(f)) {
				DBG(TAB, mnt_debug_h(tb,
					"%s: no final newline",	filename));
				s = index (buf, '\0');
			} else {
				DBG(TAB, mnt_debug_h(tb,
					"%s:%d: missing newline at line",
					filename, *nlines));
				goto err;
			}
		}
		*s = '\0';
		if (--s >= buf && *s == '\r')
			*s = '\0';
		s = skip_spaces(buf);
	} while (*s == '\0' || *s == '#');

	/*DBG(TAB, mnt_debug_h(tb, "%s:%d: %s", filename, *nlines, s));*/

	if (!tb->fmt)
		tb->fmt = detect_fmt(s);

	if (tb->fmt == MNT_FMT_FSTAB) {
		if (mnt_tab_parse_file_line(fs, s) != 0)
			goto err;

	} else if (tb->fmt == MNT_FMT_MOUNTINFO) {
		if (mnt_parse_mountinfo_line(fs, s) != 0)
			goto err;
	}

	/* merge fs_optstr and vfs_optstr into optstr (necessary for "mountinfo") */
	if (!fs->optstr && (fs->vfs_optstr || fs->fs_optstr)) {
		fs->optstr = merge_optstr(fs->vfs_optstr, fs->fs_optstr);
		if (!fs->optstr)
			return -ENOMEM;
	}
/*
	DBG(TAB, mnt_debug_h(tb, "%s:%d: SOURCE:%s, MNTPOINT:%s, TYPE:%s, "
				  "OPTS:%s, FREQ:%d, PASSNO:%d",
		filename, *nlines,
		fs->source, fs->target, fs->fstype,
		fs->optstr, fs->freq, fs->passno));
*/
	return 0;
err:
	DBG(TAB, mnt_debug_h(tb, "%s:%d: parse error", filename, *nlines));

	/* by default all errors are recoverable, otherwise behavior depends on
	 * errcb() function. See mnt_tab_set_parser_errcb().
	 */
	return tb->errcb ? tb->errcb(tb, filename, *nlines) : 1;
}

/**
 * mnt_tab_parse_stream:
 * @tb: tab pointer
 * @f: file stream
 * @filename: filename used for debug and error messages
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_tab_parse_stream(mnt_tab *tb, FILE *f, const char *filename)
{
	int nlines = 0;
	int rc = -1;

	assert(tb);
	assert(f);
	assert(filename);

	DBG(TAB, mnt_debug_h(tb, "%s: start parsing", filename));

	while (!feof(f)) {
		mnt_fs *fs = mnt_new_fs();

		if (!fs)
			goto err;

		rc = mnt_tab_parse_next(tb, f, fs, filename, &nlines);
		if (!rc)
			rc = mnt_tab_add_fs(tb, fs);
		if (rc) {
			mnt_free_fs(fs);
			if (rc == 1)
				continue;	/* recoverable error */
			if (feof(f))
				break;
			goto err;		/* fatal error */
		}
	}

	DBG(TAB, mnt_debug_h(tb, "%s: stop parsing", filename));
	return 0;
err:
	DBG(TAB, mnt_debug_h(tb, "%s: parse error (rc=%d)", filename, rc));
	return rc;
}

/**
 * mnt_tab_parse_file:
 * @tb: tab pointer
 * @filename: file
 *
 * Parses whole table (e.g. /etc/mtab) and appends new records to the @tab.
 *
 * The libmount parser ignores broken (syntax error) lines, these lines are
 * reported to caller by errcb() function (see mnt_tab_set_parser_errcb()).
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_tab_parse_file(mnt_tab *tb, const char *filename)
{
	FILE *f;
	int rc;

	assert(tb);
	assert(filename);

	if (!filename || !tb)
		return -EINVAL;

	f = fopen(filename, "r");
	if (f) {
		rc = mnt_tab_parse_stream(tb, f, filename);
		fclose(f);
	} else
		return -errno;

	return rc;
}

/**
 * mnt_new_tab_from_file:
 * @filename: /etc/{m,fs}tab or /proc/self/mountinfo path
 *
 * Same as mnt_new_tab() + mnt_tab_parse_file(). Use this function for private
 * files only. This function does not allow to use error callback, so you
 * cannot provide any feedback to end-users about broken records in files (e.g.
 * fstab).
 *
 * Returns: newly allocated tab on success and NULL in case of error.
 */
mnt_tab *mnt_new_tab_from_file(const char *filename)
{
	mnt_tab *tb;

	assert(filename);

	if (!filename)
		return NULL;
	tb = mnt_new_tab();
	if (tb && mnt_tab_parse_file(tb, filename) != 0) {
		mnt_free_tab(tb);
		tb = NULL;
	}
	return tb;
}

/**
 * mnt_tab_set_parser_errcb:
 * @tab: pointer to table
 * @cb: pointer to callback function
 *
 * The error callback function is called by table parser (mnt_tab_parse_file())
 * in case of syntax error. The callback function could be used for errors
 * evaluation, libmount will continue/stop parsing according to callback return
 * codes:
 *
 *   <0  : fatal error (abort parsing)
 *    0	 : success (parsing continue)
 *   >0  : recoverable error (the line is ignored, parsing continue).
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_tab_set_parser_errcb(mnt_tab *tb,
		int (*cb)(mnt_tab *tb, const char *filename, int line))
{
	assert(tb);
	tb->errcb = cb;
	return 0;
}

/**
 * mnt_tab_parse_fstab:
 * @tb: table
 *
 * This function parses /etc/fstab or /etc/fstab.d and appends new lines to the
 * @tab. If the system contains classic fstab file and also fstab.d directory
 * then the fstab file is parsed before the fstab.d directory.
 *
 * The fstab.d directory:
 *	- files are sorted by strverscmp(3)
 *	- files that starts with "." are ignored (e.g. ".10foo.fstab")
 *	- files without the ".fstab" extension are ignored
 *
 * See also mnt_tab_set_parser_errcb().
 *
 * Returns: 0 on success (least one record has been successfully parsed) or
 *          negative number in case of error.
 */
int mnt_tab_parse_fstab(mnt_tab *tb)
{
	int num, n = 0, i;
	DIR *dir = NULL;
	FILE *f;
	struct dirent **namelist = NULL;

	assert(tb);
	if (!tb)
		return -EINVAL;

	num = mnt_tab_get_nents(tb);

	/* classic fstab */
	{
		f = fopen(_PATH_MNTTAB, "r");
		if (f) {
			mnt_tab_parse_stream(tb, f, _PATH_MNTTAB);
			fclose(f);
		}
	}

	/* TODO: it would be nice to have a scandir() implementaion that
	 *       is able to use already opened directory */
	n = scandir(_PATH_MNTTAB_DIR, &namelist, NULL, versionsort);
	if (n <= 0)
		goto done;

	/* let use "at" functions rather than play crazy games with paths... */
	dir = opendir(_PATH_MNTTAB_DIR);
	if (!dir)
		goto done;

	for (i = 0; i < n; i++) {
		struct dirent *d = namelist[i];
		struct stat st;
		size_t namesz;

#ifdef _DIRENT_HAVE_D_TYPE
		if (d->d_type != DT_UNKNOWN && d->d_type != DT_REG &&
		    d->d_type != DT_LNK)
			continue;
#endif
		if (*d->d_name == '.')
			continue;

#define MNT_MNTTABDIR_EXTSIZ	(sizeof(MNT_MNTTABDIR_EXT) - 1)

		namesz = strlen(d->d_name);
		if (!namesz || namesz < MNT_MNTTABDIR_EXTSIZ + 1 ||
		    strcmp(d->d_name + (namesz - MNT_MNTTABDIR_EXTSIZ),
			    MNT_MNTTABDIR_EXT))
				continue;

		if (fstat_at(dirfd(dir), _PATH_MNTTAB_DIR, d->d_name, &st, 0) ||
		    !S_ISREG(st.st_mode))
			continue;

		f = fopen_at(dirfd(dir), _PATH_MNTTAB_DIR,
					d->d_name, O_RDONLY, "r");
		if (f) {
			mnt_tab_parse_stream(tb, f, d->d_name);
			fclose(f);
		}
	}
done:
	for (i = 0; i < n; i++)
		free(namelist[i]);
	free(namelist);
	if (dir)
		closedir(dir);

	num = mnt_tab_get_nents(tb) - num;

	DBG(TAB, mnt_debug_h(tb, "fstab contains %d records", num));

	return num > 0 ? 0 : -1;
}

/*
 * This function uses @uf to found corresponding record in @tb, then the record
 * from @tb is updated (userspace specific mount options are added).
 *
 * Note that @uf must contain only userspace specific mount options instead of
 * VFS options (note that FS options are ignored).
 *
 * Returns modified filesystem (from @tb) or NULL.
 */
static mnt_fs *mnt_tab_merge_userspace_fs(mnt_tab *tb, mnt_fs *uf)
{
	mnt_fs *fs;
	mnt_iter itr;
	const char *optstr, *src, *target, *root;

	assert(tb);
	assert(uf);
	if (!tb || !uf)
		return NULL;

	src = mnt_fs_get_srcpath(uf);
	target = mnt_fs_get_target(uf);
	optstr = mnt_fs_get_vfs_optstr(uf);
	root = mnt_fs_get_root(uf);

	if (!src || !target || !optstr || !root)
		return NULL;

	mnt_reset_iter(&itr, MNT_ITER_BACKWARD);

	while(mnt_tab_next_fs(tb, &itr, &fs) == 0) {
		const char *s = mnt_fs_get_srcpath(fs),
			   *t = mnt_fs_get_target(fs),
			   *r = mnt_fs_get_root(fs);

		if (s && t && r && !strcmp(t, target) &&
		    !strcmp(s, src) && !strcmp(r, root))
			break;
	}

	if (fs)
		mnt_fs_append_userspace_optstr(fs, optstr);
	return fs;
}

/**
 * mnt_tab_parse_mtab:
 * @tb: table
 *
 * This function parses /etc/mtab or {/proc/self,/var/run/mount}/mountinfo or
 * /proc/mounts. Note that the /var/run/mount/mountinfo file is optional and
 * contains userspace specific mount options only.
 *
 * See also mnt_tab_set_parser_errcb().
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_tab_parse_mtab(mnt_tab *tb)
{
	int rc;
	mnt_tab *u_tb;
	mnt_fs *u_fs;
	mnt_iter itr;

	if (mnt_has_regular_mtab()) {
		rc = mnt_tab_parse_file(tb, _PATH_MOUNTED);
		if (!rc)
			return 0;		/* system with regular mtab */
	}

	/* read kernel information from /proc/self/mountinfo */
	rc = mnt_tab_parse_file(tb, _PATH_PROC_MOUNTINFO);
	if (rc)
		/* hmm, old kernel? ...try /proc/mounts */
		return mnt_tab_parse_file(tb, _PATH_PROC_MOUNTS);

	/* try to read userspace specific information from /var/run/mount */
	u_tb = mnt_new_tab_from_file(MNT_PATH_MOUNTINFO);
	if (!u_tb)
		return 0;	/* private mountinfo does not exist */

	mnt_reset_iter(&itr, MNT_ITER_BACKWARD);

	/*  merge userspace options into mountinfo from kernel */
	while(mnt_tab_next_fs(u_tb, &itr, &u_fs) == 0)
		mnt_tab_merge_userspace_fs(tb, u_fs);

	mnt_free_tab(u_tb);
	return 0;
}
