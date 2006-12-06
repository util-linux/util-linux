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
#include "lomount.h"
#include "loop.h"
#include "fstab.h"
#include "env.h"
#include "nls.h"

#ifdef HAVE_NFS
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_prot.h>
#include "nfsmount.h"
#include <arpa/inet.h>
#endif

#if defined(MNT_FORCE) && !defined(__sparc__) && !defined(__arm__)
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

/* Nonzero for chatty (-v). */
int verbose = 0;

/* True if ruid != euid.  */
int suid = 0;

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

	if (type && strlen(type) < 100) {
		sprintf(umountprog, "/sbin/umount.%s", type);
		if (stat(umountprog, &statbuf) == 0) {
			res = fork();
			if (res == 0) {
				char *umountargs[8];
				int i = 0;

				setuid(getuid());
				setgid(getgid());
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

#ifdef HAVE_NFS
static int xdr_dir(XDR *xdrsp, char *dirp)
{
      return (xdr_string(xdrsp, &dirp, MNTPATHLEN));
}

static int
nfs_umount_rpc_call(const char *spec, const char *opts)
{
      register CLIENT *clp;
      struct sockaddr_in saddr;
      struct timeval pertry, try;
      enum clnt_stat clnt_stat;
      int port = 0;
      int so = RPC_ANYSOCK;
      struct hostent *hostp;
      char *hostname;
      char *dirname;
      char *p;

      if (spec == NULL || (p = strchr(spec,':')) == NULL)
		return 0;
      hostname = xstrndup(spec, p-spec);
      dirname = xstrdup(p+1);
#ifdef DEBUG
      printf(_("host: %s, directory: %s\n"), hostname, dirname);
#endif

      if (opts && (p = strstr(opts, "addr="))) {
	   char *q;

	   free(hostname);
	   p += 5;
	   q = p;
	   while (*q && *q != ',') q++;
	   hostname = xstrndup(p,q-p);
      }

      if (opts && (p = strstr(opts, "mountport=")) && isdigit(*(p+10)))
	   port = atoi(p+10);

      if (hostname[0] >= '0' && hostname[0] <= '9')
	   saddr.sin_addr.s_addr = inet_addr(hostname);
      else {
	   if ((hostp = gethostbyname(hostname)) == NULL) {
		fprintf(stderr, _("umount: can't get address for %s\n"),
			hostname);
		return 1;
	   }
	   if (hostp->h_length > sizeof(struct in_addr)) {
		fprintf(stderr, _("umount: got bad hostp->h_length\n"));
		hostp->h_length = sizeof(struct in_addr);
	   }
	   memcpy(&saddr.sin_addr, hostp->h_addr, hostp->h_length);
      }

      saddr.sin_family = AF_INET;
      saddr.sin_port = htons(port);
      pertry.tv_sec = 3;
      pertry.tv_usec = 0;
      if (opts && (p = strstr(opts, "tcp"))) {
	   /* possibly: make sure option is not "notcp"
	      possibly: try udp if tcp fails */
	   if ((clp = clnttcp_create(&saddr, MOUNTPROG, MOUNTVERS,
				     &so, 0, 0)) == NULL) {
		clnt_pcreateerror("Cannot MOUNTPROG RPC (tcp)");
		return 1;
	   }
      } else {
           if ((clp = clntudp_create(&saddr, MOUNTPROG, MOUNTVERS,
				     pertry, &so)) == NULL) {
		clnt_pcreateerror("Cannot MOUNTPROG RPC");
		return 1;
	   }
      }
      clp->cl_auth = authunix_create_default();
      try.tv_sec = 20;
      try.tv_usec = 0;
      clnt_stat = clnt_call(clp, MOUNTPROC_UMNT,
			    (xdrproc_t) xdr_dir, dirname,
			    (xdrproc_t) xdr_void, (caddr_t) 0,
			    try);

      if (clnt_stat != RPC_SUCCESS) {
	   clnt_perror(clp, "Bad UMNT RPC");
	   return 1;
      }
      auth_destroy(clp->cl_auth);
      clnt_destroy(clp);

      return 0;
}
#endif /* HAVE_NFS */

/* complain about a failed umount */
static void complain(int err, const char *dev) {
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
      error (_("umount: %s: device is busy"), dev); break;
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
	int umnt_err, umnt_err2;
	int isroot;
	int res;
	int status;
	const char *loopdev;

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

#ifdef HAVE_NFS
	/* Ignore any RPC errors, so that you can umount the filesystem
	   if the server is down.  */
	if (strcasecmp(type, "nfs") == 0)
		nfs_umount_rpc_call(spec, opts);
#endif
 
	umnt_err = umnt_err2 = 0;
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

	if (res < 0) {
		umnt_err = errno;
		/* A device might have been mounted on a node that has since
		   been deleted or renamed, so if node fails, also try spec. */
		/* Note that this is incorrect in case spec was mounted
		   several times. */
		/* if (umnt_err == ENOENT || umnt_err == EINVAL) */
		if (umnt_err != EBUSY && strcmp(node, spec)) {
			if (verbose)
				printf (_("could not umount %s - trying %s instead\n"),
					node, spec);
			res = umount (spec);
			if (res < 0)
				umnt_err2 = errno;
			/* Do not complain about remote NFS mount points */
			if (errno == ENOENT && index(spec, ':'))
				umnt_err2 = 0;
		}
	}

	if (res < 0 && remount && (umnt_err == EBUSY || umnt_err2 == EBUSY)) {
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
		if (delloop && is_loop_device(spec))
			loopdev = spec;
	}
 gotloop:
	if (loopdev)
		del_loop(loopdev);

 writemtab:
	if (!nomtab && mtab_is_writable() &&
	    (umnt_err == 0 || umnt_err == EINVAL || umnt_err == ENOENT)) {
		update_mtab (node, NULL);
	}

	if (res >= 0)
		return 0;

	if (umnt_err2)
		complain(umnt_err2, spec);
	if (umnt_err && umnt_err != umnt_err2)
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

extern char version[];
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
  { NULL, 0, 0, 0 }
};

static void
usage (FILE *fp, int n)
{
  fprintf (fp, _("Usage: umount [-hV]\n"
	       "       umount -a [-f] [-r] [-n] [-v] [-t vfstypes] [-O opts]\n"
	       "       umount [-f] [-r] [-n] [-v] special | node...\n"));
  exit (n);
}

int mount_quiet = 0;

/*
 * Look for an option in a comma-separated list
 */
static int
contains(const char *list, const char *s) {
	int n = strlen(s);

	while (*list) {
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

	while (*list) {
		if (strncmp(list, s, n) == 0) {
			s = t = list+n;
			while (*s && *s != ',')
				s++;
			return xstrndup(t, s-t);
		}
		while (*list && *list++ != ',') ;
	}
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
	if (!mc)
		mc = getmntdevbackward(file, NULL);
	if (!mc && verbose)
		printf(_("Could not find %s in mtab\n"), file);

	if (suid) {
		char *mtab_user = NULL;

		if (!mc)
			die(2,
			    _("umount: %s is not mounted (according to mtab)"),
			    file);
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
		   the pair (spec,file) in fstab. */
		fs = getfsspecfile(mc->m.mnt_fsname, mc->m.mnt_dir);
		if (!fs) {
			if (!getfsspec (file) && !getfsfile (file))
				die (2,
				     _("umount: %s is not in the fstab "
				       "(and you are not root)"),
				     file);
			else
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

char *progname;

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
			printf ("umount: %s\n", version);
			exit (0);
		case 't':		/* specify file system type */
			types = optarg;
			break;
		case 'i':
  			external_allowed = 0;
			break;
		case 0:
			break;
		case '?':
		default:
			usage (stderr, 1);
		}

	if (getuid () != geteuid ()) {
		suid = 1;
		if (all || types || nomtab || force || remount)
			die (2, _("umount: only root can do that"));
	}

	argc -= optind;
	argv += optind;

	if (all) {
		/* nodev stuff: sysfs, usbfs, oprofilefs, ... */
		if (types == NULL)
			types = "noproc,nodevfs,nodevpts";
		result = umount_all (types, test_opts);
	} else if (argc < 1) {
		usage (stderr, 2);
	} else while (argc--) {
		result += umount_file(*argv++);
	}
	exit (result);		/* nonzero on at least one failure */
}
