/*
 * umount(8) for Linux 0.99 - jrs, 1993
 */

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include "mount_constants.h"
#include "sundries.h"
#include "getusername.h"
#include "pathnames.h"
#include "lomount.h"
#include "loop.h"
#include "fstab.h"
#include "env.h"
#include "nls.h"

#if defined(MNT_FORCE)
/* Interesting ... it seems libc knows about MNT_FORCE and presumably
   about umount2 as well -- need not do anything */
#else /* MNT_FORCE */

/* Does the present kernel source know about umount2? */
#include <linux/unistd.h>
#ifdef __NR_umount2

static int umount2(const char *path, int flags);

_syscall2(int, umount2, const char *, path, int, flags);

#else /* __NR_umount2 */

static int
umount2(const char *path, int flags) {
	fprintf(stderr, _("umount: compiled without support for -f\n"));
	errno = ENOSYS;
	return -1;
}
#endif /* __NR_umount2 */

#if !defined(MNT_FORCE)
/* dare not try to include <linux/mount.h> -- lots of errors */
#define MNT_FORCE 1
#endif

#endif /* MNT_FORCE */

#if !defined(MNT_DETACH)
#define MNT_DETACH 2
#endif


/* True if we are allowed to call /sbin/umount.${FSTYPE} */
int external_allowed = 1;

/* Nonzero for force umount (-f).  There is kernel support since 2.1.116.  */
int force = 0;

/* Nonzero for lazy umount (-l).  There is kernel support since 2.4.11.  */
int lazy = 0;

/* When umount fails, attempt a read-only remount (-r). */
int remount = 0;

/* Don't write a entry in /etc/mtab (-n).  */
int nomtab = 0;

/* Call losetup -d for each unmounted loop device. */
int delloop = 0;

/* True if (ruid != euid) or (0 != ruid), i.e. only "user" umounts permitted. */
int restricted = 1;

/* Last error message */
int complained_err = 0;
char *complained_dev = NULL;

/*
 * check_special_umountprog()
 *	If there is a special umount program for this type, exec it.
 * returns: 0: no exec was done, 1: exec was done, status has result
 */
static int
check_special_umountprog(const char *spec, const char *node,
			 const char *type, int *status) {
	char umountprog[120];
	struct stat statbuf;
	int res;

	if (!external_allowed)
		return 0;

	if (type == NULL || strcmp(type, "none") == 0)
		return 0;

	if (strlen(type) < 100) {
		sprintf(umountprog, "/sbin/umount.%s", type);
		if (stat(umountprog, &statbuf) == 0) {
			res = fork();
			if (res == 0) {
				char *umountargs[8];
				int i = 0;

				if(setgid(getgid()) < 0)
					die(EX_FAIL, _("umount: cannot set group id: %s"), strerror(errno));

				if(setuid(getuid()) < 0)
					die(EX_FAIL, _("umount: cannot set user id: %s"), strerror(errno));

				umountargs[i++] = umountprog;
				umountargs[i++] = xstrdup(node);
				if (nomtab)
					umountargs[i++] = "-n";
				if (lazy)
					umountargs[i++] = "-l";
				if (force)
					umountargs[i++] = "-f";
				if (verbose)
					umountargs[i++] = "-v";
				if (remount)
					umountargs[i++] = "-r";
				umountargs[i] = NULL;
				execv(umountprog, umountargs);
				exit(1);	/* exec failed */
			} else if (res != -1) {
				int st;
				wait(&st);
				*status = (WIFEXITED(st) ? WEXITSTATUS(st)
					   : EX_SYSERR);
				return 1;
			} else {
				int errsv = errno;
				error(_("umount: cannot fork: %s"),
				      strerror(errsv));
			}
		}
	}
	return 0;
}

/* complain about a failed umount */
static void complain(int err, const char *dev) {

  if (complained_err == err && complained_dev && dev &&
		  strcmp(dev, complained_dev) == 0)
    return;

  complained_err = err;
  free(complained_dev);
  complained_dev = xstrdup(dev);

  switch (err) {
    case ENXIO:
      error (_("umount: %s: invalid block device"), dev); break;
    case EINVAL:
      error (_("umount: %s: not mounted"), dev); break;
    case EIO:
      error (_("umount: %s: can't write superblock"), dev); break;
    case EBUSY:
     /* Let us hope fstab has a line "proc /proc ..."
	and not "none /proc ..."*/
      error (_("umount: %s: device is busy.\n"
	       "        (In some cases useful info about processes that use\n"
	       "         the device is found by lsof(8) or fuser(1))"), dev);
      break;
    case ENOENT:
      error (_("umount: %s: not found"), dev); break;
    case EPERM:
      error (_("umount: %s: must be superuser to umount"), dev); break;
    case EACCES:
      error (_("umount: %s: block devices not permitted on fs"), dev); break;
    default:
      error (_("umount: %s: %s"), dev, strerror (err)); break;
  }
}

/* Umount a single device.  Return a status code, so don't exit
   on a non-fatal error.  We lock/unlock around each umount.  */
static int
umount_one (const char *spec, const char *node, const char *type,
	    const char *opts, struct mntentchn *mc) {
	int umnt_err = 0;
	int isroot;
	int res;
	int status;
	const char *loopdev;
	int myloop = 0;

	/* Special case for root.  As of 0.99pl10 we can (almost) unmount root;
	   the kernel will remount it readonly so that we can carry on running
	   afterwards.  The readonly remount is illegal if any files are opened
	   for writing at the time, so we can't update mtab for an unmount of
	   root.  As it is only really a remount, this doesn't matter too
	   much.  [sct May 29, 1993] */
	isroot = (streq (node, "/") || streq (node, "root")
		  || streq (node, "rootfs"));
	if (isroot)
		nomtab++;

	/*
	 * Call umount.TYPE for types that require a separate umount program.
	 * All such special things must occur isolated in the types string.
	 */
	if (check_special_umountprog(spec, node, type, &status))
		return status;

	/*
	 * Ignore the option "-d" for non-loop devices and loop devices with
	 * LO_FLAGS_AUTOCLEAR flag.
	 */
	if (delloop && is_loop_device(spec) && !is_loop_autoclear(spec))
		myloop = 1;

	if (lazy) {
		res = umount2 (node, MNT_DETACH);
		if (res < 0)
			umnt_err = errno;
		goto writemtab;
	}

	if (force) {		/* only supported for NFS */
		res = umount2 (node, MNT_FORCE);
		if (res == -1) {
			int errsv = errno;
			perror("umount2");
			errno = errsv;
			if (errno == ENOSYS) {
				if (verbose)
					printf(_("no umount2, trying umount...\n"));
				res = umount (node);
			}
		}
	} else
		res = umount (node);

	if (res < 0)
		umnt_err = errno;

	if (res < 0 && remount && umnt_err == EBUSY) {
		/* Umount failed - let us try a remount */
		res = mount(spec, node, NULL,
			    MS_MGC_VAL | MS_REMOUNT | MS_RDONLY, NULL);
		if (res == 0) {
			struct my_mntent remnt;
			fprintf(stderr,
				_("umount: %s busy - remounted read-only\n"),
				spec);
			remnt.mnt_type = remnt.mnt_fsname = NULL;
			remnt.mnt_dir = xstrdup(node);
			remnt.mnt_opts = xstrdup("ro");
			if (!nomtab)
				update_mtab(node, &remnt);
			return 0;
		} else if (errno != EBUSY) { 	/* hmm ... */
			perror("remount");
			fprintf(stderr,
				_("umount: could not remount %s read-only\n"),
				spec);
		}
	}

	loopdev = 0;
	if (res >= 0) {
		/* Umount succeeded */
		if (verbose)
			printf (_("%s umounted\n"), spec);

		/* Free any loop devices that we allocated ourselves */
		if (mc) {
			char *optl;

			/* old style mtab line? */
			if (streq(mc->m.mnt_type, "loop")) {
				loopdev = spec;
				goto gotloop;
			}

			/* new style mtab line? */
			optl = mc->m.mnt_opts ? xstrdup(mc->m.mnt_opts) : "";
			for (optl = strtok (optl, ","); optl;
			     optl = strtok (NULL, ",")) {
				if (!strncmp(optl, "loop=", 5)) {
					loopdev = optl+5;
					goto gotloop;
				}
			}
		} else {
			/*
			 * If option "-o loop=spec" occurs in mtab,
			 * note the mount point, and delete mtab line.
			 */
			if ((mc = getmntoptfile (spec)) != NULL)
				node = mc->m.mnt_dir;
		}

		/* Also free loop devices when -d flag is given */
		if (myloop)
			loopdev = spec;
	}
 gotloop:
	if (loopdev)
		del_loop(loopdev);

 writemtab:
	if (!nomtab &&
	    (umnt_err == 0 || umnt_err == EINVAL || umnt_err == ENOENT)) {
		update_mtab (node, NULL);
	}

	if (res >= 0)
		return 0;
	if (umnt_err)
		complain(umnt_err, node);
	return 1;
}

/*
 * umount_one_bw: unmount FILE that has last occurrence MC0
 *
 * Why this loop?
 * 1. People who boot a system with a bad fstab root entry
 *    will get an incorrect "/dev/foo on /" in mtab.
 *    If later /dev/foo is actually mounted elsewhere,
 *    it will occur twice in mtab.
 * 2. With overmounting one can get the situation that
 *    the same filename is used as mount point twice.
 * In both cases, it is best to try the last occurrence first.
 */
static int
umount_one_bw (const char *file, struct mntentchn *mc0) {
	struct mntentchn *mc;
	int res = 1;

	mc = mc0;
	while (res && mc) {
		res = umount_one(mc->m.mnt_fsname, mc->m.mnt_dir,
				 mc->m.mnt_type, mc->m.mnt_opts, mc);
		mc = getmntdirbackward(file, mc);
	}
	mc = mc0;
	while (res && mc) {
		res = umount_one(mc->m.mnt_fsname, mc->m.mnt_dir,
				 mc->m.mnt_type, mc->m.mnt_opts, mc);
		mc = getmntdevbackward(file, mc);
	}
	return res;
}

/* Unmount all filesystems of type VFSTYPES found in mtab.  Since we are
   concurrently updating mtab after every successful umount, we have to
   slurp in the entire file before we start.  This isn't too bad, because
   in any case it's important to umount mtab entries in reverse order
   to mount, e.g. /usr/spool before /usr.  */
static int
umount_all (char *types, char *test_opts) {
     struct mntentchn *mc, *hd;
     int errors = 0;

     hd = mtab_head();
     if (!hd->prev)
	  die (2, _("umount: cannot find list of filesystems to unmount"));
     for (mc = hd->prev; mc != hd; mc = mc->prev) {
	  if (matching_type (mc->m.mnt_type, types)
	      && matching_opts (mc->m.mnt_opts, test_opts)) {
	       errors |= umount_one (mc->m.mnt_fsname, mc->m.mnt_dir,
				     mc->m.mnt_type, mc->m.mnt_opts, mc);
	  }
     }

     sync ();
     return errors;
}

static struct option longopts[] =
{
  { "all", 0, 0, 'a' },
  { "force", 0, 0, 'f' },
  { "help", 0, 0, 'h' },
  { "no-mtab", 0, 0, 'n' },
  { "test-opts", 1, 0, 'O' },
  { "verbose", 0, 0, 'v' },
  { "version", 0, 0, 'V' },
  { "read-only", 0, 0, 'r' },
  { "types", 1, 0, 't' },

  { "no-canonicalize", 0, 0, 144 },
  { NULL, 0, 0, 0 }
};

static void
usage (FILE *fp, int n)
{
  fprintf (fp, _("Usage: umount -h | -V\n"
	       "       umount -a [-d] [-f] [-r] [-n] [-v] [-t vfstypes] [-O opts]\n"
	       "       umount [-d] [-f] [-r] [-n] [-v] special | node...\n"));
  exit (n);
}

/*
 * Look for an option in a comma-separated list
 */
static int
contains(const char *list, const char *s) {
	int n = strlen(s);

	while (list && *list) {
		if (strncmp(list, s, n) == 0 &&
		    (list[n] == 0 || list[n] == ','))
			return 1;
		while (*list && *list++ != ',') ;
	}
	return 0;
}

/*
 * If list contains "user=peter" and we ask for "user=", return "peter"
 */
static char *
get_value(const char *list, const char *s) {
	const char *t;
	int n = strlen(s);

	while (list && *list) {
		if (strncmp(list, s, n) == 0) {
			s = t = list+n;
			while (*s && *s != ',')
				s++;
			return xstrndup(t, s-t);
		}
		while (*list && *list++ != ',') ;
	}
	return NULL;
}

/* check if @mc contains a loop device which is associated
 * with the @file in fs
 */
static int
is_valid_loop(struct mntentchn *mc, struct mntentchn *fs)
{
	unsigned long long offset = 0;
	char *p;

	/* check if it begins with /dev/loop */
	if (strncmp(mc->m.mnt_fsname, _PATH_DEV_LOOP,
				sizeof(_PATH_DEV_LOOP) - 1))
		return 0;

	/* check for loop option in fstab */
	if (!contains(fs->m.mnt_opts, "loop"))
		return 0;

	/* check for offset option in fstab */
	p = get_value(fs->m.mnt_opts, "offset=");
	if (p)
		offset = strtoull(p, NULL, 10);

	/* check association */
	if (loopfile_used_with((char *) mc->m.mnt_fsname,
				fs->m.mnt_fsname, offset) == 1) {
		if (verbose > 1)
			printf(_("device %s is associated with %s\n"),
			       mc->m.mnt_fsname, fs->m.mnt_fsname);
		return 1;
	}

	if (verbose > 1)
		printf(_("device %s is not associated with %s\n"),
		       mc->m.mnt_fsname, fs->m.mnt_fsname);
	return 0;
}

static int
umount_file (char *arg) {
	struct mntentchn *mc, *fs;
	const char *file, *options;
	int fstab_has_user, fstab_has_users, fstab_has_owner, fstab_has_group;
	int ok;

	if (!*arg) {		/* "" would be expanded to `pwd` */
		die(2, _("Cannot umount \"\"\n"));
		return 0;
	}

	file = canonicalize(arg); /* mtab paths are canonicalized */
	if (verbose > 1)
		printf(_("Trying to umount %s\n"), file);

	mc = getmntdirbackward(file, NULL);
	if (!mc) {
		mc = getmntdevbackward(file, NULL);
		if (mc) {
			struct mntentchn *mc1;

			mc1 = getmntdirbackward(mc->m.mnt_dir, NULL);
			if (!mc1)
				/* 'mc1' must exist, though not necessarily
				    equals to `mc'. Otherwise we go mad. */
				die(EX_SOFTWARE,
				    _("umount: confused when analyzing mtab"));

			if (strcmp(file, mc1->m.mnt_fsname)) {
				/* Something was stacked over `file' on the
				   same mount point. */
				die(EX_FAIL, _("umount: cannot umount %s -- %s is "
				    "mounted over it on the same point."),
				    file, mc1->m.mnt_fsname);
			}
		}
	}
	if (!mc && verbose)
		printf(_("Could not find %s in mtab\n"), file);

	if (restricted) {
		char *mtab_user = NULL;

		if (!mc)
			die(2,
			    _("umount: %s is not mounted (according to mtab)"),
			    file);
		/*
		 * uhelper - unprivileged umount helper
		 * -- external umount (for example HAL mounts)
		 */
		if (external_allowed) {
			char *uhelper = NULL;

			if (mc->m.mnt_opts)
				uhelper = get_value(mc->m.mnt_opts, "uhelper=");
			if (uhelper) {
				int status = 0;
				if (check_special_umountprog(arg, arg,
							uhelper, &status))
					return status;
			}
		}

		/* The 2.4 kernel will generally refuse to mount the same
		   filesystem on the same mount point, but will accept NFS.
		   So, unmounting must be possible. */
		if (!is_mounted_once(file) && strcmp(mc->m.mnt_type,"nfs"))
			die(2,
			    _("umount: it seems %s is mounted multiple times"),
			    file);

		/* If fstab contains the two lines
		   /dev/sda1 /mnt/zip auto user,noauto  0 0
		   /dev/sda4 /mnt/zip auto user,noauto  0 0
		   then "mount /dev/sda4" followed by "umount /mnt/zip"
		   used to fail. So, we must not look for file, but for
		   the pair (dev,file) in fstab. */
		fs = getfs_by_devdir(mc->m.mnt_fsname, mc->m.mnt_dir);
		if (!fs) {
			fs = getfs_by_dir(file);
			if (!fs && !getfs_by_spec(file))
				die (2,
				     _("umount: %s is not in the fstab "
				       "(and you are not root)"),
				     file);

			/* spec could be a file which is loop mounted */
			if (fs && !is_valid_loop(mc, fs))
				die (2, _("umount: %s mount disagrees with "
					  "the fstab"), file);
		}

		/*
		 * User mounting and unmounting is allowed only
		 * if fstab contains one of the options `user',
		 * `users' or `owner' or `group'.
		 *
		 * The option `users' allows arbitrary users to mount
		 * and unmount - this may be a security risk.
		 *
		 * The options `user', `owner' and `group' only allow
		 * unmounting by the user that mounted (visible in mtab).
		 */

		options = fs->m.mnt_opts;
		if (!options)
			options = "";
		fstab_has_user = contains(options, "user");
		fstab_has_users = contains(options, "users");
		fstab_has_owner = contains(options, "owner");
		fstab_has_group = contains(options, "group");
		ok = 0;

		if (fstab_has_users)
			ok = 1;

		if (!ok && (fstab_has_user || fstab_has_owner ||
			    fstab_has_group)) {
			char *user = getusername();

			options = mc->m.mnt_opts;
			if (!options)
				options = "";
			mtab_user = get_value(options, "user=");

			if (user && mtab_user && streq (user, mtab_user))
				ok = 1;
		}
		if (!ok)
			die (2, _("umount: only %s can unmount %s from %s"),
			     mtab_user ? mtab_user : "root",
			     fs->m.mnt_fsname, fs->m.mnt_dir);
	}

	if (mc)
		return umount_one_bw (file, mc);
	else
		return umount_one (arg, arg, arg, arg, NULL);
}

int
main (int argc, char *argv[]) {
	int c;
	int all = 0;
	char *types = NULL, *test_opts = NULL, *p;
	int result = 0;

	sanitize_env();
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	progname = argv[0];
	if ((p = strrchr(progname, '/')) != NULL)
		progname = p+1;

	umask(022);

	while ((c = getopt_long (argc, argv, "adfhlnrit:O:vV",
				 longopts, NULL)) != -1)
		switch (c) {
		case 'a':		/* umount everything */
			++all;
			break;
			/* fall through? */
		case 'd':		/* do losetup -d for unmounted loop devices */
			++delloop;
			break;
		case 'f':		/* force umount */
			++force;
			break;
		case 'h':		/* help */
			usage (stdout, 0);
			break;
		case 'l':		/* lazy umount */
			++lazy;
			break;
		case 'n':		/* do not write in /etc/mtab */
			++nomtab;
			break;
		case 'O':		/* specify file system options */
			test_opts = optarg;
			break;
		case 'r':		/* remount read-only if umount fails */
			++remount;
			break;
		case 'v':		/* make noise */
			++verbose;
			break;
		case 'V':		/* version */
			printf ("umount (%s)\n", PACKAGE_STRING);
			exit (0);
		case 't':		/* specify file system type */
			types = optarg;
			break;
		case 'i':
			external_allowed = 0;
			break;
		case 144:
			nocanonicalize = 1;
			break;
		case 0:
			break;
		case '?':
		default:
			usage (stderr, 1);
		}

	{
		const uid_t ruid = getuid();
		const uid_t euid = geteuid();

		/* if we're really root and aren't running setuid */
		if (((uid_t)0 == ruid) && (ruid == euid)) {
			restricted = 0;
		}
	}

	if (restricted &&
	    (all || types || nomtab || force || remount || nocanonicalize)) {
		die (2, _("umount: only root can do that"));
	}

	argc -= optind;
	argv += optind;

	atexit(unlock_mtab);

	if (all) {
		/* nodev stuff: sysfs, usbfs, oprofilefs, ... */
		if (types == NULL)
			types = "noproc,nodevfs,nodevpts,nosysfs,norpc_pipefs,nonfsd";
		result = umount_all (types, test_opts);
	} else if (argc < 1) {
		usage (stderr, 2);
	} else while (argc--) {
		result += umount_file(*argv++);
	}
	exit (result);		/* nonzero on at least one failure */
}
