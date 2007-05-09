/*
 * Thu Jul 14 07:32:40 1994: faith@cs.unc.edu added changes from Adam
 * J. Richter (adam@adam.yggdrasil.com) so that /proc/filesystems is used
 * if no -t option is given.  I modified his patches so that, if
 * /proc/filesystems is not available, the behavior of mount is the same as
 * it was previously.
 *
 * Wed Feb 8 09:23:18 1995: Mike Grupenhoff <kashmir@umiacs.UMD.EDU> added
 * a probe of the superblock for the type before /proc/filesystems is
 * checked.
 *
 * Fri Apr  5 01:13:33 1996: quinlan@bucknell.edu, fixed up iso9660 autodetect
 *
 * Wed Nov  11 11:33:55 1998: K.Garloff@ping.de, try /etc/filesystems before
 * /proc/filesystems
 * [This was mainly in order to specify vfat before fat; these days we often
 *  detect *fat and then assume vfat, so perhaps /etc/filesystems isnt
 *  so useful anymore.]
 *
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * added Native Language Support
 *
 * 2000-12-01 Sepp Wijnands <mrrazz@garbage-coderz.net>
 * added probes for cramfs, hfs, hpfs and adfs.
 *
 * 2001-10-26 Tim Launchbury
 * added sysv magic.
 *
 * aeb - many changes.
 *
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "linux_fs.h"
#include "fsprobe.h"
#include "mount_guess_fstype.h"
#include "sundries.h"		/* for xstrdup */
#include "nls.h"

#define ETC_FILESYSTEMS		"/etc/filesystems"
#define PROC_FILESYSTEMS	"/proc/filesystems"

char *
do_guess_fstype(const char *device) 
{
	return blkid_get_tag_value(blkid, "TYPE", device);
}

static int
known_fstype(const char *fstype) 
{
	return blkid_known_fstype(fstype);
}
	

static struct tried {
	struct tried *next;
	char *type;
} *tried = NULL;

static int
was_tested(const char *fstype) {
	struct tried *t;

	if (known_fstype(fstype))
		return 1;
	for (t = tried; t; t = t->next) {
		if (!strcmp(t->type, fstype))
			return 1;
	}
	return 0;
}

static void
set_tested(const char *fstype) {
	struct tried *t = xmalloc(sizeof(struct tried));

	t->next = tried;
	t->type = xstrdup(fstype);
	tried = t;
}

static void
free_tested(void) {
	struct tried *t, *tt;

	t = tried;
	while(t) {
		free(t->type);
		tt = t->next;
		free(t);
		t = tt;
	}
	tried = NULL;
}

char *
guess_fstype(const char *spec) {
	char *type = do_guess_fstype(spec);
	if (verbose) {
	    printf (_("mount: you didn't specify a filesystem type for %s\n"),
		    spec);
	    if (!type)
	      printf (_("       I will try all types mentioned in %s or %s\n"),
		      ETC_FILESYSTEMS, PROC_FILESYSTEMS);
	    else if (!strcmp(type, "swap"))
	      printf (_("       and it looks like this is swapspace\n"));
	    else
	      printf (_("       I will try type %s\n"), type);
	}
	return type;
}

static char *
procfsnext(FILE *procfs) {
   char line[100];
   char fsname[100];

   while (fgets(line, sizeof(line), procfs)) {
      if (sscanf (line, "nodev %[^\n]\n", fsname) == 1) continue;
      if (sscanf (line, " %[^ \n]\n", fsname) != 1) continue;
      return xstrdup(fsname);
   }
   return 0;
}

/* Only use /proc/filesystems here, this is meant to test what
   the kernel knows about, so /etc/filesystems is irrelevant.
   Return: 1: yes, 0: no, -1: cannot open procfs */
int
is_in_procfs(const char *type) {
    FILE *procfs;
    char *fsname;
    int ret = -1;

    procfs = fopen(PROC_FILESYSTEMS, "r");
    if (procfs) {
	ret = 0;
	while ((fsname = procfsnext(procfs)) != NULL)
	    if (!strcmp(fsname, type)) {
		ret = 1;
		break;
	    }
	fclose(procfs);
	procfs = NULL;
    }
    return ret;
}

/* Try all types in FILESYSTEMS, except those in *types,
   in case *types starts with "no" */
/* return: 0: OK, -1: error in errno, 1: type not found */
/* when 0 or -1 is returned, *types contains the type used */
/* when 1 is returned, *types is NULL */
int
procfsloop(int (*mount_fn)(struct mountargs *), struct mountargs *args,
	   const char **types) {
	char *files[2] = { ETC_FILESYSTEMS, PROC_FILESYSTEMS };
	FILE *procfs;
	char *fsname;
	const char *notypes = NULL;
	int no = 0;
	int ret = 1;
	int errsv = 0;
	int i;

	if (*types && !strncmp(*types, "no", 2)) {
		no = 1;
		notypes = (*types) + 2;
	}
	*types = NULL;

	/* Use PROC_FILESYSTEMS only when ETC_FILESYSTEMS does not exist.
	   In some cases trying a filesystem that the kernel knows about
	   on the wrong data will crash the kernel; in such cases
	   ETC_FILESYSTEMS can be used to list the filesystems that we
	   are allowed to try, and in the order they should be tried.
	   End ETC_FILESYSTEMS with a line containing a single '*' only,
	   if PROC_FILESYSTEMS should be tried afterwards. */

	for (i=0; i<2; i++) {
		procfs = fopen(files[i], "r");
		if (!procfs)
			continue;
		while ((fsname = procfsnext(procfs)) != NULL) {
			if (!strcmp(fsname, "*")) {
				fclose(procfs);
				goto nexti;
			}
			if (was_tested (fsname))
				continue;
			if (no && matching_type(fsname, notypes))
				continue;
			set_tested (fsname);
			args->type = fsname;
			if (verbose) {
				printf(_("Trying %s\n"), fsname);
				fflush(stdout);
			}
			if ((*mount_fn) (args) == 0) {
				*types = fsname;
				ret = 0;
				break;
			} else if (errno != EINVAL &&
				   is_in_procfs(fsname) == 1) {
				*types = "guess";
				ret = -1;
				errsv = errno;
				break;
			}
		}
		free_tested();
		fclose(procfs);
		errno = errsv;
		return ret;
	nexti:;
	}
	return 1;
}
