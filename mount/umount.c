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
 */

#include "sundries.h"

#ifdef HAVE_NFS
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_prot.h>
#include "mount.h"
#include <arpa/inet.h>
#endif


#ifdef notyet
/* Nonzero for force umount (-f).  This needs kernel support we don't have.  */
int force = 0;
#endif

/* Nonzero for chatty (-v).  This is a nonstandard flag (not in BSD).  */
int verbose = 0;

/* True if ruid != euid.  */
int suid = 0;

#ifdef HAVE_NFS
static int xdr_dir(XDR *xdrsp, char *dirp)
{
      return (xdr_string(xdrsp, &dirp, MNTPATHLEN));
}
#endif



/* Umount a single device.  Return a status code, so don't exit
   on a non-fatal error.  We lock/unlock around each umount.  */
static int
umount_one (const char *spec, const char *node, const char *type)
{
  int umnt_err;
  int isroot;
  struct mntent *mnt;

#ifdef HAVE_NFS
      char buffer[256];
      register CLIENT *clp;
      struct sockaddr_in saddr;
      struct timeval pertry, try;
      enum clnt_stat clnt_stat;
      int so = RPC_ANYSOCK;
      char *p;
      struct hostent *hostp;
      char hostname[MAXHOSTNAMELEN];
      char dirname[1024];
#endif /* HAVE_NFS */
  

  /* Special case for root.  As of 0.99pl10 we can (almost) unmount root;
     the kernel will remount it readonly so that we can carry on running
     afterwards.  The readonly remount is illegal if any files are opened
     for writing at the time, so we can't update mtab for an unmount of
     root.  As it is only really a remount, this doesn't matter too
     much.  [sct May 29, 1993] */
  isroot = (streq (node, "/") || streq (node, "root"));

#ifdef HAVE_NFS
      strcpy(buffer,spec);
              /* spec is constant so must use own buffer */
      if(!strcasecmp(type, "nfs") && (p=strchr(buffer,':')))
      {
              *p='\0';
              strcpy(hostname,buffer);
              strcpy(dirname,p+1);
#ifdef DEBUG
              printf("host: %s, directory: %s\n", hostname,dirname);
#endif


              if (hostname[0] >= '0' && hostname[0] <= '9')
              {
                      saddr.sin_addr.s_addr = inet_addr(hostname);
              }
              else
              if ((hostp = gethostbyname(hostname)) == NULL)
              {
                      fprintf(stderr, "mount: can't get address for %s\n", hostname);
                      return(1);
              }
              else
              {
                              memcpy(&saddr.sin_addr, hostp->h_addr, hostp->h_length);
              }

              saddr.sin_family = AF_INET;
              saddr.sin_port = 0;
              pertry.tv_sec = 3;
              pertry.tv_usec = 0;
              if ((clp = clntudp_create(&saddr, MOUNTPROG, MOUNTVERS,
                          pertry, &so)) == NULL)
                      {
                              clnt_pcreateerror("Cannot MOUNTPROG PRC");
                              return (1);
                      }
              clp->cl_auth = authunix_create_default();
              try.tv_sec = 20;
              try.tv_usec = 0;
              clnt_stat = clnt_call(clp, MOUNTPROC_UMNT,
                              xdr_dir, dirname,
                              xdr_void, (caddr_t)0,
                              try);

              if (clnt_stat != RPC_SUCCESS)
              {
                      clnt_perror(clp, "Bad UMNT RPC");
                      return (1);
              }
              auth_destroy(clp->cl_auth);
              clnt_destroy(clp);
      }
#endif /* HAVE_NFS */
 
  if (!isroot)
	  lock_mtab ();

  if (umount (node) >= 0)
    /* Umount succeeded, update mtab.  */
    {
      if (verbose)
	printf ("%s umounted\n", spec);

      if (!isroot)
	{
				/* Special stuff for loop devices */
	  open_mtab("r");
	  if ((mnt = getmntfile (spec)) ||
	      (mnt = getmntfile (node))) {
	     if (mnt && streq(mnt->mnt_type, "loop")) {
		extern int del_loop(const char *);
		 
		if (del_loop(spec))
		      goto fail;
	     }
	  }
	  close_mtab();

				/* Non-loop stuff */
	  update_mtab (node, NULL);
	  unlock_mtab ();
	}
      return 0;
    }

fail:
  /* Umount failed, complain, but don't die.  */
  umnt_err = errno;
  if (!isroot)
	  unlock_mtab ();

  switch (umnt_err)
    {
    case ENXIO:
      error ("umount: %s: invalid block device", spec); break;
    case EINVAL:
      error ("umount: %s: not mounted", spec); break;
    case EIO:
      error ("umount: %s: can't write superblock", spec); break;
    case EBUSY:
     /* Let us hope fstab has a line "proc /proc ..."
	and not "none /proc ..."*/
     error ("umount: %s: device is busy", spec); break;
    case ENOENT:
      error ("umount: %s: not mounted", spec); break;
    case EPERM:
      error ("umount: %s: must be superuser to umount", spec); break;
    case EACCES:
      error ("umount: %s: block devices not permitted on fs", spec); break;
    default:
      error ("umount: %s: %s", spec, strerror (umnt_err)); break;
    }
  return 1;
}

/* Unmount all filesystems of type VFSTYPES found in mtab.  Since we are
   concurrently updating mtab after every succesful umount, we have to
   slurp in the entire file before we start.  This isn't too bad, because
   in any case it's important to umount mtab entries in reverse order
   to umount, e.g. /usr/spool before /usr.  */
static int
umount_all (string_list types)
{
  string_list spec_list = NULL;
  string_list node_list = NULL;
  string_list type_list = NULL;
  struct mntent *mnt;
  int errors;

  open_mtab ("r");

  while ((mnt = getmntent (F_mtab)))
    if (matching_type (mnt->mnt_type, types))
      {
	spec_list = cons (xstrdup (mnt->mnt_fsname), spec_list);
	node_list = cons (xstrdup (mnt->mnt_dir), node_list);
        type_list = cons (xstrdup (mnt->mnt_type), type_list);
      }

  close_mtab ();

  errors = 0;
  while (spec_list != NULL)
    {
      errors |= umount_one (car (spec_list), car (node_list), car (type_list));
      spec_list = cdr (spec_list);
      node_list = cdr (node_list);
      type_list = cdr (type_list);
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
  { "verbose", 0, 0, 'v' },
  { "version", 0, 0, 'V' },
  { "types", 1, 0, 't' },
  { NULL, 0, 0, 0 }
};

char *usage_string = "\
usage: umount [-hV]\n\
       umount -a [-v] [-t vfstypes]\n\
       umount [-v] special | node...\n\
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
  struct mntent *mnt;
  struct mntent mntbuf;
  struct mntent *fs;
  char *file;
  int result = 0;

  while ((c = getopt_long (argc, argv, "afhvVt:", longopts, NULL)) != EOF)
    switch (c)
      {
      case 'a':			/* umount everything */
	++all;
	break;
      case 'f':			/* force umount (needs kernel support) */
#if 0
	++force;
#else
	die (2, "umount: forced umount not supported yet");
#endif
	break;
      case 'h':			/* help */
	usage (stdout, 0);
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
      if (all || types)
	die (2, "umount: only root can do that");
    }

  argc -= optind;
  argv += optind;

  if (all)
    result = umount_all (types);
  else if (argc < 1)
    usage (stderr, 2);
  else while (argc--)
    {
      file = canonicalize (*argv); /* mtab paths are canonicalized */

      open_mtab ("r");
      mnt = getmntfile (file);
      if (mnt)
	{
	  /* Copy the structure and strings becuase they're in static areas. */
	  mntbuf = *mnt;
	  mnt = &mntbuf;
	  mnt->mnt_fsname = xstrdup (mnt->mnt_fsname);
	  mnt->mnt_dir = xstrdup (mnt->mnt_dir);
	}
      close_mtab ();

      if (suid)
	{
	  if (!mnt)
	    die (2, "umount: %s is not mounted (according to mtab)", file);
	  if (!(fs = getfsspec (file)) && !(fs = getfsfile (file)))
	    die (2, "umount: %s is not in the fstab", file);
	  if (!streq (mnt->mnt_fsname, fs->mnt_fsname)
	      || !streq (mnt->mnt_dir, fs->mnt_dir))
	    die (2, "umount: %s mount disagrees with the fstab", file);
	  options = parse_list (fs->mnt_opts);
	  while (options)
	    {	
	      if (streq (car (options), "user"))
		break;
	      options = cdr (options);
	    }
	  if (!options)
	    die (2, "umount: only root can unmount %s from %s",
		 fs->mnt_fsname, fs->mnt_dir);
	}

      if (mnt)
	 result = umount_one (xstrdup (mnt->mnt_fsname), xstrdup(mnt->mnt_dir),
			      xstrdup(mnt->mnt_type));
      else
	 result = umount_one (*argv, *argv, *argv);

    }
  exit (result);
}
