/*
 * Support function prototypes.  Functions are in sundries.c.
 * sundries.h,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 */

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <mntent.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fstab.h"


#define streq(s, t)	(strcmp ((s), (t)) == 0)


#define MOUNTED_LOCK	"/etc/mtab~"
#define MOUNTED_TEMP	"/etc/mtab.tmp"
#define _PATH_FSTAB	"/etc/fstab"
#define LOCK_BUSY	3

/* File pointer for /etc/mtab.  */
extern FILE *F_mtab;

/* File pointer for temp mtab.  */
extern FILE *F_temp;

/* String list data structure.  */ 
typedef struct string_list
{
  char *hd;
  struct string_list *tl;
} *string_list;

#define car(p) ((p) -> hd)
#define cdr(p) ((p) -> tl)

string_list cons (char *a, const string_list);

/* Quiet compilation with -Wmissing-prototypes.  */
int main (int argc, char *argv[]);

/* From mount_call.c.  */
int mount5 (const char *, const char *, const char *, int, void *);

/* Functions in sundries.c that are used in mount.c and umount.c  */ 
void block_signals (int how);
char *canonicalize (const char *path);
char *realpath (const char *path, char *resolved_path);
void close_mtab (void);
void error (const char *fmt, ...);
void lock_mtab (void);
int matching_type (const char *type, string_list types);
void open_mtab (const char *mode);
string_list parse_list (char *strings);
void unlock_mtab (void);
void update_mtab (const char *special, struct mntent *with);
struct mntent *getmntfile (const char *file);
void *xmalloc (size_t size);
char *xstrdup (const char *s);

/* Here is some serious cruft.  */
#ifdef __GNUC__
#if defined(__GNUC_MINOR__) && __GNUC__ == 2 && __GNUC_MINOR__ >= 5
void die (int errcode, const char *fmt, ...) __attribute__ ((noreturn));
#else /* GNUC < 2.5 */
void volatile die (int errcode, const char *fmt, ...);
#endif /* GNUC < 2.5 */
#else /* !__GNUC__ */
void die (int errcode, const char *fmt, ...);
#endif /* !__GNUC__ */

#ifdef HAVE_NFS
int nfsmount (const char *spec, const char *node, int *flags,
	      char **orig_opts, char **opt_args);
#endif

#define mount5(special, dir, type, flags, data) \
  mount (special, dir, type, 0xC0ED0000 | (flags), data)

