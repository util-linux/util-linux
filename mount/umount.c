/*
 * A umount(8) for Linux 0.99.
 * umount.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 *
 * Wed Sep 14 22:43:54 1994: Sebastian Lederer
 * (lederer@next-pc.informatik.uni-bonn.de) added support for sending an
 * unmount RPC call to the server when an NFS-filesystem is unmounted.
 *
 * Tue Sep 26 16:33:09 1995: Added patches from Greg Page (greg@caldera.com)
 * so that NetWare filesystems can be unmounted.
 *
 * 951213: Marek Michalkiewicz <marekm@i17linuxb.ists.pwr.wroc.pl>:
 * Ignore any RPC errors, so that you can umount an nfs mounted filesystem
 * if the server is down.
 *
 * 960223: aeb - several minor changes
 * 960324: aeb - added some changes from Rob Leslie <rob@mars.org>
 * 960823: aeb - also try umount(spec) when umount(node) fails
 * 970307: aeb - canonise names from fstab
 * 970726: aeb - remount read-only in cases where umount fails
 * 980810: aeb - umount2 support
 * 981222: aeb - If mount point or special file occurs several times
 *               in mtab, try them all, with last one tried first
 *             - Differentiate "user" and "users" key words in fstab
 */

#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include "mount_constants.h"
#include "sundries.h"
#include "getusername.h"
#include "lomount.h"
#include "loop.h"
#include "fstab.h"

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

static int umount2(const char *path, int flags);

#ifdef MNT_FORCE
/* Interesting ... it seems libc knows about MNT_FORCE and presumably
   about umount2 as well -- need not do anything */
#else /* MNT_FORCE */

/* Does the present kernel source know about umount2? */
#include <linux/unistd.h>
#ifdef __NR_umount2
_syscall2(int, umount2, const char *, path, int, flags);
#else /* __NR_umount2 */
static int
umount2(const char *path, int flags) {
	fprintf(stderr, "umount: compiled without support for -f\n");
	errno = ENOSYS;
	return -1;
}
#endif /* __NR_umount2 */

/* dare not try to include <linux/mount.h> -- lots of errors */
#define MNT_FORCE 1

#endif /* MNT_FORCE */

/* Nonzero for force umount (-f).  There is kernel support since 2.1.116.  */
int force = 0;

/* When umount fails, attempt a read-only remount (-r). */
int remount = 0;

/* Don't write a entry in /etc/mtab (-n).  */
int nomtab = 0;

/* Nonzero for chatty (-v).  This is a nonstandard flag (not in BSD).  */
int verbose = 0;

/* True if ruid != euid.  */
int suid = 0;

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
      printf("host: %s, directory: %s\n", hostname, dirname);
#endif

      if (opts && (p = strstr(opts, "addr="))) {
	   char *q;

	   free(hostname);
	   p += 5;
	   q = p;
	   while (*q && *q != ',') q++;
	   hostname = xstrndup(p,q-p);
      }

      if (hostname[0] >= '0' && hostname[0] <= '9')
	   saddr.sin_addr.s_addr = inet_addr(hostname);
      else {
	   if ((hostp = gethostbyname(hostname)) == NULL) {
		fprintf(stderr, "umount: can't get address for %s\n",
			hostname);
		return 1;
	   }
	   if (hostp->h_length > sizeof(struct in_addr)) {
		fprintf(stderr, "umount: got bad hostp->h_length\n");
		hostp->h_length = sizeof(struct in_addr);
	   }
	   memcpy(&saddr.sin_addr, hostp->h_addr, hostp->h_length);
      }

      saddr.sin_family = AF_INET;
      saddr.sin_port = 0;
      pertry.tv_sec = 3;
      pertry.tv_usec = 0;
      if ((clp = clntudp_create(&saddr, MOUNTPROG, MOUNTVERS,
				pertry, &so)) == NULL) {
	   clnt_pcreateerror("Cannot MOUNTPROG RPC");
	   return (1);
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
	   return (1);
      }
      auth_destroy(clp->cl_auth);
      clnt_destroy(clp);

      return (0);
}
#endif /* HAVE_NFS */

/* complain about a failed umount */
static void complain(int err, const char *dev) {
  switch (err) {
    case ENXIO:
      error ("umount: %s: invalid block device", dev); break;
    case EINVAL:
      error ("umount: %s: not mounted", dev); break;
    case EIO:
      error ("umount: %s: can't write superblock", dev); break;
    case EBUSY:
     /* Let us hope fstab has a line "proc /proc ..."
	and not "none /proc ..."*/
      error ("umount: %s: device is busy", dev); break;
    case ENOENT:
      error ("umount: %s: not found", dev); break;
    case EPERM:
      error ("umount: %s: must be superuser to umount", dev); break;
    case EACCES:
      error ("umount: %s: block devices not permitted on fs", dev); break;
    default:
      error ("umount: %s: %s", dev, strerror (err)); break;
  }
}

/* Umount a single device.  Return a status code, so don't exit
   on a non-fatal error.  We lock/unlock around each umount.  */
static int
umount_one (const char *spec, const char *node, const char *type,
	    const char *opts, struct mntentchn *mc)
{
  int umnt_err, umnt_err2;
  int isroot;
  int res;

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

#ifdef HAVE_NFS
  /* Ignore any RPC errors, so that you can umount the filesystem
     if the server is down.  */
  if (strcasecmp(type, "nfs") == 0)
	  nfs_umount_rpc_call(spec, opts);
#endif
 

  umnt_err = umnt_err2 = 0;
  if (force) {
       /* completely untested - 2.1.116 only has some support in nfs case */
       /* probably this won't work */
       int flags = MNT_FORCE;

       res = umount2 (node, flags);
       if (res == -1) {
	       perror("umount2");
	       if (errno == ENOSYS) {
		       if (verbose)
			       printf("no umount2, trying umount...\n");
		       res = umount (node);
	       }
       }
  } else
       res = umount (node);
  if (res < 0) {
       umnt_err = errno;
       /* A device might have been mounted on a node that has since
	  been deleted or renamed, so if node fails, also try spec. */
       /* if (umnt_err == ENOENT || umnt_err == EINVAL) */
       if (umnt_err != EBUSY && strcmp(node, spec)) {
	    if (verbose)
		 printf ("could not umount %s - trying %s instead\n",
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
       res=mount(spec, node, NULL, MS_MGC_VAL | MS_REMOUNT | MS_RDONLY, NULL);
       if (res == 0) {
	    struct mntent remnt;
	    fprintf(stderr, "umount: %s busy - remounted read-only\n", spec);
	    remnt.mnt_type = remnt.mnt_fsname = NULL;
	    remnt.mnt_dir = xstrdup(node);
	    remnt.mnt_opts = "ro";
	    update_mtab(node, &remnt);
	    return 0;
       } else if (errno != EBUSY) { 	/* hmm ... */
	    perror("remount");
	    fprintf(stderr, "umount: could not remount %s read-only\n",
		    spec);
       }
  }

  if (res >= 0) {
      /* Umount succeeded, update mtab.  */
      if (verbose)
	printf ("%s umounted\n", spec);

      if (!nomtab && mtab_is_writable()) {
				/* Special stuff for loop devices */
	  if (mc) {
	     char *optl;

	     /* old style mtab line? */
	     if (streq(mc->mnt_type, "loop"))
		if (del_loop(spec))
		      goto fail;

	     /* new style mtab line? */
	     optl = mc->mnt_opts ? xstrdup(mc->mnt_opts) : "";
	     for (optl = strtok (optl, ","); optl; optl = strtok (NULL, ",")) {
		 if (!strncmp(optl, "loop=", 5)) {
		     if (del_loop(optl+5))
		       goto fail;
		     break;
		 }
	     }
	  } else {
	      /* maybe spec is a loop device? */
	      /* no del_loop() - just delete it from mtab */
	      if ((mc = getmntoptfile (spec)) != NULL)
		node = mc->mnt_dir;
	  }

				/* Non-loop stuff */
	  update_mtab (node, NULL);
      }
      return 0;
  }

fail:
  /* Umount or del_loop failed, complain, but don't die.  */
  if (!nomtab) {
      /* remove obsolete entry */
      if (umnt_err == EINVAL || umnt_err == ENOENT)
	  update_mtab (node, NULL);
  }

  if (umnt_err2)
       complain(umnt_err2, spec);
  if (umnt_err && umnt_err != umnt_err2)
       complain(umnt_err, node);
  return 1;
}

/*
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
umount_one_bw (const char *file, struct mntentchn *mc) {
     int res = 1;

     while (res && mc) {
	  res = umount_one(mc->mnt_fsname, mc->mnt_dir,
			   mc->mnt_type, mc->mnt_opts, mc);
	  mc = getmntfilesbackward (file, mc);
     }
     return res;
}

/* Unmount all filesystems of type VFSTYPES found in mtab.  Since we are
   concurrently updating mtab after every succesful umount, we have to
   slurp in the entire file before we start.  This isn't too bad, because
   in any case it's important to umount mtab entries in reverse order
   to mount, e.g. /usr/spool before /usr.  */
static int
umount_all (string_list types) {
     struct mntentchn *mc, *hd;
     int errors = 0;

     hd = mtab_head();
     if (!hd->prev)
	  die (2, "umount: cannot find list of filesystems to unmount");
     for (mc = hd->prev; mc != hd; mc = mc->prev) {
	  if (matching_type (mc->mnt_type, types)) {
	       errors |= umount_one (mc->mnt_fsname, mc->mnt_dir,
				     mc->mnt_type, mc->mnt_opts, mc);
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
  { "verbose", 0, 0, 'v' },
  { "version", 0, 0, 'V' },
  { "read-only", 0, 0, 'r' },
  { "types", 1, 0, 't' },
  { NULL, 0, 0, 0 }
};

char *usage_string = "\
Usage: umount [-hV]\n\
       umount -a [-f] [-r] [-n] [-v] [-t vfstypes]\n\
       umount [-f] [-r] [-n] [-v] special | node...\n\
";

static void
usage (FILE *fp, int n)
{
  fprintf (fp, "%s", usage_string);
  exit (n);
}

int mount_quiet = 0;

int
main (int argc, char *argv[])
{
  int c;
  int all = 0;
  string_list types = NULL;
  string_list options;
  struct mntentchn *mc, *fs;
  char *file;
  int result = 0;

  while ((c = getopt_long (argc, argv, "afhnrt:vV",
			   longopts, NULL)) != EOF)
    switch (c) {
      case 'a':			/* umount everything */
	++all;
	break;
      case 'f':			/* force umount */
	++force;
	break;
      case 'h':			/* help */
	usage (stdout, 0);
	break;
      case 'n':			/* do not write in /etc/mtab */
	++nomtab;
	break;
      case 'r':			/* remount read-only if umount fails */
	++remount;
	break;
      case 'v':			/* make noise */
	++verbose;
	break;
      case 'V':			/* version */
	printf ("umount: %s\n", version);
	exit (0);
      case 't':			/* specify file system type */
	types = parse_list (optarg);
	break;
      case 0:
	break;
      case '?':
      default:
	usage (stderr, 1);
    }

  if (getuid () != geteuid ())
    {
      suid = 1;
      if (all || types || nomtab || force)
	die (2, "umount: only root can do that");
    }

  argc -= optind;
  argv += optind;

  if (all) {
       if (types == NULL)
	  types = parse_list(xstrdup("noproc"));
       result = umount_all (types);
  } else if (argc < 1) {
       usage (stderr, 2);
  } else while (argc--) {
       file = canonicalize (*argv); /* mtab paths are canonicalized */
       if (verbose > 1)
	  printf("Trying to umount %s\n", file);

       mc = getmntfilesbackward (file, NULL);
       if (!mc && verbose)
	  printf("Could not find %s in mtab\n", file);

       if (suid) {
	  if (!mc)
	    die (2, "umount: %s is not mounted (according to mtab)", file);
	  if (getmntfilesbackward (file, mc))
	    die (2, "umount: it seems %s is mounted multiple times", file);
	  if (!(fs = getfsspec (file)) && !(fs = getfsfile (file)))
	    die (2, "umount: %s is not in the fstab (and you are not root)",
		 file);
	  if ((!streq (mc->mnt_fsname, fs->mnt_fsname) &&
	       !streq (mc->mnt_fsname, canonicalize (fs->mnt_fsname)))
	      || (!streq (mc->mnt_dir, fs->mnt_dir) &&
		  !streq (mc->mnt_dir, canonicalize (fs->mnt_dir)))) {
	    die (2, "umount: %s mount disagrees with the fstab", file);
	  }

	  /* User mounting and unmounting is allowed only
	     if fstab contains the option `user' or `users' */
	  /* The option `users' allows arbitrary users to mount
	     and unmount - this may be a security risk. */
	  /* The option `user' only allows unmounting by the user
	     that mounted. */
	  /* A convenient side effect is that the user who mounted
	     is visible in mtab. */
	  options = parse_list (fs->mnt_opts);
	  while (options) {
	      if (streq (car (options), "user") ||
		  streq (car (options), "users"))
		break;
	      options = cdr (options);
	  }
	  if (!options)
	    die (2, "umount: only root can unmount %s from %s",
		 fs->mnt_fsname, fs->mnt_dir);
	  if (streq (car (options), "user")) {
	      char *user = getusername();

	      options = parse_list (mc->mnt_opts);
	      while (options) {
		  char *co = car (options);
		  if (!strncmp(co, "user=", 5)) {
		      if (!user || !streq(co+5,user))
			  die(2, "umount: only %s can unmount %s from %s",
			      co+5, fs->mnt_fsname, fs->mnt_dir);
		      break;
		  }
		  options = cdr (options);
	      }
	  }
       }

       if (mc)
	    result = umount_one_bw (file, mc);
       else
	    result = umount_one (*argv, *argv, *argv, *argv, NULL);

       argv++;

  }
  exit (result);
}

