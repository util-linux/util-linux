/*
 * Support functions.  Exported functions are prototyped in sundries.h.
 * sundries.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 *
 * added fcntl locking by Kjetil T. (kjetilho@math.uio.no) - aeb, 950927
 *
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@misiek.eu.org>
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

/* String list constructor.  (car() and cdr() are defined in "sundries.h").  */
string_list
cons (char *a, const string_list b) {
     string_list p;

     p = xmalloc (sizeof *p);
     car (p) = a;
     cdr (p) = b;
     return p;
}

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

/* Parse a list of strings like str[,str]... into a string list.  */
string_list
parse_list (char *strings) {
     string_list list;
     char *s, *t;

     if (strings == NULL)
	  return NULL;

     /* strtok() destroys its argument, so we have to use a copy */
     s = xstrdup(strings);

     list = cons (strtok (s, ","), NULL);

     while ((t = strtok (NULL, ",")) != NULL)
	  list = cons (t, list);

     return list;
}

/* True if fstypes match.  Null *TYPES means match anything,
   except that swap types always return false.
   This routine has some ugliness to deal with ``no'' types.
   Fixed bug: the `no' part comes at the end - aeb, 970216  */
int
matching_type (const char *type, string_list types) {
     char *notype;
     int foundyes, foundno;
     int no;			/* true if a "no" type match, eg -t nominix */

     if (streq (type, MNTTYPE_SWAP))
	  return 0;
     if (types == NULL)
	  return 1;

     if ((notype = alloca (strlen (type) + 3)) == NULL)
	  die (EX_SYSERR, _("%s: Out of memory!\n"), "mount");
     sprintf (notype, "no%s", type);

     foundyes = foundno = no = 0;
     while (types != NULL) {
	  if (cdr (types) == NULL)
	       no = (car (types)[0] == 'n') && (car (types)[1] == 'o');
	  if (streq (type, car (types)))
	       foundyes = 1;
	  else if (streq (notype, car (types)))
	       foundno = 1;
	  types = cdr (types);
     }

     return (foundno ? 0 : (no ^ foundyes));
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
