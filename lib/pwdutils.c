/*
 * No copyright is claimed. This code is in the public domain; do with
 * it what you wish.
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include "c.h"
#include "pwdutils.h"
#include "xalloc.h"
#include "strutils.h"

struct passwd *xgetpwuid(uid_t uid, char **pwdbuf)
{
	struct passwd *pwd = NULL, *res = NULL;
	int rc;

	assert(pwdbuf);

	*pwdbuf = xmalloc(UL_GETPW_BUFSIZ);
	pwd = xcalloc(1, sizeof(struct passwd));

	errno = 0;
	rc = getpwuid_r(uid, pwd, *pwdbuf, UL_GETPW_BUFSIZ, &res);
	if (rc != 0) {
		errno = rc;
		goto failed;
	}
	if (!res) {
		errno = EINVAL;
		goto failed;
	}
	return pwd;
failed:
	free(pwd);
	free(*pwdbuf);
	return NULL;
}

/* Returns allocated passwd and allocated pwdbuf for the username or UID passed
 * as @str. In case of error returns NULL and set errno, for unknown user it
 * sets errno to EINVAL.
 */
struct passwd *xgetuserpw(const char *str ,char **pwdbuf)
{
	struct passwd *pwd = NULL, *res = NULL;
	int rc;
	uint64_t uid;

	assert(pwdbuf);
	assert(str);

	*pwdbuf = xmalloc(UL_GETPW_BUFSIZ);
	pwd = xcalloc(1, sizeof(struct passwd));

	/* is @str a UID ? */
	rc = ul_strtou64(str, &uid, 10);
	if (rc == -ERANGE) {
		errno = ERANGE;
		goto failed;
	}
	/* @str is an invalid number, let's assume it is the username */
	if (rc == -EINVAL) {
		rc = getpwnam_r(str, pwd, *pwdbuf, UL_GETPW_BUFSIZ, &res);
	} else {
		if (uid > MAX_OF_UINT_TYPE(uid_t)) {
			errno = ERANGE;
			goto failed;
		}
		rc = getpwuid_r((uid_t)uid, pwd, *pwdbuf, UL_GETPW_BUFSIZ, &res);
	}

	if (rc != 0) {
		errno = rc;
		goto failed;
	}
	if (!res) {
		errno = EINVAL;
		goto failed;
	}
	return pwd;
failed:
	free(pwd);
	free(*pwdbuf);
	return NULL;
}

/* Returns allocated group and allocated grpbuf for the group name or GID passed
 * as @str. In case of error returns NULL and set errno, for unknown group it
 * sets errno to EINVAL.
 */
struct group *xgetgroup(const char *str, char **grpbuf)
{
	struct group *grp = NULL, *res = NULL;
	int rc;
	uint64_t gid;

	assert(grpbuf);
	assert(str);

	*grpbuf = xmalloc(UL_GETPW_BUFSIZ);
	grp = xcalloc(1, sizeof(struct group));

	/* is @str a GID ? */
	rc = ul_strtou64(str, &gid, 10);
	if (rc == -ERANGE) {
		errno = ERANGE;
		goto failed;
	}
	/* @str is an invalid number, let's assume it is the group name */
	if (rc == -EINVAL) {
		rc = getgrnam_r(str, grp, *grpbuf, UL_GETPW_BUFSIZ, &res);
	} else {
		if (gid > MAX_OF_UINT_TYPE(gid_t)) {
			errno = ERANGE;
			goto failed;
		}
		rc = getgrgid_r((gid_t)gid, grp, *grpbuf, UL_GETPW_BUFSIZ, &res);
	}

	if (rc != 0) {
		errno = rc;
		goto failed;
	}
	if (!res) {
		errno = EINVAL;
		goto failed;
	}
	return grp;
failed:
	free(grp);
	free(*grpbuf);
	return NULL;
}

char *xgetlogin(void)
{
	struct passwd *pw = NULL;
	uid_t ruid;

	/* GNU Hurd implementation has an extension where a process can exist in a
	 * non-conforming environment, and thus be outside the realms of POSIX
	 * process identifiers; on this platform, getuid() fails with a status of
	 * (uid_t)(-1) and sets errno if a program is run from a non-conforming
	 * environment.
	 *
	 * http://austingroupbugs.net/view.php?id=511
	 *
	 * The same implementation is useful for other systems, since getlogin(3)
	 * shouldn't be used as actual identification.
	 */
	errno = 0;
	ruid = getuid();

	if (errno == 0)
		pw = getpwuid(ruid);
	if (pw && pw->pw_name && *pw->pw_name)
		return xstrdup(pw->pw_name);

	return NULL;
}

/*
 * Return a pointer to a `struct group` for a matching group name or GID.
 */
struct group *ul_getgrp_str(const char *str)
{
        int rc;
        uint64_t gid;

        rc = ul_strtou64(str, &gid, 10);
        if (rc == -ERANGE)
                return NULL;
        if (rc == -EINVAL)
                return getgrnam(str);
        if (gid > MAX_OF_UINT_TYPE(gid_t))
                return NULL;

        return getgrgid((gid_t)gid);
}

/*
 * Return a pointer to a `struct passwd` for a matching username or UID.
 */
struct passwd *ul_getuserpw_str(const char *str)
{
        int rc;
        uint64_t uid;

        rc = ul_strtou64(str, &uid, 10);
        if (rc == -ERANGE)
                return NULL;
        if (rc == -EINVAL)
                return getpwnam(str);
        if (uid > MAX_OF_UINT_TYPE(uid_t))
                return NULL;

        return getpwuid((uid_t)uid);
}

#ifdef TEST_PROGRAM
int main(int argc, char *argv[])
{
	char *buf = NULL;
	struct passwd *pwd = NULL;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <username>\n", argv[0]);
		return EXIT_FAILURE;
	}

	pwd = xgetuserpw(argv[1], &buf);
	if (!pwd)
		err(EXIT_FAILURE, "failed to get %s pwd entry", argv[1]);

	printf("Username: %s\n", pwd->pw_name);
	printf("UID:      %d\n", pwd->pw_uid);
	printf("HOME:     %s\n", pwd->pw_dir);
	printf("GECO:     %s\n", pwd->pw_gecos);

	free(pwd);
	free(buf);

	printf("Current:  %s\n", (buf = xgetlogin()));
	free(buf);

	return EXIT_SUCCESS;
}
#endif /* TEST_PROGRAM */
