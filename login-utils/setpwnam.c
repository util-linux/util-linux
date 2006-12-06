/*
 *  setpwnam.c --
 *  edit an entry in a password database.
 *
 *  (c) 1994 Salvatore Valente <svalente@mit.edu>
 *  This file is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This file is distributed with no warranty.
 *
 *  Usage:
 *  1) get a struct passwd * from getpwnam().
 *     You should assume a struct passwd has an infinite number of fields,
 *     so you should not try to create one from scratch.
 *  2) edit the fields you want to edit.
 *  3) call setpwnam() with the edited struct passwd.
 *
 *  You should never directly read from or write to /etc/passwd.
 *  All user database queries should be directed through
 *  getpwnam() and setpwnam().
 *
 *  Thanks to "two guys named Ian".
 */
/*   $Author: faith $
 *   $Revision: 1.5 $
 *   $Date: 1995/10/12 14:46:36 $
 */

#undef DEBUG

/*  because I use getpwent(), putpwent(), etc... */
#define _SVID_SOURCE

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <errno.h>
#include <signal.h>
#include <sys/resource.h>
#ifdef BSD43
#include <sys/file.h>
#endif
#include <paths.h>

#include "pathnames.h"

extern int errno;

typedef int boolean;
#define false 0
#define true 1

#ifndef DEBUG
#define PASSWD_FILE	_PATH_PASSWD
#define PTMP_FILE	_PATH_PTMP
#define PTMPTMP_FILE	_PATH_PTMPTMP
#else
#define PASSWD_FILE	"/tmp/passwd"
#define PTMP_FILE	"/tmp/ptmp"
#define PTMPTMP_FILE	"/tmp/ptmptmp"
#endif

static int copy_pwd (struct passwd *src, struct passwd *dest);
static char *xstrdup (char *str);
static void pw_init(void);

/*
 *  setpwnam () --
 *	takes a struct passwd in which every field is filled in and valid.
 *	If the given username exists in the passwd file, his entry is
 *	replaced with the given entry.
 */
int setpwnam (struct passwd *pwd)
{
    FILE *fp;
    int x, save_errno, fd, ret;
    struct passwd *entry;
    boolean found;
    struct passwd spwd;
    int oldumask;

    /*  getpwent() returns a pointer to a static buffer.
     *  "pwd" might have some from getpwent(), so we have to copy it to
     *  some other buffer before calling getpwent().
     */
    if (copy_pwd (pwd, &spwd) < 0)
	return (-1);

    oldumask = umask(0);   /* Create with exact permissions */
    pw_init();

    /* sanity check */
    for (x = 0; x < 3; x++) {
        if (x > 0) sleep (1);
	fd = open (PTMPTMP_FILE, O_WRONLY|O_CREAT, 0644);
	if(fd == -1) {
	    perror(PTMPTMP_FILE);
	    umask(oldumask);
	    return (-1);
	}
	ret = link(PTMPTMP_FILE, PTMP_FILE);
	unlink(PTMPTMP_FILE);
	if(ret == -1)
	    close(fd);
	else
	    break;
    }

    umask(oldumask);
    if (ret == -1) return (-1);

    /* ptmp should be owned by root.root or root.wheel */
    if (chown (PTMP_FILE, (uid_t) 0, (gid_t) 0) < 0)
	perror ("chown");

    /* open ptmp for writing and passwd for reading */
    fp = fdopen (fd, "w");
    if (! fp) goto fail;

    setpwent ();

    /* parse the passwd file */
    found = false;
    while ((entry = getpwent ()) != NULL) {
        if (! strcmp (spwd.pw_name, entry->pw_name)) {
	    entry = &spwd;
            found = true;
        }
        if (putpwent (entry, fp) < 0) goto fail;
    }
    if (fclose (fp) < 0) goto fail;
    close (fd);
    endpwent ();

    if (! found) {
	errno = ENOENT; /* give me something better */
	goto fail;
    }

    /* we don't care if we can't remove the backup file */
    unlink (PASSWD_FILE".OLD");
    /* we don't care if we can't create the backup file */
    link (PASSWD_FILE, PASSWD_FILE".OLD");
    /* we DO care if we can't rename to the passwd file */
    if (rename (PTMP_FILE, PASSWD_FILE) < 0)
	goto fail;
    /* finally:  success */
    return 0;

 fail:
    save_errno = errno;
    if (fp) fclose (fp);
    if (fd >= 0) close (fd);
    endpwent ();
    unlink (PTMP_FILE);
    errno = save_errno;
    return (-1);
}

#define memzero(ptr, size) memset((char *) ptr, 0, size)
static int failed;

static int copy_pwd (struct passwd *src, struct passwd *dest)
{
    /*  this routine destroys abstraction barriers.  it's not portable
     *  across systems, or even across different versions of the C library
     *  on a given system.  it's dangerous and evil and wrong and I dispise
     *  getpwent() for forcing me to write this.
     */
    failed = 0;
    memzero (dest, sizeof (struct passwd));
    dest->pw_name = xstrdup (src->pw_name);
    dest->pw_passwd = xstrdup (src->pw_passwd);
    dest->pw_uid = src->pw_uid;
    dest->pw_gid = src->pw_gid;
    dest->pw_gecos = xstrdup (src->pw_gecos);
    dest->pw_dir = xstrdup (src->pw_dir);
    dest->pw_shell = xstrdup (src->pw_shell);
    return (failed);
}

static char *xstrdup (char *str)
{
    char *dup;

    if (! str)
	return NULL;
    dup = (char *) malloc (strlen (str) + 1);
    if (! dup) {
	failed = -1;
	return NULL;
    }
    strcpy (dup, str);
    return dup;
}

#ifdef NO_PUTPWENT

int putpwent (const struct passwd *p, FILE *stream)
{
    if (p == NULL || stream == NULL) {
	errno = EINVAL;
	return (-1);
    }
    if (fprintf (stream, "%s:%s:%u:%u:%s:%s:%s\n",
		 p->pw_name, p->pw_passwd, p->pw_uid, p->pw_gid,
		 p->pw_gecos, p->pw_dir, p->pw_shell) < 0)
	return (-1);
    return(0);
}

#endif

static void
pw_init()
{
	struct rlimit rlim;

	/* Unlimited resource limits. */
	rlim.rlim_cur = rlim.rlim_max = RLIM_INFINITY;
	(void)setrlimit(RLIMIT_CPU, &rlim);
	(void)setrlimit(RLIMIT_FSIZE, &rlim);
	(void)setrlimit(RLIMIT_STACK, &rlim);
	(void)setrlimit(RLIMIT_DATA, &rlim);
	(void)setrlimit(RLIMIT_RSS, &rlim);

	/* Don't drop core (not really necessary, but GP's). */
	rlim.rlim_cur = rlim.rlim_max = 0;
	(void)setrlimit(RLIMIT_CORE, &rlim);

	/* Turn off signals. */
	(void)signal(SIGALRM, SIG_IGN);
	(void)signal(SIGHUP, SIG_IGN);
	(void)signal(SIGINT, SIG_IGN);
	(void)signal(SIGPIPE, SIG_IGN);
	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGTERM, SIG_IGN);
	(void)signal(SIGTSTP, SIG_IGN);
	(void)signal(SIGTTOU, SIG_IGN);

	/* Create with exact permissions. */
	(void)umask(0);
}
