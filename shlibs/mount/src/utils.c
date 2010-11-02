/*
 * Copyright (C) 2008-2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: utils
 * @title: Utils
 * @short_description: misc utils.
 */
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#else
#define PR_GET_DUMPABLE 3
#endif
#if (!defined(HAVE_PRCTL) && defined(linux))
#include <sys/syscall.h>
#endif
#include <sys/stat.h>
#include <ctype.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>

#include "strutils.h"
#include "pathnames.h"
#include "mountP.h"
#include "mangle.h"
#include "canonicalize.h"

char *mnt_getenv_safe(const char *arg)
{
	if ((getuid() != geteuid()) || (getgid() != getegid()))
		return NULL;
#if HAVE_PRCTL
	if (prctl(PR_GET_DUMPABLE, 0, 0, 0, 0) == 0)
		return NULL;
#else
#if (defined(linux) && defined(SYS_prctl))
	if (syscall(SYS_prctl, PR_GET_DUMPABLE, 0, 0, 0, 0) == 0)
		return NULL;
#endif
#endif

#ifdef HAVE___SECURE_GETENV
	return __secure_getenv(arg);
#else
	return getenv(arg);
#endif
}

int endswith(const char *s, const char *sx)
{
	ssize_t off;

	assert(s);
	assert(sx);

	off = strlen(s);
	if (!off)
		return 0;
	off -= strlen(sx);
	if (off < 0)
		return 0;

        return !strcmp(s + off, sx);
}

int startswith(const char *s, const char *sx)
{
	size_t off;

	assert(s);
	assert(sx);

	off = strlen(sx);
	if (!off)
		return 0;

        return !strncmp(s, sx, off);
}

/**
 * mnt_mangle:
 * @str: string
 *
 * Encode @str to be compatible with fstab/mtab
 *
 * Returns: new allocated string or NULL in case of error.
 */
char *mnt_mangle(const char *str)
{
	return mangle(str);
}

/**
 * mnt_unmangle:
 * @str: string
 *
 * Decode @str from fstab/mtab
 *
 * Returns: new allocated string or NULL in case of error.
 */
char *mnt_unmangle(const char *str)
{
	return unmangle(str);
}

/**
 * mnt_fstype_is_pseudofs:
 * @type: filesystem name
 *
 * Returns: 1 for filesystems like proc, sysfs, ... or 0.
 */
int mnt_fstype_is_pseudofs(const char *type)
{
	if (!type)
		return 0;
	if (strcmp(type, "none")  == 0 ||
	    strcmp(type, "proc")  == 0 ||
	    strcmp(type, "tmpfs") == 0 ||
	    strcmp(type, "sysfs") == 0 ||
	    strcmp(type, "devpts") == 0||
	    strcmp(type, "cgroups") == 0 ||
	    strcmp(type, "devfs") == 0 ||
	    strcmp(type, "dlmfs") == 0 ||
	    strcmp(type, "cpuset") == 0 ||
	    strcmp(type, "securityfs") == 0 ||
	    strcmp(type, "rpc_pipefs") == 0 ||
	    strcmp(type, "fusectl") == 0 ||
	    strcmp(type, "binfmt_misc") == 0 ||
	    strcmp(type, "fuse.gvfs-fuse-daemon") == 0 ||
	    strcmp(type, "debugfs") == 0 ||
	    strcmp(type, "spufs") == 0)
		return 1;
	return 0;
}

/**
 * mnt_fstype_is_netfs:
 * @type: filesystem name
 *
 * Returns: 1 for filesystems like cifs, nfs, ... or 0.
 */
int mnt_fstype_is_netfs(const char *type)
{
	if (!type)
		return 0;
	if (strcmp(type, "cifs")  == 0 ||
	    strcmp(type, "smbfs")  == 0 ||
	    strncmp(type, "nfs", 3) == 0 ||
	    strcmp(type, "afs") == 0 ||
	    strcmp(type, "ncpfs") == 0)
		return 1;
	return 0;
}

/**
 * mnt_match_fstype:
 * @type: filesystem type
 * @pattern: filesystem name or comma delimitted list of names
 *
 * The @pattern list of filesystem can be prefixed with a global
 * "no" prefix to invert matching of the whole list. The "no" could
 * also used for individual items in the @pattern list. So,
 * "nofoo,bar" has the same meaning as "nofoo,nobar".
 *
 * "bar"  : "nofoo,bar"		-> False   (global "no" prefix)
 *
 * "bar"  : "foo,bar"		-> True
 *
 * "bar" : "foo,nobar"		-> False
 *
 * Returns: 1 if type is matching, else 0. This function also returns
 *          0 if @pattern is NULL and @type is non-NULL.
 */
int mnt_match_fstype(const char *type, const char *pattern)
{
	int no = 0;		/* negated types list */
	int len;
	const char *p;

	if (!pattern && !type)
		return 1;
	if (!pattern)
		return 0;

	if (!strncmp(pattern, "no", 2)) {
		no = 1;
		pattern += 2;
	}

	/* Does type occur in types, separated by commas? */
	len = strlen(type);
	p = pattern;
	while(1) {
		if (!strncmp(p, "no", 2) && !strncmp(p+2, type, len) &&
		    (p[len+2] == 0 || p[len+2] == ','))
			return 0;
		if (strncmp(p, type, len) == 0 && (p[len] == 0 || p[len] == ','))
			return !no;
		p = strchr(p,',');
		if (!p)
			break;
		p++;
	}
	return no;
}


/* Returns 1 if needle found or noneedle not found in haystack
 * Otherwise returns 0
 */
static int check_option(const char *haystack, size_t len,
			const char *needle, size_t needle_len)
{
	const char *p;
	int no = 0;

	if (needle_len >= 2 && !strncmp(needle, "no", 2)) {
		no = 1;
		needle += 2;
		needle_len -= 2;
	}

	for (p = haystack; p && p < haystack + len; p++) {
		char *sep = strchr(p, ',');
		size_t plen = sep ? sep - p : len - (p - haystack);

		if (plen == needle_len) {
			if (!strncmp(p, needle, plen))
				return !no;	/* foo or nofoo was found */
		}
		p += plen;
	}

	return no;  /* foo or nofoo was not found */
}

/**
 * mnt_match_options:
 * @optstr: options string
 * @pattern: comma delimitted list of options
 *
 * The "no" could used for individual items in the @options list. The "no"
 * prefix does not have a global meanning.
 *
 * Unlike fs type matching, nonetdev,user and nonetdev,nouser have
 * DIFFERENT meanings; each option is matched explicitly as specified.
 *
 * "xxx,yyy,zzz" : "nozzz"	-> False
 *
 * "xxx,yyy,zzz" : "xxx,noeee"	-> True
 *
 * Returns: 1 if pattern is matching, else 0. This function also returns 0
 *          if @pattern is NULL and @optstr is non-NULL.
 */
int mnt_match_options(const char *optstr, const char *pattern)
{
	const char *p;
	size_t len, optstr_len = 0;

	if (!pattern && !optstr)
		return 1;
	if (!pattern)
		return 0;

	len = strlen(pattern);
	if (optstr)
		optstr_len = strlen(optstr);

	for (p = pattern; p < pattern + len; p++) {
		char *sep = strchr(p, ',');
		size_t plen = sep ? sep - p : len - (p - pattern);

		if (!plen)
			continue; /* if two ',' appear in a row */

		if (!check_option(optstr, optstr_len, p, plen))
			return 0; /* any match failure means failure */

		p += plen;
	}

	/* no match failures in list means success */
	return 1;
}

/*
 * Returns allocated string with username or NULL.
 */
char *mnt_get_username(const uid_t uid)
{
        struct passwd pwd;
	struct passwd *res;
	size_t sz = sysconf(_SC_GETPW_R_SIZE_MAX);
	char *buf, *username = NULL;

	if (sz <= 0)
		sz = 16384;        /* Should be more than enough */

	buf = malloc(sz);
	if (!buf)
		return NULL;

	if (!getpwuid_r(uid, &pwd, buf, sz, &res) && res)
		username = strdup(pwd.pw_name);

	free(buf);
	return username;
}

int mnt_get_uid(const char *username, uid_t *uid)
{
	int rc = -1;
        struct passwd pwd;
	struct passwd *pw;
	size_t sz = sysconf(_SC_GETPW_R_SIZE_MAX);
	char *buf;

	if (!username || !uid)
		return -EINVAL;
	if (sz <= 0)
		sz = 16384;        /* Should be more than enough */

	buf = malloc(sz);
	if (!buf)
		return -ENOMEM;

	if (!getpwnam_r(username, &pwd, buf, sz, &pw) && pw) {
		*uid= pw->pw_uid;
		rc = 0;
	} else {
		DBG(UTILS, mnt_debug(
			"cannot convert '%s' username to UID", username));
	}

	free(buf);
	return rc;
}

int mnt_get_gid(const char *groupname, gid_t *gid)
{
	int rc = -1;
        struct group grp;
	struct group *gr;
	size_t sz = sysconf(_SC_GETGR_R_SIZE_MAX);
	char *buf;

	if (!groupname || !gid)
		return -EINVAL;
	if (sz <= 0)
		sz = 16384;        /* Should be more than enough */

	buf = malloc(sz);
	if (!buf)
		return -ENOMEM;

	if (!getgrnam_r(groupname, &grp, buf, sz, &gr) && gr) {
		*gid= gr->gr_gid;
		rc = 0;
	} else {
		DBG(UTILS, mnt_debug(
			"cannot convert '%s' groupname to GID", groupname));
	}

	free(buf);
	return rc;
}

int mnt_in_group(gid_t gid)
{
	int rc = 0, n, i;
	gid_t *grps = NULL;

	if (getgid() == gid)
		return 1;

	n = getgroups(0, NULL);
	if (n <= 0)
		goto done;

	grps = malloc(n * sizeof(*grps));
	if (!grps)
		goto done;

	if (getgroups(n, grps) == n) {
		for (i = 0; i < n; i++) {
			if (grps[i] == gid) {
				rc = 1;
				break;
			}
		}
	}
done:
	free(grps);
	return rc;
}

/*
 * Returns 1 if /etc/mtab is a reqular file.
 */
int mnt_has_regular_mtab(const char **mtab, int *writeable)
{
	struct stat st;
	int rc;
	const char *x = mtab && *mtab ? *mtab : mnt_get_mtab_path();

	if (mtab && !*mtab)
		*mtab = x;
	rc = (lstat(x, &st) == 0 && S_ISREG(st.st_mode));

	if (writable) {
		if (rc) {
			/* TODO: use utimensat() */
			int fd = open(path, O_RDWR, 0644);
			if (fd >= 0) {
				close(fd);
				*writable = 1;
				return rc;
			}
		}
		*writable = 0;
	}
	return rc;
}

/**
 * mnt_get_fstab_path:
 *
 * Returns: path to /etc/fstab or $LIBMOUNT_FSTAB.
 */
const char *mnt_get_fstab_path(void)
{
	const char *p = mnt_getenv_safe("LIBMOUNT_FSTAB");
	return p ? : _PATH_MNTTAB;
}

/**
 * mnt_get_mtab_path:
 *
 * This function returns *default* location of the mtab file. The result does
 * not have to be writable. See also mnt_get_writable_mtab_path().
 *
 * Returns: path to /etc/mtab or $LIBMOUNT_MTAB.
 */
const char *mnt_get_mtab_path(void)
{
	const char *p = mnt_getenv_safe("LIBMOUNT_MTAB");
	return p ? : _PATH_MOUNTED;
}

/**
 * mnt_get_utab_path:
 *
 * This function returns *default* location of the utab file.
 *
 * Returns: path to /dev/.mount/utab or $LIBMOUNT_UTAB.
 */
const char *mnt_get_utab_path(void)
{
	const char *p = mnt_getenv_safe("LIBMOUNT_UTAB");
	return p ? : MNT_PATH_UTAB;
}


/* returns file descriptor or -errno, @name returns uniques filename
 */
int mnt_open_uniq_filename(const char *filename, char **name, int flags)
{
	int rc, fd;
	char *n;

	assert(filename);

	if (name)
		*name = NULL;

	rc = asprintf(&n, "%s.XXXXXX", filename);
	if (rc <= 0)
		return -errno;

	fd = mkostemp(n, flags | O_EXCL);
	if (fd >= 0 && name)
		*name = n;
	else
		free(n);

	return fd < 0 ? -errno : fd;
}

/* returns basename and keeps dirname in the @path, if @path is "/" (root)
 * then returns empty string */
static char *stripoff_last_component(char *path)
{
	char *p = strrchr(path, '/');

	if (!p)
		return NULL;
	*p = '\0';
	return ++p;
}

char *mnt_get_mountpoint(const char *path)
{
	char *mnt = strdup(path);
	struct stat st;
	dev_t dir, base;

	if (!mnt)
		return NULL;
	if (*mnt == '/' && *(mnt + 1) == '\0')
		goto done;

	if (stat(mnt, &st))
		goto err;
	base = st.st_dev;

	do {
		char *p = stripoff_last_component(mnt);

		if (!p)
			break;
		if (stat(*mnt ? mnt : "/", &st))
			goto err;
		dir = st.st_dev;
		if (dir != base) {
			*(p - 1) = '/';
			goto done;
		}
		base = dir;
	} while (mnt && *(mnt + 1) != '\0');

	memcpy(mnt, "/", 2);
done:
	DBG(UTILS, mnt_debug("fs-root for %s is %s", path, mnt));
	return mnt;
err:
	free(mnt);
	return NULL;
}

char *mnt_get_fs_root(const char *path, const char *mnt)
{
	char *m = (char *) mnt;
	const char *p;
	size_t sz;

	if (!m)
		m = mnt_get_mountpoint(path);
	if (!m)
		return NULL;

	sz = strlen(m);
	p = sz > 1 ? path + sz : path;

	if (m != mnt)
		free(m);

	return *p ? strdup(p) : strdup("/");
}

#ifdef TEST_PROGRAM
int test_match_fstype(struct mtest *ts, int argc, char *argv[])
{
	char *type = argv[1];
	char *pattern = argv[2];

	printf("%s\n", mnt_match_fstype(type, pattern) ? "MATCH" : "NOT-MATCH");
	return 0;
}

int test_match_options(struct mtest *ts, int argc, char *argv[])
{
	char *optstr = argv[1];
	char *pattern = argv[2];

	printf("%s\n", mnt_match_options(optstr, pattern) ? "MATCH" : "NOT-MATCH");
	return 0;
}

int test_startswith(struct mtest *ts, int argc, char *argv[])
{
	char *optstr = argv[1];
	char *pattern = argv[2];

	printf("%s\n", startswith(optstr, pattern) ? "YES" : "NOT");
	return 0;
}

int test_endswith(struct mtest *ts, int argc, char *argv[])
{
	char *optstr = argv[1];
	char *pattern = argv[2];

	printf("%s\n", endswith(optstr, pattern) ? "YES" : "NOT");
	return 0;
}

int test_mountpoint(struct mtest *ts, int argc, char *argv[])
{
	char *path = canonicalize_path(argv[1]),
	     *mnt = path ? mnt_get_mountpoint(path) :  NULL;

	printf("%s: %s\n", argv[1], mnt ? : "unknown");
	free(mnt);
	free(path);
	return 0;
}

int test_fsroot(struct mtest *ts, int argc, char *argv[])
{
	char *path = canonicalize_path(argv[1]),
	     *mnt = path ? mnt_get_fs_root(path, NULL) : NULL;

	printf("%s: %s\n", argv[1], mnt ? : "unknown");
	free(mnt);
	free(path);
	return 0;
}

int main(int argc, char *argv[])
{
	struct mtest tss[] = {
	{ "--match-fstype",  test_match_fstype,    "<type> <pattern>     FS types matching" },
	{ "--match-options", test_match_options,   "<options> <pattern>  options matching" },
	{ "--starts-with",   test_startswith,      "<string> <prefix>" },
	{ "--ends-with",     test_endswith,        "<string> <prefix>" },
	{ "--mountpoint",    test_mountpoint,      "<path>" },
	{ "--fs-root",       test_fsroot,          "<path>" },
	{ NULL }
	};

	return mnt_run_test(tss, argc, argv);
}

#endif /* TEST_PROGRAM */
