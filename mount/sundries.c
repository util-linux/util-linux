/*
 * Support functions.  Exported functions are prototyped in sundries.h.
 * sundries.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 *
 * added fcntl locking by Kjetil T. (kjetilho@math.uio.no) - aeb, 950927
 *
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 */
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <mntent.h>		/* for MNTTYPE_SWAP */
#include "fstab.h"
#include "sundries.h"
#include "realpath.h"
#include "nfsmount.h"
#include "nls.h"

void *
xmalloc (size_t size) {
     void *t;

     if (size == 0)
          return NULL;

     t = malloc (size);
     if (t == NULL)
          die (EX_SYSERR, _("not enough memory"));

     return t;
}

char *
xstrdup (const char *s) {
     char *t;

     if (s == NULL)
          return NULL;

     t = strdup (s);

     if (t == NULL)
          die (EX_SYSERR, _("not enough memory"));

     return t;
}

char *
xstrndup (const char *s, int n) {
     char *t;

     if (s == NULL)
	  die (EX_SOFTWARE, _("bug in xstrndup call"));

     t = xmalloc(n+1);
     strncpy(t,s,n);
     t[n] = 0;

     return t;
}

char *
xstrconcat2 (const char *s, const char *t) {
     char *res;

     if (!s) s = "";
     if (!t) t = "";
     res = xmalloc(strlen(s) + strlen(t) + 1);
     strcpy(res, s);
     strcat(res, t);
     return res;
}

char *
xstrconcat3 (const char *s, const char *t, const char *u) {
     char *res;

     if (!s) s = "";
     if (!t) t = "";
     if (!u) u = "";
     res = xmalloc(strlen(s) + strlen(t) + strlen(u) + 1);
     strcpy(res, s);
     strcat(res, t);
     strcat(res, u);
     return res;
}

char *
xstrconcat4 (const char *s, const char *t, const char *u, const char *v) {
     char *res;

     if (!s) s = "";
     if (!t) t = "";
     if (!u) u = "";
     if (!v) v = "";
     res = xmalloc(strlen(s) + strlen(t) + strlen(u) + strlen(v) + 1);
     strcpy(res, s);
     strcat(res, t);
     strcat(res, u);
     strcat(res, v);
     return res;
}

/* Call this with SIG_BLOCK to block and SIG_UNBLOCK to unblock.  */
void
block_signals (int how) {
     sigset_t sigs;

     sigfillset (&sigs);
     sigdelset(&sigs, SIGTRAP);
     sigdelset(&sigs, SIGSEGV);
     sigprocmask (how, &sigs, (sigset_t *) 0);
}


/* Non-fatal error.  Print message and return.  */
/* (print the message in a single printf, in an attempt
    to avoid mixing output of several threads) */
void
error (const char *fmt, ...) {
     va_list args;
     char *fmt2;

     if (mount_quiet)
	  return;
     fmt2 = xstrconcat2 (fmt, "\n");
     va_start (args, fmt);
     vfprintf (stderr, fmt2, args);
     va_end (args);
     free (fmt2);
}

/* Fatal error.  Print message and exit.  */
void
die (int err, const char *fmt, ...) {
     va_list args;

     va_start (args, fmt);
     vfprintf (stderr, fmt, args);
     fprintf (stderr, "\n");
     va_end (args);

     unlock_mtab ();
     exit (err);
}

/* True if fstypes match.  Null *TYPES means match anything,
   except that swap types always return false. */
/* Accept nonfs,proc,devpts and nonfs,noproc,nodevpts
   with the same meaning. */
int
matching_type (const char *type, const char *types) {
     int no;			/* negated types list */
     int len;
     const char *p;

     if (streq (type, MNTTYPE_SWAP))
	  return 0;
     if (types == NULL)
	  return 1;

     no = 0;
     if (!strncmp(types, "no", 2)) {
	  no = 1;
	  types += 2;
     }

     /* Does type occur in types, separated by commas? */
     len = strlen(type);
     p = types;
     while(1) {
	     if (!strncmp(p, "no", 2) && !strncmp(p+2, type, len) &&
		 (p[len+2] == 0 || p[len+2] == ','))
		     return 0;
	     if (strncmp(p, type, len) == 0 &&
		 (p[len] == 0 || p[len] == ','))
		     return !no;
	     p = index(p,',');
	     if (!p)
		     break;
	     p++;
     }
     return no;
}

/* Make a canonical pathname from PATH.  Returns a freshly malloced string.
   It is up the *caller* to ensure that the PATH is sensible.  i.e.
   canonicalize ("/dev/fd0/.") returns "/dev/fd0" even though ``/dev/fd0/.''
   is not a legal pathname for ``/dev/fd0''.  Anything we cannot parse
   we return unmodified.   */
char *
canonicalize (const char *path) {
     char *canonical;
  
     if (path == NULL)
	  return NULL;

     if (streq(path, "none") || streq(path, "proc") || streq(path, "devpts"))
	  return xstrdup(path);

     canonical = xmalloc (PATH_MAX+2);
  
     if (myrealpath (path, canonical, PATH_MAX+1))
	  return canonical;

     free(canonical);
     return xstrdup(path);
}
