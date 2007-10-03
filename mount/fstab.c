/* 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 * Sun Mar 21 1999 - Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 * - fixed strerr(errno) in gettext calls
 */

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include "mount_mntent.h"
#include "fstab.h"
#include "sundries.h"
#include "xmalloc.h"
#include "fsprobe.h"
#include "mount_paths.h"
#include "nls.h"

#define streq(s, t)	(strcmp ((s), (t)) == 0)

#define PROC_MOUNTS		"/proc/mounts"


/* Information about mtab. ------------------------------------*/
static int have_mtab_info = 0;
static int var_mtab_does_not_exist = 0;
static int var_mtab_is_a_symlink = 0;

static void
get_mtab_info(void) {
	struct stat mtab_stat;

	if (!have_mtab_info) {
		if (lstat(MOUNTED, &mtab_stat))
			var_mtab_does_not_exist = 1;
		else if (S_ISLNK(mtab_stat.st_mode))
			var_mtab_is_a_symlink = 1;
		have_mtab_info = 1;
	}
}

int
mtab_does_not_exist(void) {
	get_mtab_info();
	return var_mtab_does_not_exist;
}

static int
mtab_is_a_symlink(void) {
	get_mtab_info();
	return var_mtab_is_a_symlink;
}

int
mtab_is_writable() {
	int fd;

	/* Should we write to /etc/mtab upon an update?
	   Probably not if it is a symlink to /proc/mounts, since that
	   would create a file /proc/mounts in case the proc filesystem
	   is not mounted. */
	if (mtab_is_a_symlink())
		return 0;

	fd = open(MOUNTED, O_RDWR | O_CREAT, 0644);
	if (fd >= 0) {
		close(fd);
		return 1;
	} else
		return 0;
}

/* Contents of mtab and fstab ---------------------------------*/

struct mntentchn mounttable, fstab;
static int got_mtab = 0;
static int got_fstab = 0;

static void read_mounttable(void), read_fstab(void);

struct mntentchn *
mtab_head() {
	if (!got_mtab)
		read_mounttable();
	return &mounttable;
}

struct mntentchn *
fstab_head() {
	if (!got_fstab)
		read_fstab();
	return &fstab;
}

static void
my_free(const void *s) {
	if (s)
		free((void *) s);
}

static void
my_free_mc(struct mntentchn *mc) {
	if (mc) {
		my_free(mc->m.mnt_fsname);
		my_free(mc->m.mnt_dir);
		my_free(mc->m.mnt_type);
		my_free(mc->m.mnt_opts);
		free(mc);
	}
}


static void
discard_mntentchn(struct mntentchn *mc0) {
	struct mntentchn *mc, *mc1;

	for (mc = mc0->nxt; mc && mc != mc0; mc = mc1) {
		mc1 = mc->nxt;
		my_free_mc(mc);
	}
}

static void
read_mntentchn(mntFILE *mfp, const char *fnam, struct mntentchn *mc0) {
	struct mntentchn *mc = mc0;
	struct my_mntent *mnt;

	while ((mnt = my_getmntent(mfp)) != NULL) {
		if (!streq(mnt->mnt_type, MNTTYPE_IGNORE)) {
			mc->nxt = (struct mntentchn *) xmalloc(sizeof(*mc));
			mc->nxt->prev = mc;
			mc = mc->nxt;
			mc->m = *mnt;
			mc->nxt = mc0;
		}
	}
	mc0->prev = mc;
	if (ferror(mfp->mntent_fp)) {
		int errsv = errno;
		error(_("warning: error reading %s: %s"),
		      fnam, strerror (errsv));
		mc0->nxt = mc0->prev = NULL;
	}
	my_endmntent(mfp);
}

/*
 * Read /etc/mtab.  If that fails, try /proc/mounts.
 * This produces a linked list. The list head mounttable is a dummy.
 * Return 0 on success.
 */
static void
read_mounttable() {
	mntFILE *mfp;
	const char *fnam;
	struct mntentchn *mc = &mounttable;

	got_mtab = 1;
	mc->nxt = mc->prev = NULL;

	fnam = MOUNTED;
	mfp = my_setmntent (fnam, "r");
	if (mfp == NULL || mfp->mntent_fp == NULL) {
		int errsv = errno;
		fnam = PROC_MOUNTS;
		mfp = my_setmntent (fnam, "r");
		if (mfp == NULL || mfp->mntent_fp == NULL) {
			error(_("warning: can't open %s: %s"),
			      MOUNTED, strerror (errsv));
			return;
		}
		if (verbose)
			printf (_("mount: could not open %s - "
				  "using %s instead\n"),
				MOUNTED, PROC_MOUNTS);
	}
	read_mntentchn(mfp, fnam, mc);
}

static void
read_fstab() {
	mntFILE *mfp = NULL;
	const char *fnam;
	struct mntentchn *mc = &fstab;

	got_fstab = 1;
	mc->nxt = mc->prev = NULL;

	fnam = _PATH_FSTAB;
	mfp = my_setmntent (fnam, "r");
	if (mfp == NULL || mfp->mntent_fp == NULL) {
		int errsv = errno;
		error(_("warning: can't open %s: %s"),
		      _PATH_FSTAB, strerror (errsv));
		return;
	}
	read_mntentchn(mfp, fnam, mc);
}
     

/* Given the name NAME, try to find it in mtab.  */ 
struct mntentchn *
getmntfile (const char *name) {
	struct mntentchn *mc, *mc0;

	mc0 = mtab_head();
	for (mc = mc0->nxt; mc && mc != mc0; mc = mc->nxt)
		if (streq(mc->m.mnt_dir, name) ||
		    streq(mc->m.mnt_fsname, name))
			return mc;
	return NULL;
}

/*
 * Given the directory name NAME, and the place MCPREV we found it last time,
 * try to find more occurrences.
 */ 
struct mntentchn *
getmntdirbackward (const char *name, struct mntentchn *mcprev) {
	struct mntentchn *mc, *mc0;

	mc0 = mtab_head();
	if (!mcprev)
		mcprev = mc0;
	for (mc = mcprev->prev; mc && mc != mc0; mc = mc->prev)
		if (streq(mc->m.mnt_dir, name))
			return mc;
	return NULL;
}

/*
 * Given the device name NAME, and the place MCPREV we found it last time,
 * try to find more occurrences.
 */ 
struct mntentchn *
getmntdevbackward (const char *name, struct mntentchn *mcprev) {
	struct mntentchn *mc, *mc0;

	mc0 = mtab_head();
	if (!mcprev)
		mcprev = mc0;
	for (mc = mcprev->prev; mc && mc != mc0; mc = mc->prev)
		if (streq(mc->m.mnt_fsname, name))
			return mc;
	return NULL;
}

/*
 * Given the name NAME, check that it occurs precisely once as dir or dev.
 */
int
is_mounted_once(const char *name) {
	struct mntentchn *mc, *mc0;
	int ct = 0;

	mc0 = mtab_head();
	for (mc = mc0->prev; mc && mc != mc0; mc = mc->prev)
		if (streq(mc->m.mnt_dir, name) ||
		    streq(mc->m.mnt_fsname, name))
			ct++;
	return (ct == 1);
}

/* Given the name FILE, try to find the option "loop=FILE" in mtab.  */ 
struct mntentchn *
getmntoptfile (const char *file) {
	struct mntentchn *mc, *mc0;
	const char *opts, *s;
	int l;

	if (!file)
		return NULL;

	l = strlen(file);

	mc0 = mtab_head();
	for (mc = mc0->nxt; mc && mc != mc0; mc = mc->nxt)
		if ((opts = mc->m.mnt_opts) != NULL
		    && (s = strstr(opts, "loop="))
		    && !strncmp(s+5, file, l)
		    && (s == opts || s[-1] == ',')
		    && (s[l+5] == 0 || s[l+5] == ','))
			return mc;
	return NULL;
}

static int
has_label(const char *device, const char *label) {
	const char *devlabel;
	int ret;

	devlabel = fsprobe_get_label_by_devname(device);
	if (!devlabel)
		return 0;

	ret = !strcmp(label, devlabel);
	my_free(devlabel);
	return ret;
}

static int
has_uuid(const char *device, const char *uuid){
	const char *devuuid;
	int ret;

	devuuid = fsprobe_get_uuid_by_devname(device);
	if (!devuuid)
		return 0;

	ret = !strcmp(uuid, devuuid);
	my_free(devuuid);
	return ret;
}

/* Find the entry (SPEC,DIR) in fstab -- spec and dir must be canonicalized! */
struct mntentchn *
getfs_by_specdir (const char *spec, const char *dir) {
	struct mntentchn *mc, *mc0;

	mc0 = fstab_head();

	for (mc = mc0->nxt; mc && mc != mc0; mc = mc->nxt) {
		/* dir */
		if (!streq(mc->m.mnt_dir, dir)) {
			char *dr = canonicalize(mc->m.mnt_dir);
			int ok = 0;

			if (streq(dr, dir))
				ok = 1;
			my_free(dr);
			if (!ok)
				continue;
		}

		/* spec */
		if (!streq(mc->m.mnt_fsname, spec)) {
			char *fs = canonicalize(mc->m.mnt_fsname);
			int ok = 0;

			if (streq(fs, spec))
				ok = 1;
			else if (strncmp (fs, "LABEL=", 6) == 0) {
				if (has_label(spec, fs + 6))
					ok = 1;
			}
			else if (strncmp (fs, "UUID=", 5) == 0) {
				if (has_uuid(spec, fs + 5))
					ok = 1;
			}
			my_free(fs);
			if (!ok)
				continue;
		}
		return mc;
	}

	return NULL;
}

/* Find the dir DIR in fstab.  */
struct mntentchn *
getfs_by_dir (const char *dir) {
	struct mntentchn *mc, *mc0;
	char *cdir;

	mc0 = fstab_head();
	for (mc = mc0->nxt; mc && mc != mc0; mc = mc->nxt)
		if (streq(mc->m.mnt_dir, dir))
			return mc;

	cdir = canonicalize(dir);
	for (mc = mc0->nxt; mc && mc != mc0; mc = mc->nxt) {
		if (streq(mc->m.mnt_dir, cdir)) {
			free(cdir);
			return mc;
		}
	}
	free(cdir);
	return NULL;
}

/* Find the device SPEC in fstab.  */
struct mntentchn *
getfs_by_spec (const char *spec) {
	char *name, *value, *cspec;
	struct mntentchn *mc = NULL;

	if (!spec)
		return NULL;

	if (parse_spec(spec, &name, &value) != 0)
		return NULL;				/* parse error */

	if (name) {
		if (!strcmp(name,"LABEL"))
			mc = getfs_by_label (value);
		else if (!strcmp(name,"UUID"))
			mc = getfs_by_uuid (value);

		free((void *) name);
		return mc;
	}

	cspec = canonicalize(spec);
	mc = getfs_by_devname(cspec);
	free(cspec);

	if (!mc)
		/* noncanonical name  like /dev/cdrom */
		mc = getfs_by_devname(spec);

	return mc;
}

/* Find the device in fstab.  */
struct mntentchn *
getfs_by_devname (const char *devname) {
	struct mntentchn *mc, *mc0;

	mc0 = fstab_head();
	for (mc = mc0->nxt; mc && mc != mc0; mc = mc->nxt)
		if (streq(mc->m.mnt_fsname, devname))
			return mc;
	return NULL;
}


/* Find the uuid UUID in fstab. */
struct mntentchn *
getfs_by_uuid (const char *uuid) {
	struct mntentchn *mc, *mc0;

	mc0 = fstab_head();
	for (mc = mc0->nxt; mc && mc != mc0; mc = mc->nxt)
		if (strncmp (mc->m.mnt_fsname, "UUID=", 5) == 0
		    && streq(mc->m.mnt_fsname + 5, uuid))
			return mc;
	return NULL;
}

/* Find the label LABEL in fstab. */
struct mntentchn *
getfs_by_label (const char *label) {
	struct mntentchn *mc, *mc0;

	mc0 = fstab_head();
	for (mc = mc0->nxt; mc && mc != mc0; mc = mc->nxt)
		if (strncmp (mc->m.mnt_fsname, "LABEL=", 6) == 0
		    && streq(mc->m.mnt_fsname + 6, label))
			return mc;
	return NULL;
}

/* Updating mtab ----------------------------------------------*/

/* Flag for already existing lock file. */
static int we_created_lockfile = 0;
static int lockfile_fd = -1;

/* Flag to indicate that signals have been set up. */
static int signals_have_been_setup = 0;

/* Ensure that the lock is released if we are interrupted.  */
extern char *strsignal(int sig);	/* not always in <string.h> */

static void
handler (int sig) {
     die(EX_USER, "%s", strsignal(sig));
}

static void
setlkw_timeout (int sig) {
     /* nothing, fcntl will fail anyway */
}

/* Remove lock file.  */
void
unlock_mtab (void) {
	if (we_created_lockfile) {
		close(lockfile_fd);
		lockfile_fd = -1;
		unlink (MOUNTED_LOCK);
		we_created_lockfile = 0;
	}
}

/* Create the lock file.
   The lock file will be removed if we catch a signal or when we exit. */
/* The old code here used flock on a lock file /etc/mtab~ and deleted
   this lock file afterwards. However, as rgooch remarks, that has a
   race: a second mount may be waiting on the lock and proceed as
   soon as the lock file is deleted by the first mount, and immediately
   afterwards a third mount comes, creates a new /etc/mtab~, applies
   flock to that, and also proceeds, so that the second and third mount
   now both are scribbling in /etc/mtab.
   The new code uses a link() instead of a creat(), where we proceed
   only if it was us that created the lock, and hence we always have
   to delete the lock afterwards. Now the use of flock() is in principle
   superfluous, but avoids an arbitrary sleep(). */

/* Where does the link point to? Obvious choices are mtab and mtab~~.
   HJLu points out that the latter leads to races. Right now we use
   mtab~.<pid> instead. Use 20 as upper bound for the length of %d. */
#define MOUNTLOCK_LINKTARGET		MOUNTED_LOCK "%d"
#define MOUNTLOCK_LINKTARGET_LTH	(sizeof(MOUNTED_LOCK)+20)

/*
 * The original mount locking code has used sleep(1) between attempts and
 * maximal number of attemps has been 5.
 *
 * There was very small number of attempts and extremely long waiting (1s)
 * that is useless on machines with large number of concurret mount processes.
 *
 * Now we wait few thousand microseconds between attempts and we have global
 * time limit (30s) rather than limit for number of attempts. The advantage
 * is that this method also counts time which we spend in fcntl(F_SETLKW) and
 * number of attempts is not so much restricted.
 *
 * -- kzak@redhat.com [2007-Mar-2007]
 */

/* maximum seconds between first and last attempt */
#define MOUNTLOCK_MAXTIME		30

/* sleep time (in microseconds, max=999999) between attempts */
#define MOUNTLOCK_WAITTIME		5000

void
lock_mtab (void) {
	int i;
	struct timespec waittime;
	struct timeval maxtime;
	char linktargetfile[MOUNTLOCK_LINKTARGET_LTH];

	at_die = unlock_mtab;

	if (!signals_have_been_setup) {
		int sig = 0;
		struct sigaction sa;

		sa.sa_handler = handler;
		sa.sa_flags = 0;
		sigfillset (&sa.sa_mask);

		while (sigismember (&sa.sa_mask, ++sig) != -1
		       && sig != SIGCHLD) {
			if (sig == SIGALRM)
				sa.sa_handler = setlkw_timeout;
			else
				sa.sa_handler = handler;
			sigaction (sig, &sa, (struct sigaction *) 0);
		}
		signals_have_been_setup = 1;
	}

	sprintf(linktargetfile, MOUNTLOCK_LINKTARGET, getpid ());

	i = open (linktargetfile, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
	if (i < 0) {
		int errsv = errno;
		/* linktargetfile does not exist (as a file)
		   and we cannot create it. Read-only filesystem?
		   Too many files open in the system?
		   Filesystem full? */
		die (EX_FILEIO, _("can't create lock file %s: %s "
						  "(use -n flag to override)"),
			 linktargetfile, strerror (errsv));
	}
	close(i);

	gettimeofday(&maxtime, NULL);
	maxtime.tv_sec += MOUNTLOCK_MAXTIME;

	waittime.tv_sec = 0;
	waittime.tv_nsec = (1000 * MOUNTLOCK_WAITTIME);

	/* Repeat until it was us who made the link */
	while (!we_created_lockfile) {
		struct timeval now;
		struct flock flock;
		int errsv, j;

		j = link(linktargetfile, MOUNTED_LOCK);
		errsv = errno;

		if (j == 0)
			we_created_lockfile = 1;

		if (j < 0 && errsv != EEXIST) {
			(void) unlink(linktargetfile);
			die (EX_FILEIO, _("can't link lock file %s: %s "
			     "(use -n flag to override)"),
			     MOUNTED_LOCK, strerror (errsv));
		}

		lockfile_fd = open (MOUNTED_LOCK, O_WRONLY);

		if (lockfile_fd < 0) {
			/* Strange... Maybe the file was just deleted? */
			int errsv = errno;
			gettimeofday(&now, NULL);
			if (errno == ENOENT && now.tv_sec < maxtime.tv_sec) {
				we_created_lockfile = 0;
				continue;
			}
			(void) unlink(linktargetfile);
			die (EX_FILEIO, _("can't open lock file %s: %s "
			     "(use -n flag to override)"),
			     MOUNTED_LOCK, strerror (errsv));
		}

		flock.l_type = F_WRLCK;
		flock.l_whence = SEEK_SET;
		flock.l_start = 0;
		flock.l_len = 0;

		if (j == 0) {
			/* We made the link. Now claim the lock. */
			if (fcntl (lockfile_fd, F_SETLK, &flock) == -1) {
				if (verbose) {
				    int errsv = errno;
				    printf(_("Can't lock lock file %s: %s\n"),
					   MOUNTED_LOCK, strerror (errsv));
				}
				/* proceed, since it was us who created the lockfile anyway */
			}
			(void) unlink(linktargetfile);
		} else {
			/* Someone else made the link. Wait. */
			gettimeofday(&now, NULL);
			if (now.tv_sec < maxtime.tv_sec) {
				alarm(maxtime.tv_sec - now.tv_sec);
				if (fcntl (lockfile_fd, F_SETLKW, &flock) == -1) {
					int errsv = errno;
					(void) unlink(linktargetfile);
					die (EX_FILEIO, _("can't lock lock file %s: %s"),
					     MOUNTED_LOCK, (errno == EINTR) ?
					     _("timed out") : strerror (errsv));
				}
				alarm(0);

				nanosleep(&waittime, NULL);
			} else {
				(void) unlink(linktargetfile);
				die (EX_FILEIO, _("Cannot create link %s\n"
						  "Perhaps there is a stale lock file?\n"),
					 MOUNTED_LOCK);
			}
			close(lockfile_fd);
		}
	}
}

/*
 * Update the mtab.
 *  Used by umount with null INSTEAD: remove the last DIR entry.
 *  Used by mount upon a remount: update option part,
 *   and complain if a wrong device or type was given.
 *   [Note that often a remount will be a rw remount of /
 *    where there was no entry before, and we'll have to believe
 *    the values given in INSTEAD.]
 */

void
update_mtab (const char *dir, struct my_mntent *instead) {
	mntFILE *mfp, *mftmp;
	const char *fnam = MOUNTED;
	struct mntentchn mtabhead;	/* dummy */
	struct mntentchn *mc, *mc0, *absent = NULL;
	struct stat sbuf;
	int fd;

	if (mtab_does_not_exist() || !mtab_is_writable())
		return;

	lock_mtab();

	/* having locked mtab, read it again */
	mc0 = mc = &mtabhead;
	mc->nxt = mc->prev = NULL;

	mfp = my_setmntent(fnam, "r");
	if (mfp == NULL || mfp->mntent_fp == NULL) {
		int errsv = errno;
		error (_("cannot open %s (%s) - mtab not updated"),
		       fnam, strerror (errsv));
		goto leave;
	}

	read_mntentchn(mfp, fnam, mc);

	/* find last occurrence of dir */
	for (mc = mc0->prev; mc && mc != mc0; mc = mc->prev)
		if (streq(mc->m.mnt_dir, dir))
			break;
	if (mc && mc != mc0) {
		if (instead == NULL) {
			/* An umount - remove entry */
			if (mc && mc != mc0) {
				mc->prev->nxt = mc->nxt;
				mc->nxt->prev = mc->prev;
				my_free_mc(mc);
			}
		} else if (!strcmp(mc->m.mnt_dir, instead->mnt_dir)) {
			/* A remount */
			my_free(mc->m.mnt_opts);
			mc->m.mnt_opts = xstrdup(instead->mnt_opts);
		} else {
			/* A move */
			my_free(mc->m.mnt_dir);
			mc->m.mnt_dir = xstrdup(instead->mnt_dir);
		}
	} else if (instead) {
		/* not found, add a new entry */
		absent = xmalloc(sizeof(*absent));
		absent->m.mnt_fsname = xstrdup(instead->mnt_fsname);
		absent->m.mnt_dir = xstrdup(instead->mnt_dir);
		absent->m.mnt_type = xstrdup(instead->mnt_type);
		absent->m.mnt_opts = xstrdup(instead->mnt_opts);
		absent->m.mnt_freq = instead->mnt_freq;
		absent->m.mnt_passno = instead->mnt_passno;
		absent->nxt = mc0;
		if (mc0->prev != NULL) {
			absent->prev = mc0->prev;
			mc0->prev->nxt = absent;
		} else {
			absent->prev = mc0;
		}
		mc0->prev = absent;
		if (mc0->nxt == NULL)
			mc0->nxt = absent;
	}

	/* write chain to mtemp */
	mftmp = my_setmntent (MOUNTED_TEMP, "w");
	if (mftmp == NULL || mftmp->mntent_fp == NULL) {
		int errsv = errno;
		error (_("cannot open %s (%s) - mtab not updated"),
		       MOUNTED_TEMP, strerror (errsv));
		discard_mntentchn(mc0);
		goto leave;
	}

	for (mc = mc0->nxt; mc && mc != mc0; mc = mc->nxt) {
		if (my_addmntent(mftmp, &(mc->m)) == 1) {
			int errsv = errno;
			die (EX_FILEIO, _("error writing %s: %s"),
			     MOUNTED_TEMP, strerror (errsv));
		}
	}

	discard_mntentchn(mc0);
	fd = fileno(mftmp->mntent_fp);

	/*
	 * It seems that better is incomplete and broken /mnt/mtab that
	 * /mnt/mtab that is writeable for non-root users.
	 *
	 * We always skip rename() when chown() and chmod() failed.
	 * -- kzak, 11-Oct-2007
	 */

	if (fchmod(fd, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) < 0) {
		int errsv = errno;
		fprintf(stderr, _("error changing mode of %s: %s\n"),
			MOUNTED_TEMP, strerror (errsv));
		goto leave;
	}

	/*
	 * If mount is setuid and some non-root user mounts sth,
	 * then mtab.tmp might get the group of this user. Copy uid/gid
	 * from the present mtab before renaming.
	 */
	if (stat(MOUNTED, &sbuf) == 0) {
		if (fchown(fd, sbuf.st_uid, sbuf.st_gid) < 0) {
			int errsv = errno;
			fprintf (stderr, _("error changing owner of %s: %s\n"),
				MOUNTED_TEMP, strerror(errsv));
			goto leave;
		}
	}

	my_endmntent (mftmp);

	/* rename mtemp to mtab */
	if (rename (MOUNTED_TEMP, MOUNTED) < 0) {
		int errsv = errno;
		fprintf(stderr, _("can't rename %s to %s: %s\n"),
			MOUNTED_TEMP, MOUNTED, strerror(errsv));
	}

 leave:
	unlock_mtab();
}


#ifdef MAIN_TEST_MTABLOCK

/*
 * This is mtab locking code test for:
 *
 *	- performance (how many concurrent processes)
 *
 *	- lock reliability (is possible to see corrupted data  if more
 *	                    concurrent processes modify a same file)
 *
 *  The test is very simple -- it reads a number from locked file, increments the
 *  number and writes the number back to the file.
 */

/* dummy */
int verbose;
int mount_quiet;
char *progname;

const char *fsprobe_get_label_by_devname(const char *spec) { return NULL; }
const char *fsprobe_get_uuid_by_devname(const char *spec) { return NULL; }
struct my_mntent *my_getmntent (mntFILE *mfp) { return NULL; }
mntFILE *my_setmntent (const char *file, char *mode) { return NULL; }
void my_endmntent (mntFILE *mfp) { }
int my_addmntent (mntFILE *mfp, struct my_mntent *mnt) { return 0; }
char *myrealpath(const char *path, char *resolved_path, int m) { return NULL; }

int
main(int argc, char **argv)
{
	time_t synctime;
	char *filename;
	int nloops, id, i;
	pid_t pid = getpid();
	unsigned int usecs;
	struct timeval tv;
	struct stat st;
	long last = 0;

	progname = argv[0];

	if (argc < 3)
		die(EXIT_FAILURE,
			"usage: %s <id> <synctime> <file> <nloops>\n",
			progname);

	id = atoi(argv[1]);
	synctime = (time_t) atol(argv[2]);
	filename = argv[3];
	nloops = atoi(argv[4]);

	if (stat(filename, &st) < -1)
		die(EXIT_FAILURE, "%s: %s\n", filename, strerror(errno));

	fprintf(stderr, "%05d (pid=%05d): START\n", id, pid);

	gettimeofday(&tv, NULL);
	if (synctime && synctime - tv.tv_sec > 1) {
		usecs = ((synctime - tv.tv_sec) * 1000000UL) -
					(1000000UL - tv.tv_usec);
		usleep(usecs);
	}

	for (i = 0; i < nloops; i++) {
		FILE *f;
		long num;
		char buf[256];

		lock_mtab();

		if (!(f = fopen(filename, "r"))) {
			unlock_mtab();
			die(EXIT_FAILURE, "ERROR: %d (pid=%d, loop=%d): "
					"open for read failed\n", id, pid, i);
		}
		if (!fgets(buf, sizeof(buf), f)) {
			unlock_mtab();
			die(EXIT_FAILURE, "ERROR: %d (pid=%d, loop=%d): "
					"read failed\n", id, pid, i);
		}
		fclose(f);

		num = atol(buf) + 1;

		if (!(f = fopen(filename, "w"))) {
			unlock_mtab();
			die(EXIT_FAILURE, "ERROR: %d (pid=%d, loop=%d): "
					"open for write failed\n", id, pid, i);
		}
		fprintf(f, "%ld", num);
		fclose(f);

		unlock_mtab();

		gettimeofday(&tv, NULL);

		fprintf(stderr, "%010ld.%06ld %04d (pid=%05d, loop=%05d): "
				"num=%09ld last=%09ld\n",
				tv.tv_sec, tv.tv_usec, id,
				pid, i, num, last);
		last = num;

		/* The mount command usually finish after mtab update. We
		 * simulate this via short sleep -- it's also enough to make
		 * concurrent processes happy.
		 */
		usleep(50000);
	}

	fprintf(stderr, "%05d (pid=%05d): DONE\n", id, pid);

	exit(EXIT_SUCCESS);
}
#endif

