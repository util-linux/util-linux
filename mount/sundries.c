/*
 * Support functions.  Exported functions are prototyped in sundries.h.
 * sundries.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 */

#include "sundries.h"

/* File pointer for /etc/mtab.  */
FILE *F_mtab = NULL;

/* File pointer for temp mtab.  */
FILE *F_temp = NULL;

/* File descriptor for lock.  Value tested in unlock_mtab() to remove race.  */
static int lock = -1;

/* String list constructor.  (car() and cdr() are defined in "sundries.h").  */
string_list
cons (char *a, const string_list b)
{
  string_list p;

  p = xmalloc (sizeof *p);

  car (p) = a;
  cdr (p) = b;
  return p;
}

void *
xmalloc (size_t size)
{
  void *t;

  if (size == 0)
    return NULL;

  t = malloc (size);
  if (t == NULL)
    die (2, "not enough memory");
  
  return t;
}

char *
xstrdup (const char *s)
{
  char *t;

  if (s == NULL)
    return NULL;
 
  t = strdup (s);

  if (t == NULL)
    die (2, "not enough memory");

  return t;
}

/* Call this with SIG_BLOCK to block and SIG_UNBLOCK to unblock.  */
void
block_signals (int how)
{
  sigset_t sigs;

  sigfillset (&sigs);
  sigprocmask (how, &sigs, (sigset_t *) 0);
}


/* Non-fatal error.  Print message and return.  */
void
error (const char *fmt, ...)
{
  va_list args;

  va_start (args, fmt);
  vfprintf (stderr, fmt, args);
  fprintf (stderr, "\n");
  va_end (args);
}

/* Fatal error.  Print message and exit.  */
void
die (int err, const char *fmt, ...)
{
  va_list args;

  va_start (args, fmt);
  vfprintf (stderr, fmt, args);
  fprintf (stderr, "\n");
  va_end (args);

  unlock_mtab ();
  exit (err);
}

/* Ensure that the lock is released if we are interrupted.  */
static void
handler (int sig)
{
  die (2, "%s", sys_siglist[sig]);
}

/* Create the lock file.  The lock file will be removed if we catch a signal
   or when we exit.  The value of lock is tested to remove the race.  */
void
lock_mtab (void)
{
  int sig = 0;
  struct sigaction sa;

  /* If this is the first time, ensure that the lock will be removed.  */
  if (lock < 0)
    {
      sa.sa_handler = handler;
      sa.sa_flags = 0;
      sigfillset (&sa.sa_mask);
  
      while (sigismember (&sa.sa_mask, ++sig) != -1)
	sigaction (sig, &sa, (struct sigaction *) 0);

      if ((lock = open (MOUNTED_LOCK, O_WRONLY|O_CREAT|O_EXCL, 0)) < 0)
	die (2, "can't create lock file %s: %s",
	     MOUNTED_LOCK, strerror (errno));
    }
}

/* Remove lock file.  */
void
unlock_mtab (void)
{
  if (lock != -1)
    {
      close( lock );
      unlink (MOUNTED_LOCK);
    }
}

/* Open mtab.  */
void
open_mtab (const char *mode)
{
  if ((F_mtab = setmntent (MOUNTED, mode)) == NULL)
    die (2, "can't open %s: %s", MOUNTED, strerror (errno));
}

/* Close mtab.  */
void
close_mtab (void)
{
  if (fchmod (fileno (F_mtab), S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) < 0)
    die (1, "mount: error changing mode of %s: %s", MOUNTED, strerror (errno));
  endmntent (F_mtab);
}

/* Update the mtab by removing any DIR entries and replace it with INSTEAD.  */
void
update_mtab (const char *dir, struct mntent *instead)
{
  struct mntent *mnt;
  struct mntent *next;
  int added = 0;

  open_mtab ("r");

  if ((F_temp = setmntent (MOUNTED_TEMP, "w")) == NULL)
    die (2, "can't open %s: %s", MOUNTED_TEMP, strerror (errno));
  
  while ((mnt = getmntent (F_mtab)))
    {
      next = streq (mnt->mnt_dir, dir) ? (added++, instead) : mnt;
      if (next && addmntent(F_temp, next) == 1)
	die (1, "error writing %s: %s", MOUNTED_TEMP, strerror (errno));
    }
  if (instead && !added)
    addmntent(F_temp, instead);

  endmntent (F_mtab);
  if (fchmod (fileno (F_temp), S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) < 0)
    die (1, "error changing mode of %s: %s", MOUNTED_TEMP, strerror (errno));
  endmntent (F_temp);

  if (rename (MOUNTED_TEMP, MOUNTED) < 0)
    die (1, "can't rename %s to %s: %s",
	 MOUNTED_TEMP, MOUNTED, strerror(errno));
}

/* Given the name FILE, try to find it in mtab.  */ 
struct mntent *
getmntfile (const char *file)
{
  struct mntent *mnt;

  if (!F_mtab)
     return NULL;

  rewind(F_mtab);

  while ((mnt = getmntent (F_mtab)) != NULL)
    {
      if (streq (mnt->mnt_dir, file))
	break;
      if (streq (mnt->mnt_fsname, file))
	break;
    }

  return mnt;
}

/* Parse a list of strings like str[,str]... into a string list.  */
string_list
parse_list (char *strings)
{
  string_list list;
  char *t;

  if (strings == NULL)
    return NULL;

  list = cons (strtok (strings, ","), NULL);

  while ((t = strtok (NULL, ",")) != NULL)
    list = cons (t, list);

  return list;
}

/* True if fstypes match.  Null *TYPES means match anything,
   except that swap types always return false.  This routine
   has some ugliness to deal with ``no'' types.  */
int
matching_type (const char *type, string_list types)
{
  char *notype;
  int no;			/* true if a "no" type match, ie -t nominix */

  if (streq (type, MNTTYPE_SWAP))
    return 0;
  if (types == NULL)
    return 1;

  if ((notype = alloca (strlen (type) + 3)) == NULL)
    die (2, "mount: out of memory");
  sprintf (notype, "no%s", type);
  no = (car (types)[0] == 'n') && (car (types)[1] == 'o');

  /* If we get a match and the user specified a positive match type (e.g.
     "minix") we return true.  If we match and a negative match type (e.g.
     "nominix") was specified we return false.  */
  while (types != NULL)
    if (streq (type, car (types)))
      return !no;
    else if (streq (notype, car (types)))
      return 0;			/* match with "nofoo" always returns false */
    else
      types = cdr (types);

  /* No matches, so if the user specified a positive match type return false,
     if a negative match type was specified, return true.  */
  return no;
}

/* Make a canonical pathname from PATH.  Returns a freshly malloced string.
   It is up the *caller* to ensure that the PATH is sensible.  i.e.
   canonicalize ("/dev/fd0/.") returns "/dev/fd0" even though ``/dev/fd0/.''
   is not a legal pathname for ``/dev/fd0.''  Anything we cannot parse
   we return unmodified.   */
char *
canonicalize (const char *path)
{
  char *canonical = xmalloc (PATH_MAX + 1);
  
  if (path == NULL)
    return NULL;
  
  if (realpath (path, canonical))
    return canonical;

  strcpy (canonical, path);
  return canonical;
}
