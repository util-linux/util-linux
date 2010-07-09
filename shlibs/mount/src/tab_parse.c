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

static inline char *skip_nonspaces(char *s)
{
	assert(s);

	while (*s && !(*s == ' ' || *s == '\t'))
		s++;
	return s;
}

static size_t next_word_size(char *s, char **start, char **end)
{
	char *e;

	assert(s);

	s = skip_spaces(s);
	if (!*s)
		return 0;
	e = skip_nonspaces(s);

	if (start)
		*start = s;
	if (end)
		*end = e;

	return e - s;
}

static char *next_word(char **s)
{
	size_t sz;
	char *res, *end;

	assert(s);

	sz = next_word_size(*s, s, &end) + 1;
	if (sz == 1)
		return NULL;

	res = malloc(sz);
	if (!res)
		return NULL;

	unmangle_to_buffer(*s, res, sz);
	*s = end + 1;
	return res;
}

static int next_word_skip(char **s)
{
	*s = skip_spaces(*s);
	if (!**s)
		return 1;
	*s = skip_nonspaces(*s);
	return 0;
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
	/* SOURCE */
	if (__mnt_fs_set_source(fs, next_word(&s)) != 0)
		return 1;

	/* TARGET */
	fs->target = next_word(&s);
	if (!fs->target)
		return 1;

	/* TYPE */
	if (__mnt_fs_set_fstype(fs, next_word(&s)) != 0)
		return 1;

	/* OPTS */
	fs->optstr = next_word(&s);
	if (!fs->optstr)
		return 1;
	/* default */
	fs->passno = fs->freq = 0;

	/* FREQ (optional) */
	if (next_number(&s, &fs->freq) != 0) {
		if (*s)
			return 1;

	/* PASSNO (optional) */
	} else if (next_number(&s, &fs->passno) != 0 && *s)
		return 1;

	return 0;
}

/*
 * Parses one line from mountinfo file
 */
static int mnt_parse_mountinfo_line(mnt_fs *fs, char *s)
{
	unsigned int maj, min;

	/* ID */
	if (next_number(&s, &fs->id) != 0)
		return 1;

	/* PARENT */
	if (next_number(&s, &fs->parent) != 0)
		return 1;

	/* <maj>:<min> */
	s = skip_spaces(s);
	if (!*s || sscanf(s, "%u:%u", &maj, &min) != 2)
		return 1;
	fs->devno = makedev(maj, min);
	next_word_skip(&s);

	/* MOUNTROOT */
	fs->root = next_word(&s);
	if (!fs->root)
		return 1;

	/* TARGET (mountpoit) */
	fs->target = next_word(&s);
	if (!fs->target)
		return 1;

	/* OPTIONS (fs-independent) */
	fs->vfs_optstr = next_word(&s);
	if (!fs->vfs_optstr)
		return 1;

	/* optional fields (ignore) */
	do {
		s = skip_spaces(s);
		if (s && *s == '-' &&
		    (*(s + 1) == ' ' || *(s + 1) == '\t')) {
			s++;
			break;
		}
		if (s && next_word_skip(&s) != 0)
			return 1;
	} while (s);

	/* FSTYPE */
	if (__mnt_fs_set_fstype(fs, next_word(&s)) != 0)
		return 1;

	/* SOURCE or "none" */
	if (__mnt_fs_set_source(fs, next_word(&s)) != 0)
		return 1;

	/* OPTIONS (fs-dependent) */
	fs->fs_optstr = next_word(&s);
	if (!fs->fs_optstr)
		return 1;

	return 0;
}

/*
 * Returns {m,fs}tab or mountinfo file format (MNT_FMT_*)
 *
 * The "mountinfo" format is always: "<number> <number> ... "
 */
static int detect_fmt(char *line)
{
	int num;

	/* ID */
	if (next_number(&line, &num) != 0)
		return MNT_FMT_FSTAB;

	/* PARENT */
	if (next_number(&line, &num) != 0)
		return MNT_FMT_FSTAB;

	return MNT_FMT_MOUNTINFO;
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
			return -1;
		++*nlines;
		s = index (buf, '\n');
		if (!s) {
			/* Missing final newline?  Otherwise extremely */
			/* long line - assume file was corrupted */
			if (feof(f)) {
				DBG(DEBUG_TAB, fprintf(stderr,
					"libmount: WARNING: no final newline at the end of %s\n",
					filename));
				s = index (buf, '\0');
			} else {
				DBG(DEBUG_TAB, fprintf(stderr,
					"libmount: %s: %d: missing newline at line\n",
					filename, *nlines));
				goto err;
			}
		}
		*s = '\0';
		if (--s >= buf && *s == '\r')
			*s = '\0';
		s = skip_spaces(buf);
	} while (*s == '\0' || *s == '#');

	DBG(DEBUG_TAB, fprintf(stderr, "libmount: %s:%d: %s\n",
		filename, *nlines, s));

	if (!tb->fmt)
		tb->fmt = detect_fmt(s);

	if (tb->fmt == MNT_FMT_FSTAB) {
		/* parse /etc/{fs,m}tab */
		if (mnt_tab_parse_file_line(fs, s) != 0)
			goto err;

	} else if (tb->fmt == MNT_FMT_MOUNTINFO) {
		/* parse /proc/self/mountinfo */
		if (mnt_parse_mountinfo_line(fs, s) != 0)
			goto err;
	}

	/* merge fs_optstr and vfs_optstr into optstr (necessary for "mountinfo") */
	if (!fs->optstr && (fs->vfs_optstr || fs->fs_optstr)) {
		fs->optstr = merge_optstr(fs->vfs_optstr, fs->fs_optstr);
		if (!fs->optstr)
			return -1;
	}

	DBG(DEBUG_TAB, fprintf(stderr,
		"libmount: tab %p: %s:%d: SOURCE:%s, MNTPOINT:%s, TYPE:%s, "
				  "OPTS:%s, FREQ:%d, PASSNO:%d\n",
		tb, filename, *nlines,
		fs->source, fs->target, fs->fstype,
		fs->optstr, fs->freq, fs->passno));

	return 0;
err:
	DBG(DEBUG_TAB, fprintf(stderr,
		"libmount: tab %p: %s:%d: parse error\n", tb, filename, *nlines));

	if (tb->errcb && tb->errcb(tb, filename, *nlines, 0))
		return -1;	/* fatal error */

	return 1;		/* recoverable error */
}

/**
 * mnt_tab_parse_stream:
 * @tb: tab pointer
 * @f: file stream
 * @filename: filename used for debug and error messages
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_tab_parse_stream(mnt_tab *tb, FILE *f, const char *filename)
{
	int nlines = 0;

	assert(tb);
	assert(f);
	assert(filename);

	DBG(DEBUG_TAB,
		fprintf(stderr, "libmount: tab %p: start parsing %s\n", tb, filename));

	while (!feof(f)) {
		int rc;
		mnt_fs *fs = mnt_new_fs();

		if (!fs)
			goto error;

		rc = mnt_tab_parse_next(tb, f, fs, filename, &nlines);
		if (!rc)
			rc = mnt_tab_add_fs(tb, fs);
		if (rc) {
			mnt_free_fs(fs);
			if (rc == 1)
				continue;	/* recoverable error */
			if (feof(f))
				break;
			goto error;		/* fatal error */
		}
	}

	DBG(DEBUG_TAB,
		fprintf(stderr, "libmount: tab %p: stop parsing %s\n", tb, filename));
	return 0;
error:
	DBG(DEBUG_TAB,
		fprintf(stderr, "libmount: tab %p: error parsing %s\n", tb, filename));
	return -1;
}

/**
 * mnt_tab_parse_file:
 * @tb: tab pointer
 * @filename: file
 *
 * Parses whole table (e.g. /etc/mtab) and appends new records to the @tab.
 *
 * <informalexample>
 *   <programlisting>
 *	mnt_tab *tb = mnt_new_tab();
 *	int rc;
 *
 *	rc = mnt_tab_parse_file(tb, "/etc/fstab");
 *	if (!rc)
 *		mnt_fprintf_tab(tb, stdout, NULL);
 *	mnt_free_tab(tb);
 *   </programlisting>
 * </informalexample>
 *
 * The libmount parser ignores broken (syntax error) lines, these lines are
 * reported to caller by errcb() function (see mnt_tab_set_parser_errcb()).
 *
 * Returns: 0 on success, -1 in case of error.
 */
int mnt_tab_parse_file(mnt_tab *tb, const char *filename)
{
	FILE *f;
	int rc = -1;

	assert(tb);
	assert(filename);

	if (!filename || !tb)
		return -1;

	f = fopen(filename, "r");
	if (f) {
		rc = mnt_tab_parse_stream(tb, f, filename);
		fclose(f);
	}
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
 * in case of sytax error. If the callback function does not return zero then
 * parsing is aborted.
 *
 * Returns: 0 on success or -1 in case of error.
 */
int mnt_tab_set_parser_errcb(mnt_tab *tb,
		int (*cb)(mnt_tab *tb, const char *filename, int line, int flag))
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
 * Returns: 0 on success (least one record has been sucessfully parsed) or -1.
 */
int mnt_tab_parse_fstab(mnt_tab *tb)
{
	int num, n = 0, i;
	DIR *dir = NULL;
	FILE *f;
	struct dirent **namelist = NULL;

	assert(tb);
	if (!tb)
		return -1;

	num = mnt_tab_get_nents(tb);

	/* classic fstab */
	{
		f = fopen(_PATH_MNTTAB, "r");
		if (f) {
			mnt_tab_parse_stream(tb, f, _PATH_MNTTAB);
			fclose(f);
		}
	}

	/* TODO: it would be nice to have a scandir() implemenation that
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

	DBG(DEBUG_TAB, fprintf(stderr,
		"libmount: tab %p: fstab contains %d records\n", tb, num));

	return num > 0 ? 0 : -1;
}
