/*
 * Copyright (C) 2008-2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
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

#include "mountP.h"

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

/* TODO: move strn<...> functions to top-level lib/strn.c */
#ifndef HAVE_STRNLEN
size_t strnlen(const char *s, size_t maxlen)
{
	int i;

	for (i = 0; i < maxlen; i++) {
		if (s[i] == '\0')
			return i + 1;
	}
	return maxlen;
}
#endif

#ifndef HAVE_STRNCHR
char *strnchr(const char *s, size_t maxlen, int c)
{
	for (; maxlen-- && *s != '\0'; ++s)
		if (*s == (char)c)
			return (char *)s;
	return NULL;
}
#endif

#ifndef HAVE_STRNDUP
char *strndup(const char *s, size_t n)
{
  size_t len = strnlen (s, n);
  char *new = (char *) malloc (len + 1);

  if (new == NULL)
    return NULL;

  new[len] = '\0';
  return (char *) memcpy (new, s, len);
}
#endif


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

/*
 * Reallocates its first arg @s - typical use: s = mnt_strconcat3(s,t,u);
 * Returns reallocated @s ion succes or NULL in case of error.
 */
char *mnt_strconcat3(char *s, const char *t, const char *u)
{
     size_t len = 0;

     len = (s ? strlen(s) : 0) + (t ? strlen(t) : 0) + (u ? strlen(u) : 0);

     if (!len)
	     return NULL;
     if (!s) {
	     s = malloc(len + 1);
	     *s = '\0';
     } else
	     s = realloc(s, len + 1);

     if (!s)
	     return NULL;
     if (t)
	     strcat(s, t);
     if (u)
	     strcat(s, u);
     return s;
}

/**
 * mnt_open_device:
 * @devname: device path
 * @flags: open(2) flags
 *
 * Opens device like open(2), but waits for cdrom medium (if errno=ENOMEDIUM).
 *
 * Returns: file descriptor or -1 in case of error.
 */
int mnt_open_device(const char *devname, int flags)
{
	int retries = 0;

	do {
		int fd = open(devname, flags);
		if (fd >= 0)
			return fd;
		if (errno != ENOMEDIUM)
			break;
		if (retries >= CONFIG_CDROM_NOMEDIUM_RETRIES)
			break;
		++retries;
		sleep(3);
	} while(1);

	return -1;
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
