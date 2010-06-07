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

#include "nls.h"
#include "mangle.h"
#include "mountP.h"

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
	fs->mntroot = next_word(&s);
	if (!fs->mntroot)
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

	sz = strlen(vfs) + strlen(fs) + 2;
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
static int mnt_tab_parse_next(mnt_tab *tb, FILE *f, mnt_fs *fs)
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
		tb->nlines++;
		s = index (buf, '\n');
		if (!s) {
			/* Missing final newline?  Otherwise extremely */
			/* long line - assume file was corrupted */
			if (feof(f)) {
				DBG(DEBUG_TAB, fprintf(stderr,
					"libmount: WARNING: no final newline at the end of %s\n",
					tb->filename));
				s = index (buf, '\0');
			} else {
				DBG(DEBUG_TAB, fprintf(stderr,
					"libmount: %s: %d: missing newline at line\n",
					tb->filename, tb->nlines));
				goto err;
			}
		}
		*s = '\0';
		if (--s >= buf && *s == '\r')
			*s = '\0';
		s = skip_spaces(buf);
	} while (*s == '\0' || *s == '#');

	DBG(DEBUG_TAB, fprintf(stderr, "libmount: %s:%d: %s\n",
		tb->filename, tb->nlines, s));

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
			goto err;
	}

	fs->lineno = tb->nlines;

	DBG(DEBUG_TAB, fprintf(stderr,
		"libmount: %s: %d: SOURCE:%s, MNTPOINT:%s, TYPE:%s, "
				  "OPTS:%s, FREQ:%d, PASSNO:%d\n",
		tb->filename, fs->lineno,
		fs->source, fs->target, fs->fstype,
		fs->optstr, fs->freq, fs->passno));

	return 0;
err:
	/* we don't report parse errors to caller; caller has to check
	 * errors by mnt_tab_get_nerrs() or internaly by MNT_ENTRY_ERR flag
	 */
	fs->lineno = tb->nlines;
	fs->flags |= MNT_FS_ERROR;
	return 0;
}

/**
 * mnt_tab_parse_file:
 * @tb: tab pointer
 *
 * Parses whole table (e.g. /etc/fstab).
 *
 * <informalexample>
 *   <programlisting>
 *	mnt_tab *tb = mnt_new_tab("/etc/fstab");
 *	int rc;
 *
 *	rc = mnt_tab_parse_file(tb);
 *	if (rc) {
 *		if (mnt_tab_get_nerrs(tb)) {             / * parse error * /
 *			mnt_tab_strerror(tb, buf, sizeof(buf));
 *			fprintf(stderr, "%s: %s\n", progname, buf);
 *		} else
 *			perror(mnt_tab_get_name(tb));  / * system error * /
 *	} else
 *		mnt_fprintf_tab(tb, stdout, NULL);
 *
 *	mnt_free_tab(tb);
 *   </programlisting>
 * </informalexample>
 *
 * Returns: 0 on success and -1 in case of error. The parse errors is possible
 * to detect by mnt_tab_get_nerrs() and error message is possible to create by
 * mnt_tab_strerror().
 */
int mnt_tab_parse_file(mnt_tab *tb)
{
	FILE *f;

	assert(tb);
	assert(tb->filename);

	if (!tb->filename)
		return -1;

	f = fopen(tb->filename, "r");
	if (!f)
		return -1;

	while (!feof(f)) {
		int rc;
		mnt_fs *fs = mnt_new_fs();
		if (!fs)
			goto error;

		rc = mnt_tab_parse_next(tb, f, fs);
		if (!rc)
			rc = mnt_tab_add_fs(tb, fs);
		else if (feof(f)) {
			mnt_free_fs(fs);
			break;
		}
		if (rc) {
			mnt_free_fs(fs);
			goto error;
		}
	}

	fclose(f);
	return 0;
error:
	fclose(f);
	return -1;
}

/**
 * mnt_new_tab_parse:
 * @filename: /etc/{m,fs}tab or /proc/self/mountinfo path
 *
 * Same as mnt_new_tab() + mnt_tab_parse_file(). Note that this function does
 * not provide details (by mnt_tab_strerror()) about failed parsing -- so you
 * should not to use this function for user-writeable files like /etc/fstab.
 *
 * Returns: newly allocated tab on success and NULL in case of error.
 */
mnt_tab *mnt_new_tab_from_file(const char *filename)
{
	mnt_tab *tb;

	assert(filename);

	if (!filename)
		return NULL;
	tb = mnt_new_tab(filename);
	if (tb && mnt_tab_parse_file(tb) != 0) {
		mnt_free_tab(tb);
		tb = NULL;
	}
	return tb;
}

/**
 * mnt_tab_get_nerrs:
 * @tb: pointer to table
 *
 * Returns: number of broken (parse error) entries in the table.
 */
int mnt_tab_get_nerrs(mnt_tab *tb)
{
	assert(tb);
	return tb->nerrs;
}

/**
 * mnt_tab_strerror:
 * @tb: pointer to table
 * @buf: buffer to return error message
 * @buflen: lenght of the buf
 *
 * Returns: error message for table (file) parse errors. For example:
 *
 *	"/etc/fstab: parse error at line(s): 1, 2 and 3."
 */
char *mnt_tab_strerror(mnt_tab *tb, char *buf, size_t buflen)
{
	struct list_head *p;
	int last = -1;
	char *b = buf;
	char *end = buf + buflen - 1;

	assert(tb);
	assert(buf);
	assert(buflen);

	if (!tb || !tb->nerrs || !buf || buflen <=0)
		return NULL;

	if (tb->filename) {
		snprintf(b, end - b, "%s: ", tb->filename);
		b += strnlen(b, end - b);
	}

	if (tb->nerrs > 1)
		strncpy(b, _("parse error at lines: "), end - b);
	else
		strncpy(b, _("parse error at line: "), end - b);
	b += strnlen(b, end - b);
	*b = '\0';

	list_for_each(p, &tb->ents) {
		mnt_fs *fs = list_entry(p, mnt_fs, ents);
		if (b == end)
			goto done;
		if (fs->flags & MNT_FS_ERROR) {
			if (last != -1) {
				snprintf(b, end - b, "%d, ", last);
				b += strnlen(b, end - b);
			}
			last = fs->lineno;
		}
	}

	if (tb->nerrs == 1)
		snprintf(b, end - b, "%d.", last);
	else
		snprintf(b - 1, end - b, _(" and %d."), last);
done:
	return buf;
}

