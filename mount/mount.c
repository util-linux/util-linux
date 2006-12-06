/*
 * A mount(8) for Linux 0.99.
 * mount.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 *
 * Thu Jul 14 07:32:40 1994: faith@cs.unc.edu added changed from Adam
 * J. Richter (adam@adam.yggdrasil.com) so that /proc/filesystems is used
 * if no -t option is given.  I modified his patches so that, if
 * /proc/filesystems is not available, the behavior of mount is the same as
 * it was previously.
 *
 * Wed Sep 14 22:43:00 1994: Mitchum DSouza
 * (mitch@mrc-applied-psychology.cambridge.ac.uk) added support for mounting
 * the "loop" device.
 *
 * Wed Sep 14 22:55:10 1994: Sander van Malssen (svm@kozmix.hacktic.nl)
 * added support for remounting readonly file systems readonly.
 *
 * Wed Feb 8 09:23:18 1995: Mike Grupenhoff <kashmir@umiacs.UMD.EDU> added
 * a probe of the superblock for the type before /proc/filesystems is
 * checked.
 *
 * Wed Feb  8 12:27:00 1995: Andries.Brouwer@cwi.nl fixed up error messages.
 *
 */

#include "sundries.h"

#include <linux/fs.h>
#include <linux/minix_fs.h>
#include <linux/ext_fs.h>
#include <linux/ext2_fs.h>
#include <linux/xia_fs.h>
#include <sys/stat.h>
#include <unistd.h>

int del_loop (const char *);

/* True for fake mount (-f).  */
int fake = 0;

/* Don't write a entry in /etc/mtab (-n).  */
int nomtab = 0;

/* True for readonly (-r).  */
int readonly = 0;

/* Nonzero for chatty (-v).  */
int verbose = 0;

/* True for read/write (-w).  */
int readwrite = 0;

/* True for all mount (-a).  */
int all = 0;

/* True if ruid != euid.  */
int suid = 0;

/* Map from -o and fstab option strings to the flag argument to mount(2).  */
struct opt_map
{
  const char *opt;		/* option name */
  int  inv;			/* true if flag value should be inverted */
  int  mask;			/* flag mask value */
};

/* Custom mount options for our own purposes.  */
#define MS_NOAUTO	0x80000000
#define MS_USER		0x40000000

/* Options that we keep the mount system call from seeing.  */
#define MS_NOSYS	(MS_NOAUTO|MS_USER)

/* Options that we keep from appearing in the options field in the mtab.  */
#define MS_NOMTAB	(MS_REMOUNT|MS_NOAUTO|MS_USER)

/* OPTIONS that we make ordinary users have by default.  */
#define MS_SECURE	(MS_NOEXEC|MS_NOSUID|MS_NODEV)

const struct opt_map opt_map[] =
{
  { "defaults",	0, 0		},	/* default options */
  { "ro",	0, MS_RDONLY	},	/* read-only */
  { "rw",	1, MS_RDONLY	},	/* read-write */
  { "exec",	1, MS_NOEXEC	},	/* permit execution of binaries */
  { "noexec",	0, MS_NOEXEC	},	/* don't execute binaries */
  { "suid",	1, MS_NOSUID	},	/* honor suid executables */
  { "nosuid",	0, MS_NOSUID	},	/* don't honor suid executables */
  { "dev",	1, MS_NODEV	},	/* interpret device files  */
  { "nodev",	0, MS_NODEV	},	/* don't interpret devices */
  { "sync",	0, MS_SYNCHRONOUS},	/* synchronous I/O */
  { "async",	1, MS_SYNCHRONOUS},	/* asynchronous I/O */
  { "remount",  0, MS_REMOUNT   },      /* Alter flags of mounted FS */
  { "auto",	1, MS_NOAUTO	},	/* Can be mounted using -a */
  { "noauto",	0, MS_NOAUTO	},	/* Can  only be mounted explicitly */
  { "user",	0, MS_USER	},	/* Allow ordinary user to mount */
  { "nouser",	1, MS_USER	},	/* Forbid ordinary user to mount */
  /* add new options here */
#ifdef MS_NOSUB
  { "sub",	1, MS_NOSUB	},	/* allow submounts */
  { "nosub",	0, MS_NOSUB	},	/* don't allow submounts */
#endif
  { NULL,	0, 0		}
};


/* Report on a single mount.  */
static void
print_one (const struct mntent *mnt)
{
  printf ("%s on %s", mnt->mnt_fsname, mnt->mnt_dir);
  if ((mnt->mnt_type != NULL) && *mnt->mnt_type != '\0')
    printf (" type %s", mnt->mnt_type);
  if (mnt->mnt_opts != NULL)
    printf (" (%s)", mnt->mnt_opts);
  printf ("\n");
}

/* Report on everything in mtab (of the specified types if any).  */
static int
print_all (string_list types)
{
  struct mntent *mnt;
  
  open_mtab ("r");

  while ((mnt = getmntent (F_mtab)) != NULL)
    if (matching_type (mnt->mnt_type, types))
      print_one (mnt);

  if (ferror (F_mtab))
    die (1, "mount: error reading %s: %s", MOUNTED, strerror (errno));

  exit (0);
}


/* Look for OPT in opt_map table and return mask value.  If OPT isn't found,
   tack it onto extra_opts.  */
static inline void
parse_opt (const char *opt, int *mask, char *extra_opts)
{
  const struct opt_map *om;

  for (om = opt_map; om->opt != NULL; om++)
    if (streq (opt, om->opt))
      {
	if (om->inv)
	  *mask &= ~om->mask;
	else
	  *mask |= om->mask;
	if (om->mask == MS_USER)
	  *mask |= MS_SECURE;
	return;
      }
  if (*extra_opts)
    strcat(extra_opts, ",");
  strcat(extra_opts, opt);
}
  
/* Take -o options list and compute 4th and 5th args to mount(2).  flags
   gets the standard options and extra_opts anything we don't recognize.  */
static void
parse_opts (char *opts, int *flags, char **extra_opts)
{
  char *opt;

  *flags = 0;
  *extra_opts = NULL;

  if (opts != NULL)
    {
      *extra_opts = xmalloc (strlen (opts) + 1); 
      **extra_opts = '\0';

      for (opt = strtok (opts, ",");
	   opt != NULL;
	   opt = strtok (NULL, ","))
	parse_opt (opt, flags, *extra_opts);
    }

  if (readonly)
    *flags |= MS_RDONLY;
  if (readwrite)
    *flags &= ~MS_RDONLY;
}

/* Try to build a canonical options string.  */
static char *
fix_opts_string (int flags, char *extra_opts)
{
  const struct opt_map *om;
  char *new_opts;
  char *tmp;

  new_opts = (flags & MS_RDONLY) ? "ro" : "rw";
  for (om = opt_map; om->opt != NULL; om++)
    {
      if (om->mask & MS_RDONLY)
	continue;
      if (om->inv || !om->mask || (flags & om->mask) != om->mask)
	continue;
      tmp = xmalloc(strlen(new_opts) + strlen(om->opt) + 2);
      sprintf(tmp, "%s,%s", new_opts, om->opt);
      new_opts = tmp;
      flags &= ~om->mask;
    }
  if (extra_opts && *extra_opts)
    {
      tmp = xmalloc(strlen(new_opts) + strlen(extra_opts) + 2);
      sprintf(tmp, "%s,%s", new_opts, extra_opts);
      new_opts = tmp;
    }
  return new_opts;
}


/*
    char *fstype(const char *device);

    probes the device and attempts to determine the type of filesystem
    contained within.

    Original routine by <jmorriso@bogomips.ww.ubc.ca>; made into a function
    for mount(8) by Mike Grupenhoff <kashmir@umiacs.umd.edu>.

    Currently supports: minix, ext, ext2, xia
*/

static char *
fstype(const char *device)
{
    int fd;

    /* MINIX */
    struct minix_super_block ms;
    /* extended fs */
    struct ext_super_block es;
    /* 2nd extended fs */
    struct ext2_super_block e2s;
    /* xia fs */
    struct xiafs_super_block xfs;

    fd = open(device, O_RDONLY);
    if (fd < 0) {
	perror(device);
	return 0;
    }
    lseek(fd, BLOCK_SIZE, SEEK_SET);
    read(fd, (char *) &ms, sizeof(ms));
    if (ms.s_magic == MINIX_SUPER_MAGIC || ms.s_magic == MINIX_SUPER_MAGIC2) {
        close(fd);
	return("minix");
    }

    lseek(fd, BLOCK_SIZE, SEEK_SET);
    read(fd, (char *) &es, sizeof(es));
    if (es.s_magic == EXT_SUPER_MAGIC) {
        close(fd);
	return("ext");
    }

    lseek(fd, BLOCK_SIZE, SEEK_SET);
    read(fd, (char *) &e2s, sizeof(e2s));
    if (e2s.s_magic == EXT2_SUPER_MAGIC || e2s.s_magic == EXT2_PRE_02B_MAGIC) {
        close(fd);
	return("ext2");
    }

    lseek(fd, 0, SEEK_SET);
    read(fd, (char *) &xfs, sizeof(xfs));
    if (xfs.s_magic == _XIAFS_SUPER_MAGIC) {
        close(fd);
	return("xiafs");
    }

    close(fd);

    return(0);

}


/* Mount a single file system.  Return status,
   so don't exit on non-fatal errors.  */
 
static int
try_mount5 (char *spec, char *node, char **type, int flags, char *mount_opts) {
   FILE *procfs_file;
   char line[100];
   char fsname[50];
   
   if (*type) return mount5 (spec, node, *type, flags & ~MS_NOSYS, mount_opts);
   if (( procfs_file = fopen("/proc/filesystems", "r")) == NULL) {
				/* If /proc/filesystems is not available,
				   preserve the old behavior of mount. */
      return mount5 (spec,
		     node,
		     FSTYPE_DEFAULT,
		     flags & ~MS_NOSYS, mount_opts);
   }
   while (fgets(line, sizeof(line), procfs_file)) {
      if (sscanf (line, "nodev %[^\n]\n", fsname) == 1) continue;
      if (sscanf (line, " %[^ \n]\n", fsname) != 1) continue;
      if (mount5 (spec, node, fsname, flags & ~MS_NOSYS, mount_opts) == 0) {
      *type=xstrdup(fsname);
      fclose(procfs_file);
      return 0;
      }
   }
   fclose(procfs_file);
   return -1;
}


static int
mount_one (char *spec, char *node, char *type, char *opts, int freq, int pass)
{
  struct mntent mnt;
  int mnt_err;
  int flags;
  char *extra_opts;
  char *mount_opts;
  int anti_recurse = 0;
  int loop=0;

  if (type == NULL)
    {
      if (strchr (spec, ':') != NULL)
	type = "nfs";
    }

  parse_opts (xstrdup (opts), &flags, &extra_opts);

  /* root may allow certain types of mounts by ordinary users */
  if (suid && !(flags & MS_USER))
    die (3, "mount: only root can mount %s on %s", spec, node);

  /* quietly succeed for fstab entries that don't get mounted automatically */
  if (all && (flags & MS_NOAUTO))
    return 0;

  mount_opts = extra_opts;

  if (!fake && type && strncmp("lo@", type, 3)==0) {
    extern int lomount (char *, char *, char *, char **, 
			int *, char **, char **);
    char *dev=type+3;

    loop=1;
    if (lomount (spec, node, dev, &type,
		 &flags, &opts, &mount_opts) != 0)
      return 1;
    spec=dev;
    mount_opts=NULL;
  }

  if (!fake && type && streq (type, "nfs"))
#ifdef HAVE_NFS
    if (nfsmount (spec, node, &flags, &extra_opts, &mount_opts) != 0)
      return 1;
#else
    die (1, "mount: this version doesn't support the type `nfs'");
#endif

  if (!type && !(type = fstype(spec)))
  	return 1;

  block_signals (SIG_BLOCK);

  if (fake
      || (try_mount5 (spec, node, &type, flags & ~MS_NOSYS, mount_opts)) == 0)
    /* Mount succeeded, write mtab entry.  */
    {
      if (!nomtab)
	{
	  mnt.mnt_fsname = canonicalize (spec);
	  mnt.mnt_dir = canonicalize (node);
	  mnt.mnt_type = loop?"loop":type;
	  mnt.mnt_opts = fix_opts_string (flags & ~MS_NOMTAB, 
					  loop?opts:extra_opts);
	  mnt.mnt_freq = freq;
	  mnt.mnt_passno = pass;
      
	  /* We get chatty now rather than after the update to mtab since the
	     mount succeeded, even if the write to /etc/mtab should fail.  */
	  if (verbose)
	    print_one (&mnt);

	  if (flags & MS_REMOUNT)
	    {
	      close_mtab ();
	      update_mtab (mnt.mnt_dir, &mnt);
	      open_mtab ("a+");
	    }
	  else
	    if ((addmntent (F_mtab, &mnt)) == 1)
	      die (1, "mount: error writing %s: %s",
		   MOUNTED, strerror (errno));
	}

      block_signals (SIG_UNBLOCK);
      return 0;
    }

  if (loop)
	del_loop(spec);

  mnt_err = errno; /* work around for errno bug in sigprocmask */

  block_signals (SIG_UNBLOCK);

  /* Mount failed, complain, but don't die.  */
  switch (mnt_err)
    {
    case EPERM:
      if (geteuid() == 0)
	error ("mount: mount point %s is not a directory", node);
      else
	error ("mount: must be superuser to use mount");
      break;
    case EBUSY:
      error ("mount: %s already mounted or %s busy", spec, node);
      break;
    case ENOENT:
      { struct stat statbuf;
	if (stat (node, &statbuf))
	      error ("mount: mount point %s does not exist", node);
	else if (stat (spec, &statbuf))
	      error ("mount: special device %s does not exist", spec);
	else {
           errno = mnt_err;
           perror("mount");
	}
	break;
     }
    case ENOTDIR:
      error ("mount: mount point %s is not a directory", node); break;
    case EINVAL:
      error ("mount: wrong fs type or bad superblock on %s", spec); break;
    case EMFILE:
      error ("mount table full"); break;
    case EIO:
      error ("mount: %s: can't read superblock", spec); break;
    case ENODEV:
      error ("mount: fs type %s not supported by kernel", type); break;
    case ENOTBLK:
      error ("mount: %s is not a block device", spec); break;
    case ENXIO:
      error ("mount: %s is not a valid block device", spec); break;
    case EACCES:  /* pre-linux 1.1.38 */
    case EROFS:   /* linux 1.1.38 and later */
      if (anti_recurse)
        {
          error ("mount: block device %s is not permitted on its filesystem", spec);
          break;
        }
      else
        {
         anti_recurse++;
         if (opts)
           {
             opts = realloc(xstrdup(opts), strlen(opts)+3);
             strcat(opts, ",ro");
           }
         else
           opts = "ro";
          error ("mount: block device %s is write-protected, mounting read-only", spec);
          return mount_one (spec, node, type, opts, freq, pass);
        }
      break;
    default:
      error ("mount: %s", strerror (mnt_err)); break;
    }
  return 1;
}

/* Check if an fsname/dir pair was already in the old mtab.  */
static int
mounted (char *spec, char *node, string_list spec_list, string_list node_list)
{
  spec = canonicalize (spec);
  node = canonicalize (node);

  while (spec_list != NULL)
    {
      if (streq (spec, car (spec_list)) && streq (node, car (node_list)))
	return 1;
      spec_list = cdr (spec_list);
      node_list = cdr (node_list);
    }
    return 0;
}

/* Mount all filesystems of the specified types except swap and root.  */
static int
mount_all (string_list types)
{
  struct mntent *fstab;
  struct mntent *mnt;
  string_list spec_list = NULL;
  string_list node_list = NULL;
  int status;

  rewind (F_mtab);

  while ((mnt = getmntent (F_mtab)))
    if (matching_type (mnt->mnt_type, types)
	&& !streq (mnt->mnt_dir, "/")
	&& !streq (mnt->mnt_dir, "root"))
      {
	spec_list = cons (xstrdup (mnt->mnt_fsname), spec_list);
	node_list = cons (xstrdup (mnt->mnt_dir), node_list);
      }

  status = 0;
  while ((fstab = getfsent ()) != NULL)
    if (matching_type (fstab->mnt_type, types)
	 && !streq (fstab->mnt_dir, "/")
	 && !streq (fstab->mnt_dir, "root"))
      if (mounted (fstab->mnt_fsname, fstab->mnt_dir, spec_list, node_list))
	{
	  if (verbose)
	    printf("mount: %s already mounted on %s\n",
		   fstab->mnt_fsname, fstab->mnt_dir);
	}
      else
        status |= mount_one (fstab->mnt_fsname, fstab->mnt_dir,
			     fstab->mnt_type, fstab->mnt_opts,
			     fstab->mnt_freq, fstab->mnt_passno);

  return status;
}

/* Create mtab with a root entry.  */
static void
create_mtab (void)
{
  struct mntent *fstab;
  struct mntent mnt;
  int flags;
  char *extra_opts;

  if ((F_mtab = setmntent (MOUNTED, "a+")) == NULL)
    die (1, "mount: can't open %s for writing: %s", MOUNTED, strerror (errno));

  /* Find the root entry by looking it up in fstab, which might be wrong.
     We could statfs "/" followed by a slew of stats on /dev/ but then
     we'd have to unparse the mount options as well....  */
  if ((fstab = getfsfile ("/")) || (fstab = getfsfile ("root")))
    {
      parse_opts (xstrdup (fstab->mnt_opts), &flags, &extra_opts);
      mnt = *fstab;
      mnt.mnt_fsname = canonicalize (fstab->mnt_fsname);
      mnt.mnt_dir = "/";
      mnt.mnt_opts = fix_opts_string (flags, extra_opts);

      if (addmntent (F_mtab, &mnt) == 1)
	die (1, "mount: error writing %s: %s", MOUNTED, strerror (errno));
    }
  if (fchmod (fileno (F_mtab), S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) < 0)
    die (1, "mount: error changing mode of %s: %s", MOUNTED, strerror (errno));
  endmntent (F_mtab);
}

extern char version[];
static struct option longopts[] =
{
  { "all", 0, 0, 'a' },
  { "fake", 0, 0, 'f' },
  { "help", 0, 0, 'h' },
  { "no-mtab", 0, 0, 'n' },
  { "read-only", 0, 0, 'r' },
  { "ro", 0, 0, 'r' },
  { "verbose", 0, 0, 'v' },
  { "version", 0, 0, 'V' },
  { "read-write", 0, 0, 'w' },
  { "rw", 0, 0, 'w' },
  { "options", 1, 0, 'o' },
  { "types", 1, 0, 't' },
  { NULL, 0, 0, 0 }
};

const char *usage_string = "\
usage: mount [-hV]\n\
       mount -a [-nfrvw] [-t vfstypes]\n\
       mount [-nfrvw] [-o options] special | node\n\
       mount [-nfrvw] [-t vfstype] [-o options] special node\n\
";

static void
usage (FILE *fp, int n)
{
  fprintf (fp, "%s", usage_string);
  exit (n);
}

int
main (int argc, char *argv[])
{
  int c;
  char *options = NULL;
  string_list types = NULL;
  struct mntent *fs;
  char *spec;
  int result = 0;
  struct stat statbuf;

  while ((c = getopt_long (argc, argv, "afhnrvVwt:o:", longopts, NULL)) != EOF)
    switch (c)
      {
      case 'a':			/* mount everything in fstab */
	++all;
	break;
      case 'f':			/* fake (don't actually do mount(2) call) */
	++fake;
	break;
      case 'h':			/* help */
	usage (stdout, 0);
	break;
      case 'n':			/* mount without writing in /etc/mtab */
	++nomtab;
	break;
      case 'r':			/* mount readonly */
	++readonly;
	readwrite = 0;
	break;
      case 'v':			/* be chatty */
	++verbose;
	break;
      case 'V':			/* version */
	printf ("%s\n", version);
	exit (0);
      case 'w':			/* mount read/write */
	++readwrite;
	readonly = 0;
	break;
      case 't':			/* specify file system types */
	types = parse_list (optarg);
	break;
      case 'o':			/* specify mount options */
	options = optarg;
	break;
      case 0:
	break;
      case '?':
      default:
	usage (stderr, 1);
	break;
      }

  argc -= optind;
  argv += optind;

  if (argc == 0)
    {
      if (options)
	usage (stderr, 1);
      if (!all)
	return print_all (types);
    }

  if (getuid () != geteuid ())
    {
      suid = 1;
      if (types || options || readwrite || nomtab || all || fake || argc != 1)
	die (2, "mount: only root can do that");
    }

  if (!nomtab)
    {
      lock_mtab ();
      if (stat(MOUNTED, &statbuf) < 0)
	create_mtab ();
      open_mtab ("a+");
    }
  else if (stat(MOUNTED, &statbuf) >= 0)
    open_mtab ("r");


  switch (argc)
    {
    case 0:
      /* mount -a */
      result = mount_all (types);
      break;

    case 1:
      /* mount [-nfrvw] [-o options] special | node */
      if (types != NULL)
	usage (stderr, 1);
      /* Try to find the other pathname in fstab.  */ 
      spec = canonicalize (*argv);
      if (!(fs = getmntfile (spec))
	  && !(fs = getfsspec (spec)) && !(fs = getfsfile (spec)))
	die (2, "mount: can't find %s in %s or %s",
	     spec, MOUNTED, _PATH_FSTAB);
      /* Merge the fstab and command line options.  */
      if (options == NULL)
	options = fs->mnt_opts;
      else
	{
	  char *tmp = xmalloc(strlen(fs->mnt_opts) + strlen(options) + 2);

	  sprintf (tmp, "%s,%s", fs->mnt_opts, options);
	  options = tmp;
	}
      result = mount_one (xstrdup (fs->mnt_fsname), xstrdup (fs->mnt_dir),
			  xstrdup (fs->mnt_type), options,
			  fs->mnt_freq, fs->mnt_passno);
      break;

    case 2:
      /* mount [-nfrvw] [-t vfstype] [-o options] special node */
      if (types == NULL)
	result = mount_one (argv[0], argv[1], NULL, options, 0, 0);
      else if (cdr (types) == NULL)
	result = mount_one (argv[0], argv[1], car (types), options, 0, 0);
      else
	usage (stderr, 2);
      break;
      
    default:
      usage (stderr, 2);
    }

  if (!nomtab)
    {
      endmntent (F_mtab);
      unlock_mtab ();
    }

  exit (result);
}
