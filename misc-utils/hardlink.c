/* hardlink.c - Link multiple identical files together
 *
 * Copyright (C) 2008 - 2014 Julian Andres Klode <jak@jak-linux.org>
 * Copyright (C) 2021 Karel Zak <kzak@redhat.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#define _POSIX_C_SOURCE 200112L	/* POSIX functions */
#define _XOPEN_SOURCE      600	/* nftw() */

#include <sys/types.h>		/* stat */
#include <sys/stat.h>		/* stat */
#include <sys/time.h>		/* getrlimit, getrusage */
#include <sys/resource.h>	/* getrlimit, getrusage */
#include <fcntl.h>		/* posix_fadvise */
#include <ftw.h>		/* ftw */
#include <search.h>		/* tsearch() and friends */
#include <signal.h>		/* SIG*, sigaction */
#include <getopt.h>		/* getopt_long() */
#include <ctype.h>		/* tolower() */

#include "nls.h"
#include "c.h"
#include "xalloc.h"
#include "strutils.h"
#include "monotonic.h"
#include "optutils.h"

#include <regex.h>		/* regcomp(), regsearch() */

#ifdef HAVE_SYS_XATTR_H
# include <sys/xattr.h>		/* listxattr, getxattr */
#endif

static int quiet;		/* don't print anything */

/**
 * struct file - Information about a file
 * @st:       The stat buffer associated with the file
 * @next:     Next file with the same size
 * @basename: The offset off the basename in the filename
 * @path:     The path of the file
 *
 * This contains all information we need about a file.
 */
struct file {
	struct stat st;
	struct file *next;
	struct link {
		struct link *next;
		int basename;
#if __STDC_VERSION__ >= 199901L
		char path[];
#elif __GNUC__
		char path[0];
#else
		char path[1];
#endif
	} *links;
};

/**
 * enum log_level - Logging levels
 * @JLOG_SUMMARY: Default log level
 * @JLOG_INFO:    Verbose logging (verbose == 1)
 * @JLOG_VERBOSE1:  Verbosity 2
 * @JLOG_VERBOSE2:  Verbosity 3
 */
enum log_level {
	JLOG_SUMMARY,
	JLOG_INFO,
	JLOG_VERBOSE1,
	JLOG_VERBOSE2
};

/**
 * struct statistic - Statistics about the file
 * @started: Whether we are post command-line processing
 * @files: The number of files worked on
 * @linked: The number of files replaced by a hardlink to a master
 * @xattr_comparisons: The number of extended attribute comparisons
 * @comparisons: The number of comparisons
 * @saved: The (exaggerated) amount of space saved
 * @start_time: The time we started at
 */
static struct statistics {
	int started;
	size_t files;
	size_t linked;
	size_t xattr_comparisons;
	size_t comparisons;
	double saved;
	struct timeval start_time;
} stats;

/**
 * struct options - Processed command-line options
 * @include: A linked list of regular expressions for the --include option
 * @exclude: A linked list of regular expressions for the --exclude option
 * @verbosity: The verbosity. Should be one of #enum log_level
 * @respect_mode: Whether to respect file modes (default = TRUE)
 * @respect_owner: Whether to respect file owners (uid, gid; default = TRUE)
 * @respect_name: Whether to respect file names (default = FALSE)
 * @respect_time: Whether to respect file modification times (default = TRUE)
 * @respect_xattrs: Whether to respect extended attributes (default = FALSE)
 * @maximise: Chose the file with the highest link count as master
 * @minimise: Chose the file with the lowest link count as master
 * @keep_oldest: Choose the file with oldest timestamp as master (default = FALSE)
 * @dry_run: Specifies whether hardlink should not link files (default = FALSE)
 * @min_size: Minimum size of files to consider. (default = 1 byte)
 */
static struct options {
	struct regex_link {
		regex_t preg;
		struct regex_link *next;
	} *include, *exclude;

	signed int verbosity;
	unsigned int respect_mode:1;
	unsigned int respect_owner:1;
	unsigned int respect_name:1;
	unsigned int respect_time:1;
	unsigned int respect_xattrs:1;
	unsigned int maximise:1;
	unsigned int minimise:1;
	unsigned int keep_oldest:1;
	unsigned int dry_run:1;
	uintmax_t min_size;
} opts = {
	/* default setting */
	.respect_mode = TRUE,
	.respect_owner = TRUE,
	.respect_time = TRUE,
	.respect_xattrs = FALSE,
	.keep_oldest = FALSE,
	.min_size = 1
};

/*
 * files
 *
 * A binary tree of files, managed using tsearch(). To see which nodes
 * are considered equal, see compare_nodes()
 */
static void *files;
static void *files_by_ino;

/*
 * last_signal
 *
 * The last signal we received. We store the signal here in order to be able
 * to break out of loops gracefully and to return from our nftw() handler.
 */
static int last_signal;

/**
 * jlog - Logging for hardlink
 * @level: The log level
 * @format: A format string for printf()
 */
__attribute__((format(printf, 2, 3)))
static void jlog(enum log_level level, const char *format, ...)
{
	va_list args;

	if (quiet || level > (unsigned int)opts.verbosity)
		return;

	va_start(args, format);
	vfprintf(stdout, format, args);
	va_end(args);
	fputc('\n', stdout);
}

/**
 * CMP - Compare two numerical values, return 1, 0, or -1
 * @a: First value
 * @b: Second value
 *
 * Used to compare two integers of any size while avoiding overflow.
 */
#define CMP(a, b) ((a) > (b) ? 1 : ((a) < (b) ? -1 : 0))

/**
 * regexec_any - Match against multiple regular expressions
 * @pregs: A linked list of regular expressions
 * @what:  The string to match against
 *
 * Checks whether any of the regular expressions in the list matches the
 * string.
 */
static int regexec_any(struct regex_link *pregs, const char *what)
{
	for (; pregs != NULL; pregs = pregs->next) {
		if (regexec(&pregs->preg, what, 0, NULL, 0) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * compare_nodes - Node comparison function
 * @_a: The first node (a #struct file)
 * @_b: The second node (a #struct file)
 *
 * Compare the two nodes for the binary tree.
 */
static int compare_nodes(const void *_a, const void *_b)
{
	const struct file *a = _a;
	const struct file *b = _b;
	int diff = 0;

	if (diff == 0)
		diff = CMP(a->st.st_dev, b->st.st_dev);
	if (diff == 0)
		diff = CMP(a->st.st_size, b->st.st_size);

	return diff;
}

/**
 * compare_nodes_ino - Node comparison function
 * @_a: The first node (a #struct file)
 * @_b: The second node (a #struct file)
 *
 * Compare the two nodes for the binary tree.
 */
static int compare_nodes_ino(const void *_a, const void *_b)
{
	const struct file *a = _a;
	const struct file *b = _b;
	int diff = 0;

	if (diff == 0)
		diff = CMP(a->st.st_dev, b->st.st_dev);
	if (diff == 0)
		diff = CMP(a->st.st_ino, b->st.st_ino);

	/* If opts.respect_name is used, we will restrict a struct file to
	 * contain only links with the same basename to keep the rest simple.
	 */
	if (diff == 0 && opts.respect_name)
		diff = strcmp(a->links->path + a->links->basename,
			      b->links->path + b->links->basename);

	return diff;
}

/**
 * print_stats - Print statistics to stdout
 */
static void print_stats(void)
{
	struct timeval end = { 0, 0 }, delta = { 0, 0 };
	char *ssz;

	gettime_monotonic(&end);
	timersub(&end, &stats.start_time, &delta);

	jlog(JLOG_SUMMARY, "%-15s %s", _("Mode:"),
	     opts.dry_run ? _("dry-run") : _("real"));
	jlog(JLOG_SUMMARY, "%-15s %zu", _("Files:"), stats.files);
	jlog(JLOG_SUMMARY, _("%-15s %zu files"), _("Linked:"), stats.linked);

#ifdef HAVE_SYS_XATTR_H
	jlog(JLOG_SUMMARY, _("%-15s %zu xattrs"), _("Compared:"),
	     stats.xattr_comparisons);
#endif
	jlog(JLOG_SUMMARY, _("%-15s %zu files"), _("Compared:"),
	     stats.comparisons);

	ssz = size_to_human_string(SIZE_SUFFIX_3LETTER |
				   SIZE_SUFFIX_SPACE |
				   SIZE_DECIMAL_2DIGITS, stats.saved);

	jlog(JLOG_SUMMARY, "%-15s %s", _("Saved:"), ssz);
	free(ssz);

	jlog(JLOG_SUMMARY, _("%-15s %"PRId64".%06"PRId64" seconds"), _("Duration:"),
	     (int64_t)delta.tv_sec, (int64_t)delta.tv_usec);
}

/**
 * handle_interrupt - Handle a signal
 *
 * Returns: %TRUE on SIGINT, SIGTERM; %FALSE on all other signals.
 */
static int handle_interrupt(void)
{
	switch (last_signal) {
	case SIGINT:
	case SIGTERM:
		return TRUE;
	case SIGUSR1:
		print_stats();
		putchar('\n');
		break;
	}

	last_signal = 0;
	return FALSE;
}

#ifdef HAVE_SYS_XATTR_H

/**
 * llistxattr_or_die - Wrapper for llistxattr()
 *
 * This does the same thing as llistxattr() except that it aborts if any error
 * other than "not supported" is detected.
 */
static ssize_t llistxattr_or_die(const char *path, char *list, size_t size)
{
	ssize_t len = llistxattr(path, list, size);

	if (len < 0 && errno != ENOTSUP)
		err(EXIT_FAILURE, _("cannot get xattr names for %s"), path);

	return len;
}

/**
 * lgetxattr_or_die - Wrapper for lgetxattr()
 *
 * This does the same thing as lgetxattr() except that it aborts upon error.
 */
static ssize_t lgetxattr_or_die(const char *path,
			const char *name, void *value, size_t size)
{
	ssize_t len = lgetxattr(path, name, value, size);

	if (len < 0)
		err(EXIT_FAILURE, _("cannot get xattr value of %s for %s"),
		    name, path);

	return len;
}

/**
 * get_xattr_name_count - Count the number of xattr names
 * @names: a non-empty table of concatenated, null-terminated xattr names
 * @len: the total length of the table
 *
 * @Returns the number of xattr names
 */
static int get_xattr_name_count(const char *const names, ssize_t len)
{
	int count = 0;
	const char *name;

	for (name = names; name < (names + len); name += strlen(name) + 1)
		count++;

	return count;
}

/**
 * cmp_xattr_name_ptrs - Compare two pointers to xattr names by comparing
 * the names they point to.
 */
static int cmp_xattr_name_ptrs(const void *ptr1, const void *ptr2)
{
	return strcmp(*(char *const *)ptr1, *(char *const *)ptr2);
}

/**
 * get_sorted_xattr_name_table - Create a sorted table of xattr names.
 * @names - table of concatenated, null-terminated xattr names
 * @n - the number of names
 *
 * @Returns allocated table of pointers to the names, sorted alphabetically
 */
static const char **get_sorted_xattr_name_table(const char *names, int n)
{
	const char **table = xmalloc(n * sizeof(char *));
	int i;

	for (i = 0; i < n; i++) {
		table[i] = names;
		names += strlen(names) + 1;
	}

	qsort(table, n, sizeof(char *), cmp_xattr_name_ptrs);

	return table;
}

/**
 * file_xattrs_equal - Compare the extended attributes of two files
 * @a: The first file
 * @b: The second file
 *
 * @Returns: %TRUE if and only if extended attributes are equal
 */
static int file_xattrs_equal(const struct file *a, const struct file *b)
{
	ssize_t len_a;
	ssize_t len_b;
	char *names_a = NULL;
	char *names_b = NULL;
	int n_a;
	int n_b;
	const char **name_ptrs_a = NULL;
	const char **name_ptrs_b = NULL;
	void *value_a = NULL;
	void *value_b = NULL;
	int ret = FALSE;
	int i;

	assert(a->links != NULL);
	assert(b->links != NULL);

	jlog(JLOG_VERBOSE1, _("Comparing xattrs of %s to %s"), a->links->path,
	     b->links->path);

	stats.xattr_comparisons++;

	len_a = llistxattr_or_die(a->links->path, NULL, 0);
	len_b = llistxattr_or_die(b->links->path, NULL, 0);

	if (len_a <= 0 && len_b <= 0)
		return TRUE;	// xattrs not supported or neither file has any

	if (len_a != len_b)
		return FALSE;	// total lengths of xattr names differ

	names_a = xmalloc(len_a);
	names_b = xmalloc(len_b);

	len_a = llistxattr_or_die(a->links->path, names_a, len_a);
	len_b = llistxattr_or_die(b->links->path, names_b, len_b);
	assert((len_a > 0) && (len_a == len_b));

	n_a = get_xattr_name_count(names_a, len_a);
	n_b = get_xattr_name_count(names_b, len_b);

	if (n_a != n_b)
		goto exit;	// numbers of xattrs differ

	name_ptrs_a = get_sorted_xattr_name_table(names_a, n_a);
	name_ptrs_b = get_sorted_xattr_name_table(names_b, n_b);

	// We now have two sorted tables of xattr names.

	for (i = 0; i < n_a; i++) {
		if (handle_interrupt())
			goto exit;	// user wants to quit

		if (strcmp(name_ptrs_a[i], name_ptrs_b[i]) != 0)
			goto exit;	// names at same slot differ

		len_a =
		    lgetxattr_or_die(a->links->path, name_ptrs_a[i], NULL, 0);
		len_b =
		    lgetxattr_or_die(b->links->path, name_ptrs_b[i], NULL, 0);

		if (len_a != len_b)
			goto exit;	// xattrs with same name, different value lengths

		value_a = xmalloc(len_a);
		value_b = xmalloc(len_b);

		len_a = lgetxattr_or_die(a->links->path, name_ptrs_a[i],
					 value_a, len_a);
		len_b = lgetxattr_or_die(b->links->path, name_ptrs_b[i],
					 value_b, len_b);
		assert((len_a >= 0) && (len_a == len_b));

		if (memcmp(value_a, value_b, len_a) != 0)
			goto exit;	// xattrs with same name, different values

		free(value_a);
		free(value_b);
		value_a = NULL;
		value_b = NULL;
	}

	ret = TRUE;

 exit:
	free(names_a);
	free(names_b);
	free(name_ptrs_a);
	free(name_ptrs_b);
	free(value_a);
	free(value_b);
	return ret;
}
#else /* !HAVE_SYS_XATTR_H */
static int file_xattrs_equal(const struct file *a, const struct file *b)
{
	return TRUE;
}
#endif /* HAVE_SYS_XATTR_H */

/**
 * file_contents_equal - Compare contents of two files for equality
 * @a: The first file
 * @b: The second file
 *
 * Compare the contents of the files for equality
 */
static int file_contents_equal(const struct file *a, const struct file *b)
{
	FILE *fa = NULL;
	FILE *fb = NULL;
	char buf_a[8192];
	char buf_b[8192];
	int cmp = 0;		/* zero => equal */
	off_t off = 0;		/* current offset */

	assert(a->links != NULL);
	assert(b->links != NULL);

	jlog(JLOG_VERBOSE1, _("Comparing %s to %s"), a->links->path,
	     b->links->path);

	stats.comparisons++;

	if ((fa = fopen(a->links->path, "rb")) == NULL)
		goto err;
	if ((fb = fopen(b->links->path, "rb")) == NULL)
		goto err;

#if defined(POSIX_FADV_SEQUENTIAL) && defined(HAVE_POSIX_FADVISE)
	posix_fadvise(fileno(fa), 0, 0, POSIX_FADV_SEQUENTIAL);
	posix_fadvise(fileno(fb), 0, 0, POSIX_FADV_SEQUENTIAL);
#endif

	while (!handle_interrupt() && cmp == 0) {
		size_t ca;
		size_t cb;

		ca = fread(buf_a, 1, sizeof(buf_a), fa);
		if (ca < sizeof(buf_a) && ferror(fa))
			goto err;

		cb = fread(buf_b, 1, sizeof(buf_b), fb);
		if (cb < sizeof(buf_b) && ferror(fb))
			goto err;

		off += ca;

		if ((ca != cb || ca == 0)) {
			cmp = CMP(ca, cb);
			break;
		}
		cmp = memcmp(buf_a, buf_b, ca);
	}
 out:
	if (fa != NULL)
		fclose(fa);
	if (fb != NULL)
		fclose(fb);
	return !handle_interrupt() && cmp == 0;
 err:
	if (fa == NULL || fb == NULL)
		warn(_("cannot open %s"), fa ? b->links->path : a->links->path);
	else
		warn(_("cannot read %s"),
		     ferror(fa) ? a->links->path : b->links->path);
	cmp = 1;
	goto out;
}

/**
 * file_may_link_to - Check whether a file may replace another one
 * @a: The first file
 * @b: The second file
 *
 * Check whether the two fies are considered equal and can be linked
 * together. If the two files are identical, the result will be FALSE,
 * as replacing a link with an identical one is stupid.
 */
static int file_may_link_to(const struct file *a, const struct file *b)
{
	return (a->st.st_size != 0 &&
		a->st.st_size == b->st.st_size &&
		a->links != NULL && b->links != NULL &&
		a->st.st_dev == b->st.st_dev &&
		a->st.st_ino != b->st.st_ino &&
		(!opts.respect_mode || a->st.st_mode == b->st.st_mode) &&
		(!opts.respect_owner || a->st.st_uid == b->st.st_uid) &&
		(!opts.respect_owner || a->st.st_gid == b->st.st_gid) &&
		(!opts.respect_time || a->st.st_mtime == b->st.st_mtime) &&
		(!opts.respect_name
		 || strcmp(a->links->path + a->links->basename,
			   b->links->path + b->links->basename) == 0) &&
		(!opts.respect_xattrs || file_xattrs_equal(a, b)) &&
		file_contents_equal(a, b));
}

/**
 * file_compare - Compare two files to decide which should be master
 * @a: The first file
 * @b: The second file
 *
 * Check which of the files should be considered greater and thus serve
 * as the master when linking (the master is the file that all equal files
 * will be replaced with).
 */
static int file_compare(const struct file *a, const struct file *b)
{
	int res = 0;
	if (a->st.st_dev == b->st.st_dev && a->st.st_ino == b->st.st_ino)
		return 0;

	if (res == 0 && opts.maximise)
		res = CMP(a->st.st_nlink, b->st.st_nlink);
	if (res == 0 && opts.minimise)
		res = CMP(b->st.st_nlink, a->st.st_nlink);
	if (res == 0)
		res = opts.keep_oldest ? CMP(b->st.st_mtime, a->st.st_mtime)
		    : CMP(a->st.st_mtime, b->st.st_mtime);
	if (res == 0)
		res = CMP(b->st.st_ino, a->st.st_ino);

	return res;
}

/**
 * file_link - Replace b with a link to a
 * @a: The first file
 * @b: The second file
 *
 * Link the file, replacing @b with the current one. The file is first
 * linked to a temporary name, and then renamed to the name of @b, making
 * the replace atomic (@b will always exist).
 */
static int file_link(struct file *a, struct file *b)
{
	char *ssz;

 file_link:
	assert(a->links != NULL);
	assert(b->links != NULL);

	ssz = size_to_human_string(SIZE_SUFFIX_3LETTER |
				   SIZE_SUFFIX_SPACE |
				   SIZE_DECIMAL_2DIGITS, a->st.st_size);
	jlog(JLOG_INFO, _("%sLinking %s to %s (-%s)"),
	     opts.dry_run ? _("[DryRun] ") : "", a->links->path, b->links->path,
	     ssz);
	free(ssz);

	if (!opts.dry_run) {
		size_t len =
		    strlen(b->links->path) + strlen(".hardlink-temporary") + 1;
		char *new_path = xmalloc(len);

		snprintf(new_path, len, "%s.hardlink-temporary",
			 b->links->path);

		if (link(a->links->path, new_path) != 0) {
			warn(_("cannot link %s to %s"), a->links->path,
			     new_path);
			free(new_path);
			return FALSE;
		} else if (rename(new_path, b->links->path) != 0) {
			warn(_("cannot rename %s to %s"), a->links->path,
			     new_path);
			unlink(new_path);	/* cleanup failed rename */
			free(new_path);
			return FALSE;
		}
		free(new_path);
	}

	/* Update statistics */
	stats.linked++;

	/* Increase the link count of this file, and set stat() of other file */
	a->st.st_nlink++;
	b->st.st_nlink--;

	if (b->st.st_nlink == 0)
		stats.saved += a->st.st_size;

	/* Move the link from file b to a */
	{
		struct link *new_link = b->links;

		b->links = b->links->next;
		new_link->next = a->links->next;
		a->links->next = new_link;
	}

	/* Do it again */
	if (b->links)
		goto file_link;

	return TRUE;
}

/**
 * inserter - Callback function for nftw()
 * @fpath: The path of the file being visited
 * @sb:    The stat information of the file
 * @typeflag: The type flag
 * @ftwbuf:   Contains current level of nesting and offset of basename
 *
 * Called by nftw() for the files. See the manual page for nftw() for
 * further information.
 */
static int inserter(const char *fpath, const struct stat *sb,
		    int typeflag, struct FTW *ftwbuf)
{
	struct file *fil;
	struct file **node;
	size_t pathlen;
	int included;
	int excluded;

	if (handle_interrupt())
		return 1;
	if (typeflag == FTW_DNR || typeflag == FTW_NS)
		warn(_("cannot read %s"), fpath);
	if (typeflag != FTW_F || !S_ISREG(sb->st_mode))
		return 0;

	included = regexec_any(opts.include, fpath);
	excluded = regexec_any(opts.exclude, fpath);

	if ((opts.exclude && excluded && !included) ||
	    (!opts.exclude && opts.include && !included))
		return 0;

	stats.files++;

	if ((uintmax_t) sb->st_size < opts.min_size) {
		jlog(JLOG_VERBOSE1,
		     _("Skipped %s (smaller than configured size)"), fpath);
		return 0;
	}

	jlog(JLOG_VERBOSE2, _("Visiting %s (file %zu)"), fpath, stats.files);

	pathlen = strlen(fpath) + 1;

	fil = xcalloc(1, sizeof(*fil));
	fil->links = xcalloc(1, sizeof(struct link) + pathlen);

	fil->st = *sb;
	fil->links->basename = ftwbuf->base;
	fil->links->next = NULL;

	memcpy(fil->links->path, fpath, pathlen);

	node = tsearch(fil, &files_by_ino, compare_nodes_ino);

	if (node == NULL)
		goto fail;

	if (*node != fil) {
		/* Already known inode, add link to inode information */
		assert((*node)->st.st_dev == sb->st_dev);
		assert((*node)->st.st_ino == sb->st_ino);

		fil->links->next = (*node)->links;
		(*node)->links = fil->links;

		free(fil);
	} else {
		/* New inode, insert into by-size table */
		node = tsearch(fil, &files, compare_nodes);

		if (node == NULL)
			goto fail;

		if (*node != fil) {
			struct file *l;

			if (file_compare(fil, *node) >= 0) {
				fil->next = *node;
				*node = fil;
			} else {
				for (l = *node; l != NULL; l = l->next) {
					if (l->next != NULL
					    && file_compare(fil, l->next) < 0)
						continue;

					fil->next = l->next;
					l->next = fil;

					break;
				}
			}
		}
	}

	return 0;

 fail:
	warn(_("cannot continue"));	/* probably ENOMEM */
	return 0;
}

/**
 * visitor - Callback for twalk()
 * @nodep: Pointer to a pointer to a #struct file
 * @which: At which point this visit is (preorder, postorder, endorder)
 * @depth: The depth of the node in the tree
 *
 * Visit the nodes in the binary tree. For each node, call hardlinker()
 * on each #struct file in the linked list of #struct file instances located
 * at that node.
 */
static void visitor(const void *nodep, const VISIT which, const int depth)
{
	struct file *master = *(struct file **)nodep;
	struct file *other;

	(void)depth;

	if (which != leaf && which != endorder)
		return;

	for (; master != NULL; master = master->next) {
		if (handle_interrupt())
			exit(EXIT_FAILURE);
		if (master->links == NULL)
			continue;

		for (other = master->next; other != NULL; other = other->next) {
			if (handle_interrupt())
				exit(EXIT_FAILURE);

			assert(other != other->next);
			assert(other->st.st_size == master->st.st_size);

			if (other->links == NULL
			    || !file_may_link_to(master, other))
				continue;

			if (!file_link(master, other) && errno == EMLINK)
				master = other;
		}
	}
}

/**
 * usage - Print the program help and exit
 */
static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;

	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] <directory>|<file> ...\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Consolidate duplicate files using hardlinks.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -v, --verbose              verbose output (repeat for more verbosity)\n"), out);
	fputs(_(" -q, --quiet                quiet mode - don't print anything\n"), out);
	fputs(_(" -n, --dry-run              don't actually link anything\n"), out);
	fputs(_(" -f, --respect-name         filenames have to be identical\n"), out);
	fputs(_(" -p, --ignore-mode          ignore changes of file mode\n"), out);
	fputs(_(" -o, --ignore-owner         ignore owner changes\n"), out);
	fputs(_(" -t, --ignore-time          ignore timestamps (when testing for equality)\n"), out);
#ifdef HAVE_SYS_XATTR_H
	fputs(_(" -X, --respect-xattrs       respect extended attributes\n"), out);
#endif
	fputs(_(" -m, --maximize             maximize the hardlink count, remove the file with\n"
	        "                              lowest hardlink count\n"), out);
	fputs(_(" -M, --minimize             reverse the meaning of -m\n"), out);
	fputs(_(" -O, --keep-oldest          keep the oldest file of multiple equal files\n"
		"                              (lower precedence than minimize/maximize)\n"), out);
	fputs(_(" -x, --exclude <regex>      regular expression to exclude files\n"), out);
	fputs(_(" -i, --include <regex>      regular expression to include files/dirs\n"), out);
	fputs(_(" -s, --minimum-size <size>  minimum size for files.\n"), out);
	fputs(_(" -c, --content              compare only file contents, same as -pot\n"), out);

	fputs(USAGE_SEPARATOR, out);
	printf(USAGE_HELP_OPTIONS(28));
	printf(USAGE_MAN_TAIL("hardlink(1)"));

	exit(EXIT_SUCCESS);
}

/**
 * register_regex - Compile and insert a regular expression into list
 * @pregs: Pointer to a linked list of regular expressions
 * @regex: String containing the regular expression to be compiled
 */
static void register_regex(struct regex_link **pregs, const char *regex)
{
	struct regex_link *link;
	int err;

	link = xmalloc(sizeof(*link));

	if ((err = regcomp(&link->preg, regex, REG_NOSUB | REG_EXTENDED)) != 0) {
		size_t size = regerror(err, &link->preg, NULL, 0);
		char *buf = xmalloc(size + 1);

		regerror(err, &link->preg, buf, size);

		errx(EXIT_FAILURE, _("could not compile regular expression %s: %s"),
				regex, buf);
	}
	link->next = *pregs; *pregs = link;
}

/**
 * parse_options - Parse the command line options
 * @argc: Number of options
 * @argv: Array of options
 */
static int parse_options(int argc, char *argv[])
{
	static const char optstr[] = "VhvnfpotXcmMOx:i:s:q";
	static const struct option long_options[] = {
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{"verbose", no_argument, NULL, 'v'},
		{"dry-run", no_argument, NULL, 'n'},
		{"respect-name", no_argument, NULL, 'f'},
		{"ignore-mode", no_argument, NULL, 'p'},
		{"ignore-owner", no_argument, NULL, 'o'},
		{"ignore-time", no_argument, NULL, 't'},
		{"respect-xattrs", no_argument, NULL, 'X'},
		{"maximize", no_argument, NULL, 'm'},
		{"minimize", no_argument, NULL, 'M'},
		{"keep-oldest", no_argument, NULL, 'O'},
		{"exclude", required_argument, NULL, 'x'},
		{"include", required_argument, NULL, 'i'},
		{"minimum-size", required_argument, NULL, 's'},
		{"content", no_argument, NULL, 'c'},
		{"quiet", no_argument, NULL, 'q'},
		{NULL, 0, NULL, 0}
	};
	static const ul_excl_t excl[] = {
		{'q', 'v'},
		{0}
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;
	int c;

	while ((c = getopt_long(argc, argv, optstr, long_options, NULL)) != -1) {

		err_exclusive_options(c, long_options, excl, excl_st);

		switch (c) {
		case 'p':
			opts.respect_mode = FALSE;
			break;
		case 'o':
			opts.respect_owner = FALSE;
			break;
		case 't':
			opts.respect_time = FALSE;
			break;
		case 'X':
			opts.respect_xattrs = TRUE;
			break;
		case 'm':
			opts.maximise = TRUE;
			break;
		case 'M':
			opts.minimise = TRUE;
			break;
		case 'O':
			opts.keep_oldest = TRUE;
			break;
		case 'f':
			opts.respect_name = TRUE;
			break;
		case 'v':
			opts.verbosity++;
			break;
		case 'q':
			quiet = TRUE;
			break;
		case 'c':
			opts.respect_mode = FALSE;
			opts.respect_name = FALSE;
			opts.respect_owner = FALSE;
			opts.respect_time = FALSE;
			opts.respect_xattrs = FALSE;
			break;
		case 'n':
			opts.dry_run = 1;
			break;
		case 'x':
			register_regex(&opts.exclude, optarg);
			break;
		case 'i':
			register_regex(&opts.include, optarg);
			break;
		case 's':
			opts.min_size = strtosize_or_err(optarg, _("failed to parse size"));
			break;
		case 'h':
			usage();
		case 'V':
			print_version(EXIT_SUCCESS);
		default:
			errtryhelp(EXIT_FAILURE);}
	}

	return 0;
}

/**
* to_be_called_atexit - Cleanup handler, also prints statistics.
*/
static void to_be_called_atexit(void)
{
	if (stats.started)
		print_stats();
}

/**
* sighandler - Signal handler, sets the global last_signal variable
* @i: The signal number
*/
static void sighandler(int i)
{
	if (last_signal != SIGINT)
		last_signal = i;
	if (i == SIGINT)
		putchar('\n');
}

int main(int argc, char *argv[])
{
	struct sigaction sa;

	sa.sa_handler = sighandler;
	sa.sa_flags = SA_RESTART;
	sigfillset(&sa.sa_mask);

	/* If we receive a SIGINT, end the processing */
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);

	/* Pretty print numeric output */
	setlocale(LC_NUMERIC, "");

	if (atexit(to_be_called_atexit) != 0)
		err(EXIT_FAILURE, _("cannot register exit handler"));

	parse_options(argc, argv);

	if (optind == argc)
		errx(EXIT_FAILURE, _("no directory or file specified"));

	gettime_monotonic(&stats.start_time);
	stats.started = TRUE;

	for (; optind < argc; optind++) {
		if (nftw(argv[optind], inserter, 20, FTW_PHYS) == -1)
			warn(_("cannot process %s"), argv[optind]);
	}

	twalk(files, visitor);
	return 0;
}
