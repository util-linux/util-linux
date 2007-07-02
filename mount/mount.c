/*
 * A mount(8) for Linux 0.99.
 * mount.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 *
 * Modifications by many people. Distributed under GPL.
 */

#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <stdio.h>

#include <pwd.h>
#include <grp.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mount.h>

#include <mntent.h>
#include <paths.h>

#ifdef HAVE_LIBSELINUX
#include <selinux/selinux.h>
#include <selinux/context.h>
#endif

#include "fsprobe.h"
#include "mount_constants.h"
#include "sundries.h"
#include "xmalloc.h"
#include "mount_mntent.h"
#include "fstab.h"
#include "lomount.h"
#include "loop.h"
#include "linux_fs.h"		/* for BLKGETSIZE */
#include "getusername.h"
#include "mount_paths.h"
#include "env.h"
#include "nls.h"

#define DO_PS_FIDDLING

#ifdef DO_PS_FIDDLING
#include "setproctitle.h"
#endif

/* Quiet mode */
int mount_quiet = 0;

/* True for fake mount (-f).  */
static int fake = 0;

/* True if we are allowed to call /sbin/mount.${FSTYPE} */
static int external_allowed = 1;

/* Don't write a entry in /etc/mtab (-n).  */
static int nomtab = 0;

/* True for explicit readonly (-r).  */
static int readonly = 0;

/* Nonzero for chatty (-v).  */
int verbose = 0;

/* Nonzero for sloppy (-s).  */
int sloppy = 0;

/* True for explicit read/write (-w).  */
static int readwrite = 0;

/* True for all mount (-a).  */
int mount_all = 0;

/* True for fork() during all mount (-F).  */
static int optfork = 0;

/* Add volumelabel in a listing of mounted devices (-l). */
static int list_with_volumelabel = 0;

/* Nonzero for mount {--bind|--replace|--before|--after|--over|--move|
 * 		       make-shared|make-private|make-unbindable|make-slave}
 */
static int mounttype = 0;

/* True if ruid != euid.  */
static int suid = 0;

/* Contains the fd to read the passphrase from, if any. */
static int pfd = -1;

/* Map from -o and fstab option strings to the flag argument to mount(2).  */
struct opt_map {
  const char *opt;		/* option name */
  int  skip;			/* skip in mtab option string */
  int  inv;			/* true if flag value should be inverted */
  int  mask;			/* flag mask value */
};

/* Custom mount options for our own purposes.  */
/* Maybe these should now be freed for kernel use again */
#define MS_NOAUTO	0x80000000
#define MS_USERS	0x40000000
#define MS_USER		0x20000000
#define MS_OWNER	0x10000000
#define MS_GROUP	0x08000000
#define MS_COMMENT	0x02000000
#define MS_LOOP		0x00010000

/* Options that we keep the mount system call from seeing.  */
#define MS_NOSYS	(MS_NOAUTO|MS_USERS|MS_USER|MS_COMMENT|MS_LOOP)

/* Options that we keep from appearing in the options field in the mtab.  */
#define MS_NOMTAB	(MS_REMOUNT|MS_NOAUTO|MS_USERS|MS_USER)

#define MS_PROPAGATION  (MS_SHARED|MS_SLAVE|MS_UNBINDABLE|MS_PRIVATE)

/* Options that we make ordinary users have by default.  */
#define MS_SECURE	(MS_NOEXEC|MS_NOSUID|MS_NODEV)

/* Options that we make owner-mounted devices have by default */
#define MS_OWNERSECURE	(MS_NOSUID|MS_NODEV)

static const struct opt_map opt_map[] = {
  { "defaults",	0, 0, 0		},	/* default options */
  { "ro",	1, 0, MS_RDONLY	},	/* read-only */
  { "rw",	1, 1, MS_RDONLY	},	/* read-write */
  { "exec",	0, 1, MS_NOEXEC	},	/* permit execution of binaries */
  { "noexec",	0, 0, MS_NOEXEC	},	/* don't execute binaries */
  { "suid",	0, 1, MS_NOSUID	},	/* honor suid executables */
  { "nosuid",	0, 0, MS_NOSUID	},	/* don't honor suid executables */
  { "dev",	0, 1, MS_NODEV	},	/* interpret device files  */
  { "nodev",	0, 0, MS_NODEV	},	/* don't interpret devices */
  { "sync",	0, 0, MS_SYNCHRONOUS},	/* synchronous I/O */
  { "async",	0, 1, MS_SYNCHRONOUS},	/* asynchronous I/O */
  { "dirsync",	0, 0, MS_DIRSYNC},	/* synchronous directory modifications */
  { "remount",  0, 0, MS_REMOUNT},      /* Alter flags of mounted FS */
  { "bind",	0, 0, MS_BIND   },	/* Remount part of tree elsewhere */
  { "rbind",	0, 0, MS_BIND|MS_REC }, /* Idem, plus mounted subtrees */
  { "auto",	0, 1, MS_NOAUTO	},	/* Can be mounted using -a */
  { "noauto",	0, 0, MS_NOAUTO	},	/* Can  only be mounted explicitly */
  { "users",	0, 0, MS_USERS	},	/* Allow ordinary user to mount */
  { "nousers",	0, 1, MS_USERS	},	/* Forbid ordinary user to mount */
  { "user",	0, 0, MS_USER	},	/* Allow ordinary user to mount */
  { "nouser",	0, 1, MS_USER	},	/* Forbid ordinary user to mount */
  { "owner",	0, 0, MS_OWNER  },	/* Let the owner of the device mount */
  { "noowner",	0, 1, MS_OWNER  },	/* Device owner has no special privs */
  { "group",	0, 0, MS_GROUP  },	/* Let the group of the device mount */
  { "nogroup",	0, 1, MS_GROUP  },	/* Device group has no special privs */
  { "_netdev",	0, 0, MS_COMMENT},	/* Device requires network */
  { "comment",	0, 0, MS_COMMENT},	/* fstab comment only (kudzu,_netdev)*/

  /* add new options here */
#ifdef MS_NOSUB
  { "sub",	0, 1, MS_NOSUB	},	/* allow submounts */
  { "nosub",	0, 0, MS_NOSUB	},	/* don't allow submounts */
#endif
#ifdef MS_SILENT
  { "quiet",	0, 0, MS_SILENT    },	/* be quiet  */
  { "loud",	0, 1, MS_SILENT    },	/* print out messages. */
#endif
#ifdef MS_MANDLOCK
  { "mand",	0, 0, MS_MANDLOCK },	/* Allow mandatory locks on this FS */
  { "nomand",	0, 1, MS_MANDLOCK },	/* Forbid mandatory locks on this FS */
#endif
  { "loop",	1, 0, MS_LOOP	},	/* use a loop device */
#ifdef MS_NOATIME
  { "atime",	0, 1, MS_NOATIME },	/* Update access time */
  { "noatime",	0, 0, MS_NOATIME },	/* Do not update access time */
#endif
#ifdef MS_NODIRATIME
  { "diratime",	0, 1, MS_NODIRATIME },	/* Update dir access times */
  { "nodiratime", 0, 0, MS_NODIRATIME },/* Do not update dir access times */
#endif
#ifdef MS_RELATIME
  { "relatime",	0, 0, MS_RELATIME },   /* Update access times relative to
					  mtime/ctime */
  { "norelatime", 0, 1, MS_RELATIME }, /* Update access time without regard
					  to mtime/ctime */
#endif
  { NULL,	0, 0, 0		}
};

static const char *opt_loopdev, *opt_vfstype, *opt_offset, *opt_encryption,
	*opt_speed, *opt_comment, *opt_uhelper;

static int mounted (const char *spec0, const char *node0);
static int check_special_mountprog(const char *spec, const char *node,
		const char *type, int flags, char *extra_opts, int *status);

static struct string_opt_map {
  char *tag;
  int skip;
  const char **valptr;
} string_opt_map[] = {
  { "loop=",	0, &opt_loopdev },
  { "vfs=",	1, &opt_vfstype },
  { "offset=",	0, &opt_offset },
  { "encryption=", 0, &opt_encryption },
  { "speed=", 0, &opt_speed },
  { "comment=", 1, &opt_comment },
  { "uhelper=", 0, &opt_uhelper },
  { NULL, 0, NULL }
};

static void
clear_string_opts(void) {
	struct string_opt_map *m;

	for (m = &string_opt_map[0]; m->tag; m++)
		*(m->valptr) = NULL;
}

static int
parse_string_opt(char *s) {
	struct string_opt_map *m;
	int lth;

	for (m = &string_opt_map[0]; m->tag; m++) {
		lth = strlen(m->tag);
		if (!strncmp(s, m->tag, lth)) {
			*(m->valptr) = xstrdup(s + lth);
			return 1;
		}
	}
	return 0;
}

static void
my_free(const void *s) {
	if (s)
		free((void *) s);
}

/* Report on a single mount.  */
static void
print_one (const struct my_mntent *me) {
	if (mount_quiet)
		return;
	printf ("%s on %s", me->mnt_fsname, me->mnt_dir);
	if (me->mnt_type != NULL && *(me->mnt_type) != '\0')
		printf (" type %s", me->mnt_type);
	if (me->mnt_opts != NULL)
		printf (" (%s)", me->mnt_opts);
	if (list_with_volumelabel) {
		const char *devname = fsprobe_get_devname(me->mnt_fsname);

		if (devname) {
			const char *label;

			label = fsprobe_get_label_by_devname(devname);
			my_free(devname);

			if (label) {
				printf (" [%s]", label);
				my_free(label);
			}
		}
	}
	printf ("\n");
}

/* Report on everything in mtab (of the specified types if any).  */
static int
print_all (char *types) {
     struct mntentchn *mc, *mc0;

     mc0 = mtab_head();
     for (mc = mc0->nxt; mc && mc != mc0; mc = mc->nxt) {
	  if (matching_type (mc->m.mnt_type, types))
	       print_one (&(mc->m));
     }
     exit (0);
}

/* reallocates its first arg */
static char *
append_opt(char *s, const char *opt, const char *val)
{
	if (!opt)
		return s;
	if (!s) {
		if (!val)
		       return xstrdup(opt);		/* opt */

		return xstrconcat3(NULL, opt, val);	/* opt=val */
	}
	if (!val)
		return xstrconcat3(s, ",", opt);	/* s,opt */

	return xstrconcat4(s, ",", opt, val);		/* s,opt=val */
}

static char *
append_numopt(char *s, const char *opt, long num)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%ld", num);
	return append_opt(s, opt, buf);
}

#ifdef HAVE_LIBSELINUX
/* strip quotes from a "string"
 * Warning: This function modify the "str" argument.
 */
static char *
strip_quotes(char *str)
{
	char *end = NULL;

	if (*str != '"')
		return str;

	end = strrchr(str, '"');
	if (end == NULL || end == str)
		die (EX_USAGE, _("mount: improperly quoted option string '%s'"), str);

	*end = '\0';
	return str+1;
}

/* translates SELinux context from human to raw format and
 * appends it to the mount extra options.
 *
 * returns -1 on error and 0 on success
 */
static int
append_context(const char *optname, char *optdata, char **extra_opts)
{
	security_context_t raw = NULL;
	char *data = NULL;

	if (!is_selinux_enabled())
		/* ignore the option if we running without selinux */
		return 0;

	if (optdata==NULL || *optdata=='\0' || optname==NULL)
		return -1;

	/* TODO: use strip_quotes() for all mount options? */
	data = *optdata =='"' ? strip_quotes(optdata) : optdata;

	if (selinux_trans_to_raw_context(
			(security_context_t) data, &raw)==-1 ||
			raw==NULL)
		return -1;

	if (verbose)
		printf(_("mount: translated %s '%s' to '%s'\n"),
				optname, data, (char *) raw);

	*extra_opts = append_opt(*extra_opts, optname, NULL);
	*extra_opts = xstrconcat4(*extra_opts, "\"", (char *) raw, "\"");

	freecon(raw);
	return 0;
}
#endif

/*
 * Look for OPT in opt_map table and return mask value.
 * If OPT isn't found, tack it onto extra_opts (which is non-NULL).
 * For the options uid= and gid= replace user or group name by its value.
 */
static inline void
parse_opt(char *opt, int *mask, char **extra_opts) {
	const struct opt_map *om;

	for (om = opt_map; om->opt != NULL; om++)
		if (streq (opt, om->opt)) {
			if (om->inv)
				*mask &= ~om->mask;
			else
				*mask |= om->mask;
			if ((om->mask == MS_USER || om->mask == MS_USERS)
			    && !om->inv)
				*mask |= MS_SECURE;
			if ((om->mask == MS_OWNER || om->mask == MS_GROUP)
			    && !om->inv)
				*mask |= MS_OWNERSECURE;
#ifdef MS_SILENT
			if (om->mask == MS_SILENT && om->inv)  {
				mount_quiet = 1;
				verbose = 0;
			}
#endif
			return;
		}

	/* convert nonnumeric ids to numeric */
	if (!strncmp(opt, "uid=", 4) && !isdigit(opt[4])) {
		struct passwd *pw = getpwnam(opt+4);

		if (pw) {
			*extra_opts = append_numopt(*extra_opts,
						"uid=", pw->pw_uid);
			return;
		}
	}
	if (!strncmp(opt, "gid=", 4) && !isdigit(opt[4])) {
		struct group *gr = getgrnam(opt+4);

		if (gr) {
			*extra_opts = append_numopt(*extra_opts,
						"gid=", gr->gr_gid);
			return;
		}
	}

#ifdef HAVE_LIBSELINUX
	if (strncmp(opt, "context=", 8) == 0 && *(opt+8)) {
		if (append_context("context=", opt+8, extra_opts) == 0)
			return;
	}
	if (strncmp(opt, "fscontext=", 10) == 0 && *(opt+10)) {
		if (append_context("fscontext=", opt+10, extra_opts) == 0)
			return;
	}
	if (strncmp(opt, "defcontext=", 11) == 0 && *(opt+11)) {
		if (append_context("defcontext=", opt+11, extra_opts) == 0)
			return;
	}
#endif
	*extra_opts = append_opt(*extra_opts, opt, NULL);
}

/* Take -o options list and compute 4th and 5th args to mount(2).  flags
   gets the standard options (indicated by bits) and extra_opts all the rest */
static void
parse_opts (const char *options, int *flags, char **extra_opts) {
	*flags = 0;
	*extra_opts = NULL;

	clear_string_opts();

	if (options != NULL) {
		char *opts = xstrdup(options);
		int open_quote = 0;
		char *opt, *p;

		for (p=opts, opt=NULL; p && *p; p++) {
			if (!opt)
				opt = p;		/* begin of the option item */
			if (*p == '"')
				open_quote ^= 1;	/* reverse the status */
			if (open_quote)
				continue;		/* still in quoted block */
			if (*p == ',')
				*p = '\0';		/* terminate the option item */
			/* end of option item or last item */
			if (*p == '\0' || *(p+1) == '\0') {
				if (!parse_string_opt(opt))
					parse_opt(opt, flags, extra_opts);
				opt = NULL;
			}
		}
		free(opts);
	}

	if (readonly)
		*flags |= MS_RDONLY;
	if (readwrite)
		*flags &= ~MS_RDONLY;

	if (mounttype & MS_PROPAGATION)
		*flags &= ~MS_BIND;
	*flags |= mounttype;
}

/* Try to build a canonical options string.  */
static char *
fix_opts_string (int flags, const char *extra_opts, const char *user) {
	const struct opt_map *om;
	const struct string_opt_map *m;
	char *new_opts;

	new_opts = append_opt(NULL, (flags & MS_RDONLY) ? "ro" : "rw", NULL);
	for (om = opt_map; om->opt != NULL; om++) {
		if (om->skip)
			continue;
		if (om->inv || !om->mask || (flags & om->mask) != om->mask)
			continue;
		new_opts = append_opt(new_opts, om->opt, NULL);
		flags &= ~om->mask;
	}
	for (m = &string_opt_map[0]; m->tag; m++) {
		if (!m->skip && *(m->valptr))
			new_opts = append_opt(new_opts, m->tag, *(m->valptr));
	}
	if (extra_opts && *extra_opts)
		new_opts = append_opt(new_opts, extra_opts, NULL);

	if (user)
		new_opts = append_opt(new_opts, "user=", user);

	return new_opts;
}

static int
already (const char *spec, const char *node) {
	struct mntentchn *mc;
	int ret = 1;

	if ((mc = getmntfile(node)) != NULL)
		error (_("mount: according to mtab, "
			 "%s is already mounted on %s"),
		       mc->m.mnt_fsname, node);
	else if (spec && strcmp (spec, "none") &&
		 (mc = getmntfile(spec)) != NULL)
		error (_("mount: according to mtab, %s is mounted on %s"),
		       spec, mc->m.mnt_dir);
	else
		ret = 0;
	return ret;
}

/* Create mtab with a root entry.  */
static void
create_mtab (void) {
	struct mntentchn *fstab;
	struct my_mntent mnt;
	int flags;
	mntFILE *mfp;

	lock_mtab();

	mfp = my_setmntent (MOUNTED, "a+");
	if (mfp == NULL || mfp->mntent_fp == NULL) {
		int errsv = errno;
		die (EX_FILEIO, _("mount: can't open %s for writing: %s"),
		     MOUNTED, strerror (errsv));
	}

	/* Find the root entry by looking it up in fstab */
	if ((fstab = getfs_by_dir ("/")) || (fstab = getfs_by_dir ("root"))) {
		char *extra_opts;
		parse_opts (fstab->m.mnt_opts, &flags, &extra_opts);
		mnt.mnt_dir = "/";
		mnt.mnt_fsname = canonicalize (fstab->m.mnt_fsname);
		mnt.mnt_type = fstab->m.mnt_type;
		mnt.mnt_opts = fix_opts_string (flags, extra_opts, NULL);
		mnt.mnt_freq = mnt.mnt_passno = 0;
		my_free(extra_opts);

		if (my_addmntent (mfp, &mnt) == 1) {
			int errsv = errno;
			die (EX_FILEIO, _("mount: error writing %s: %s"),
			     MOUNTED, strerror (errsv));
		}
	}
	if (fchmod (fileno (mfp->mntent_fp), 0644) < 0)
		if (errno != EROFS) {
			int errsv = errno;
			die (EX_FILEIO,
			     _("mount: error changing mode of %s: %s"),
			     MOUNTED, strerror (errsv));
		}
	my_endmntent (mfp);

	unlock_mtab();
}

/* count successful mount system calls */
static int mountcount = 0;

/*
 * do_mount_syscall()
 *	Mount a single file system. Keep track of successes.
 * returns: 0: OK, -1: error in errno
 */
static int
do_mount_syscall (struct mountargs *args) {
	int flags = args->flags;

	if ((flags & MS_MGC_MSK) == 0)
		flags |= MS_MGC_VAL;

	if (verbose > 2)
		printf("mount: mount(2) syscall: source: \"%s\", target: \"%s\", "
			"filesystemtype: \"%s\", mountflags: %d, data: %s\n",
			args->spec, args->node, args->type, flags, (char *) args->data);

	return mount (args->spec, args->node, args->type, flags, args->data);
}

/*
 * do_mount()
 *	Mount a single file system, possibly invoking an external handler to
 *      do so. Keep track of successes.
 * returns: 0: OK, -1: error in errno
 */
static int
do_mount (struct mountargs *args, int *special, int *status) {
	int ret;
	if (check_special_mountprog(args->spec, args->node, args->type,
	                            args->flags, args->data, status)) {
		*special = 1;
		ret = 0;
	} else
		ret = do_mount_syscall(args);

	if (ret == 0)
		mountcount++;
	return ret;
}

/*
 * check_special_mountprog()
 *	If there is a special mount program for this type, exec it.
 * returns: 0: no exec was done, 1: exec was done, status has result
 */
static int
check_special_mountprog(const char *spec, const char *node, const char *type, int flags,
			char *extra_opts, int *status) {
  char mountprog[120];
  struct stat statbuf;
  int res;

  if (!external_allowed)
      return 0;

  if (type && strlen(type) < 100) {
       sprintf(mountprog, "/sbin/mount.%s", type);
       if (stat(mountprog, &statbuf) == 0) {
	    if (verbose)
		 fflush(stdout);
	    res = fork();
	    if (res == 0) {
		 char *oo, *mountargs[10];
		 int i = 0;

		 setuid(getuid());
		 setgid(getgid());
		 oo = fix_opts_string (flags, extra_opts, NULL);
		 mountargs[i++] = mountprog;				/* 1 */
		 mountargs[i++] = (char *) spec;			/* 2 */
		 mountargs[i++] = (char *) node;			/* 3 */
		 if (sloppy && strncmp(type, "nfs", 3) == 0)
		      mountargs[i++] = "-s";				/* 4 */
		 if (fake)
		      mountargs[i++] = "-f";				/* 5 */
		 if (nomtab)
		      mountargs[i++] = "-n";				/* 6 */
		 if (verbose)
		      mountargs[i++] = "-v";				/* 7 */
		 if (oo && *oo) {
		      mountargs[i++] = "-o";				/* 8 */
		      mountargs[i++] = oo;				/* 9 */
		 }
		 mountargs[i] = NULL;					/* 10 */

		 if (verbose > 2) {
			i = 0;
			while(verbose > 2 && mountargs[i]) {
				printf("mount: external mount: argv[%d] = \"%s\"\n",
					i, mountargs[i]);
				i++;
			}
			fflush(stdout);
		 }

		 execv(mountprog, mountargs);
		 exit(1);	/* exec failed */
	    } else if (res != -1) {
		 int st;
		 wait(&st);
		 *status = (WIFEXITED(st) ? WEXITSTATUS(st) : EX_SYSERR);
		 return 1;
	    } else {
		 int errsv = errno;
		 error(_("mount: cannot fork: %s"), strerror(errsv));
	    }
       }
  }
  return 0;
}


static const char *
guess_fstype_by_devname(const char *devname)
{
   const char *type = fsprobe_get_fstype_by_devname(devname);

   if (verbose) {
      printf (_("mount: you didn't specify a filesystem type for %s\n"), devname);

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

/*
 * guess_fstype_and_mount()
 *	Mount a single file system. Guess the type when unknown.
 * returns: 0: OK, -1: error in errno, 1: other error
 *	don't exit on non-fatal errors.
 *	on return types is filled with the type used.
 */
static int
guess_fstype_and_mount(const char *spec, const char *node, const char **types,
		       int flags, char *mount_opts, int *special, int *status) {
   struct mountargs args = { spec, node, NULL, flags & ~MS_NOSYS, mount_opts };

   if (*types && strcasecmp (*types, "auto") == 0)
      *types = NULL;

   if (!*types && (flags & (MS_BIND | MS_MOVE)))
      *types = "none";		/* random, but not "bind" */

   if (!*types && !(flags & MS_REMOUNT)) {
      *types = guess_fstype_by_devname(spec);
      if (*types) {
	  if (!strcmp(*types, "swap")) {
	      error(_("%s looks like swapspace - not mounted"), spec);
	      *types = NULL;
	      return 1;
	  } else {
	      args.type = *types;
	      return do_mount (&args, special, status);
          }
      }
   }

   /* Accept a comma-separated list of types, and try them one by one */
   /* A list like "nonfs,.." indicates types not to use */
   if (*types && strncmp(*types, "no", 2) && index(*types,',')) {
      char *t = strdup(*types);
      char *p;

      while((p = index(t,',')) != NULL) {
	 *p = 0;
	 args.type = *types = t;
	 if (do_mount (&args, special, status) == 0)
	    return 0;
	 t = p+1;
      }
      /* do last type below */
      *types = t;
   }

   if (*types || (flags & MS_REMOUNT)) {
      args.type = *types;
      return do_mount (&args, special, status);
   }

   return fsprobe_procfsloop_mount(do_mount, &args, types, special, status);
}

/*
 * suid_check()
 *	Die if the user is not allowed to do this.
 */
static void
suid_check(const char *spec, const char *node, int *flags, char **user) {
  if (suid) {
      /*
       * MS_OWNER: Allow owners to mount when fstab contains
       * the owner option.  Note that this should never be used
       * in a high security environment, but may be useful to give
       * people at the console the possibility of mounting a floppy.
       * MS_GROUP: Allow members of device group to mount. (Martin Dickopp)
       */
      if (*flags & (MS_OWNER | MS_GROUP)) {
	  struct stat sb;

	  if (!strncmp(spec, "/dev/", 5) && stat(spec, &sb) == 0) {

	      if (*flags & MS_OWNER) {
		  if (getuid() == sb.st_uid)
		      *flags |= MS_USER;
	      }

	      if (*flags & MS_GROUP) {
		  if (getgid() == sb.st_gid)
		      *flags |= MS_USER;
		  else {
		      int n = getgroups(0, NULL);

		      if (n > 0) {
			      gid_t *groups = xmalloc(n * sizeof(*groups));
			      if (getgroups(n, groups) == n) {
				      int i;
				      for (i = 0; i < n; i++) {
					      if (groups[i] == sb.st_gid) {
						      *flags |= MS_USER;
						      break;
					      }
				      }
			      }
			      free(groups);
		      }
		  }
	      }
	  }
      }

      /* James Kehl <mkehl@gil.com.au> came with a similar patch:
	 allow an arbitrary user to mount when he is the owner of
	 the mount-point and has write-access to the device.
	 This is even less secure. Let me skip it for the time being;
	 there should be an explicit fstab line allowing such things. */

      if (!(*flags & (MS_USER | MS_USERS))) {
	  if (already (spec, node))
	    die (EX_USAGE, _("mount failed"));
	  else
	    die (EX_USAGE, _("mount: only root can mount %s on %s"), spec, node);
      }
      if (*flags & MS_USER)
	  *user = getusername();
  }

  *flags &= ~(MS_OWNER | MS_GROUP);
}

static int
loop_check(const char **spec, const char **type, int *flags,
	   int *loop, const char **loopdev, const char **loopfile) {
  int looptype;
  unsigned long long offset;

  /*
   * In the case of a loop mount, either type is of the form lo@/dev/loop5
   * or the option "-o loop=/dev/loop5" or just "-o loop" is given, or
   * mount just has to figure things out for itself from the fact that
   * spec is not a block device. We do not test for a block device
   * immediately: maybe later other types of mountable objects will occur.
   */

  *loopdev = opt_loopdev;

  looptype = (*type && strncmp("lo@", *type, 3) == 0);
  if (looptype) {
    if (*loopdev)
      error(_("mount: loop device specified twice"));
    *loopdev = *type + 3;
    *type = opt_vfstype;
  } else if (opt_vfstype) {
    if (*type)
      error(_("mount: type specified twice"));
    else
      *type = opt_vfstype;
  }

  *loop = ((*flags & MS_LOOP) || *loopdev || opt_offset || opt_encryption);
  *loopfile = *spec;

  if (*loop) {
    *flags |= MS_LOOP;
    if (fake) {
      if (verbose)
	printf(_("mount: skipping the setup of a loop device\n"));
    } else {
      int loopro = (*flags & MS_RDONLY);
      int res;

      offset = opt_offset ? strtoull(opt_offset, NULL, 0) : 0;

      do {
        if (!*loopdev || !**loopdev)
	  *loopdev = find_unused_loop_device();
	if (!*loopdev)
	  return EX_SYSERR;	/* no more loop devices */
	if (verbose)
	  printf(_("mount: going to use the loop device %s\n"), *loopdev);

	if ((res = set_loop(*loopdev, *loopfile, offset,
			    opt_encryption, pfd, &loopro))) {
	  if (res == 2) {
	     /* loop dev has been grabbed by some other process,
	        try again, if not given explicitly */
	     if (!opt_loopdev) {
	       if (verbose)
	         printf(_("mount: stolen loop=%s ...trying again\n"), *loopdev);
	       my_free(*loopdev);
	       *loopdev = NULL;
	       continue;
	     }
	     error(_("mount: stolen loop=%s"), *loopdev);
	     return EX_FAIL;

	  } else {
	     if (verbose)
	       printf(_("mount: failed setting up loop device\n"));
	     if (!opt_loopdev) {
	       my_free(*loopdev);
	       *loopdev = NULL;
	     }
	     return EX_FAIL;
	  }
	}
      } while (!*loopdev);

      if (verbose > 1)
	printf(_("mount: setup loop device successfully\n"));
      *spec = *loopdev;
      if (loopro)
	*flags |= MS_RDONLY;
    }
  }

  return 0;
}

static void
update_mtab_entry(const char *spec, const char *node, const char *type,
		  const char *opts, int flags, int freq, int pass) {
	struct my_mntent mnt;

	mnt.mnt_fsname = canonicalize (spec);
	mnt.mnt_dir = canonicalize (node);
	mnt.mnt_type = type;
	mnt.mnt_opts = opts;
	mnt.mnt_freq = freq;
	mnt.mnt_passno = pass;

	/* We get chatty now rather than after the update to mtab since the
	   mount succeeded, even if the write to /etc/mtab should fail.  */
	if (verbose)
		print_one (&mnt);

	if (!nomtab && mtab_is_writable()) {
		if (flags & MS_REMOUNT)
			update_mtab (mnt.mnt_dir, &mnt);
		else if (flags & MS_MOVE)
			update_mtab(mnt.mnt_fsname, &mnt);
		else {
			mntFILE *mfp;

			lock_mtab();
			mfp = my_setmntent(MOUNTED, "a+");
			if (mfp == NULL || mfp->mntent_fp == NULL) {
				int errsv = errno;
				error(_("mount: can't open %s: %s"), MOUNTED,
				      strerror (errsv));
			} else {
				if ((my_addmntent (mfp, &mnt)) == 1) {
					int errsv = errno;
					error(_("mount: error writing %s: %s"),
					      MOUNTED, strerror (errsv));
				}
			}
			my_endmntent(mfp);
			unlock_mtab();
		}
	}
	my_free(mnt.mnt_fsname);
	my_free(mnt.mnt_dir);
}

static void
set_pfd(char *s) {
	if (!isdigit(*s))
		die(EX_USAGE,
		    _("mount: argument to -p or --pass-fd must be a number"));
	pfd = atoi(optarg);
}

static void
cdrom_setspeed(const char *spec) {
#define CDROM_SELECT_SPEED      0x5322  /* Set the CD-ROM speed */
	if (opt_speed) {
		int cdrom;
		int speed = atoi(opt_speed);

		if ((cdrom = open(spec, O_RDONLY | O_NONBLOCK)) < 0)
			die(EX_FAIL,
			    _("mount: cannot open %s for setting speed"),
			    spec);
		if (ioctl(cdrom, CDROM_SELECT_SPEED, speed) < 0)
			die(EX_FAIL, _("mount: cannot set speed: %s"),
			    strerror(errno));
		close(cdrom);
	}
}

/*
 * try_mount_one()
 *	Try to mount one file system. When "bg" is 1, this is a retry
 *	in the background. One additional exit code EX_BG is used here.
 *	It is used to instruct the caller to retry the mount in the
 *	background.
 * returns: 0: OK, EX_SYSERR, EX_FAIL, return code from nfsmount,
 *      return status from wait
 */
static int
try_mount_one (const char *spec0, const char *node0, const char *types0,
	       const char *opts0, int freq, int pass, int bg, int ro) {
  int res = 0, status = 0, special = 0;
  int mnt5_res = 0;		/* only for gcc */
  int mnt_err;
  int flags;
  char *extra_opts;		/* written in mtab */
  char *mount_opts;		/* actually used on system call */
  const char *opts, *spec, *node, *types;
  char *user = 0;
  int loop = 0;
  const char *loopdev = 0, *loopfile = 0;
  struct stat statbuf;

  /* copies for freeing on exit */
  const char *opts1, *spec1, *node1, *types1, *extra_opts1;

  if (verbose > 2) {
	  printf("mount: spec:  \"%s\"\n", spec0);
	  printf("mount: node:  \"%s\"\n", node0);
	  printf("mount: types: \"%s\"\n", types0);
	  printf("mount: opts:  \"%s\"\n", opts0);
  }

  spec = spec1 = xstrdup(spec0);
  node = node1 = xstrdup(node0);
  types = types1 = xstrdup(types0);
  opts = opts1 = xstrdup(opts0);

  parse_opts (opts, &flags, &extra_opts);
  extra_opts1 = extra_opts;

  /* quietly succeed for fstab entries that don't get mounted automatically */
  if (mount_all && (flags & MS_NOAUTO))
      goto out;

  suid_check(spec, node, &flags, &user);

  /* The "mount -f" checks for for existing record in /etc/mtab (with
   * regular non-fake mount this is usually done by kernel)
   */
  if (fake && mounted (spec, node))
      die(EX_USAGE, _("mount: according to mtab, "
                      "%s is already mounted on %s\n"),
		      spec, node);

  mount_opts = extra_opts;

  if (opt_speed)
      cdrom_setspeed(spec);

  if (!(flags & MS_REMOUNT)) {
      /*
       * Don't set up a (new) loop device if we only remount - this left
       * stale assignments of files to loop devices. Nasty when used for
       * encryption.
       */
      res = loop_check(&spec, &types, &flags, &loop, &loopdev, &loopfile);
      if (res)
	  goto out;
  }

  if (loop)
      opt_loopdev = loopdev;

  /*
   * Call mount.TYPE for types that require a separate mount program.
   * For the moment these types are ncpfs and smbfs. Maybe also vxfs.
   * All such special things must occur isolated in the types string.
   */
  if (check_special_mountprog(spec, node, types, flags, extra_opts, &status)) {
      res = status;
      goto out;
  }

  block_signals (SIG_BLOCK);

  if (!fake) {
    mnt5_res = guess_fstype_and_mount (spec, node, &types, flags & ~MS_NOSYS,
				       mount_opts, &special, &status);

    if (special) {
      block_signals (SIG_UNBLOCK);
      res = status;
      goto out;
    }
  }

  if (fake || mnt5_res == 0) {
      /* Mount succeeded, report this (if verbose) and write mtab entry.  */

      if (!(mounttype & MS_PROPAGATION)) {
	      update_mtab_entry(loop ? loopfile : spec,
			node,
			types ? types : "unknown",
			fix_opts_string (flags & ~MS_NOMTAB, extra_opts, user),
			flags,
			freq,
			pass);
      }

      block_signals (SIG_UNBLOCK);
      res = 0;
      goto out;
  }

  mnt_err = errno;

  if (loop)
	del_loop(spec);

  block_signals (SIG_UNBLOCK);

  /* Mount failed, complain, but don't die.  */

  if (types == 0) {
    if (suid)
      error (_("mount: I could not determine the filesystem type, "
	       "and none was specified"));
    else
      error (_("mount: you must specify the filesystem type"));
  } else if (mnt5_res != -1) {
      /* should not happen */
      error (_("mount: mount failed"));
  } else {
   switch (mnt_err) {
    case EPERM:
      if (geteuid() == 0) {
	   if (stat (node, &statbuf) || !S_ISDIR(statbuf.st_mode))
		error (_("mount: mount point %s is not a directory"), node);
	   else
		error (_("mount: permission denied"));
      } else
	error (_("mount: must be superuser to use mount"));
      break;
    case EBUSY:
      if (flags & MS_REMOUNT) {
	error (_("mount: %s is busy"), node);
      } else if (!strcmp(types, "proc") && !strcmp(node, "/proc")) {
	/* heuristic: if /proc/version exists, then probably proc is mounted */
	if (stat ("/proc/version", &statbuf))   /* proc mounted? */
	   error (_("mount: %s is busy"), node);   /* no */
	else if (!mount_all || verbose)            /* yes, don't mention it */
	   error (_("mount: proc already mounted"));
      } else {
	error (_("mount: %s already mounted or %s busy"), spec, node);
	already (spec, node);
      }
      break;
    case ENOENT:
      if (lstat (node, &statbuf))
	   error (_("mount: mount point %s does not exist"), node);
      else if (stat (node, &statbuf))
	   error (_("mount: mount point %s is a symbolic link to nowhere"),
		  node);
      else if (stat (spec, &statbuf))
	   error (_("mount: special device %s does not exist"), spec);
      else {
	   errno = mnt_err;
	   perror("mount");
      }
      break;
    case ENOTDIR:
      if (stat (node, &statbuf) || ! S_ISDIR(statbuf.st_mode))
	   error (_("mount: mount point %s is not a directory"), node);
      else if (stat (spec, &statbuf) && errno == ENOTDIR)
	   error (_("mount: special device %s does not exist\n"
		    "       (a path prefix is not a directory)\n"), spec);
      else {
	   errno = mnt_err;
	   perror("mount");
      }
      break;
    case EINVAL:
    { int fd;
      unsigned long size;
      int warned=0;

      if (flags & MS_REMOUNT) {
	error (_("mount: %s not mounted already, or bad option"), node);
      } else {
	error (_("mount: wrong fs type, bad option, bad superblock on %s,\n"
	       "       missing codepage or other error"),
	       spec);

	if (stat(spec, &statbuf) == 0 && S_ISBLK(statbuf.st_mode)
	   && (fd = open(spec, O_RDONLY | O_NONBLOCK)) >= 0) {
	  if (ioctl(fd, BLKGETSIZE, &size) == 0) {
	    if (size == 0 && !loop) {
	      warned++;
	      error(_(
		 "       (could this be the IDE device where you in fact use\n"
		 "       ide-scsi so that sr0 or sda or so is needed?)"));
	    }
	    if (size && size <= 2) {
	      warned++;
	      error(_(
		  "       (aren't you trying to mount an extended partition,\n"
		  "       instead of some logical partition inside?)"));
	    }
	  close(fd);
	  }
#if 0
	  /* 0xf for SCSI, 0x3f for IDE. One might check /proc/partitions
	     to see whether this thing really is partitioned.
	     Do not suggest partitions for /dev/fd0. */
	  if (!warned && (statbuf.st_rdev & 0xf) == 0) {
	    warned++;
	    error ("       (could this be the whole disk device\n"
		   "       where you need a partition?)");
	  }
#endif
	}
	error(_(
		"       In some cases useful info is found in syslog - try\n"
		"       dmesg | tail  or so\n"));
      }
      break;
    }
    case EMFILE:
      error (_("mount table full")); break;
    case EIO:
      error (_("mount: %s: can't read superblock"), spec); break;
    case ENODEV:
    {
      int pfs = fsprobe_known_fstype_in_procfs(types);

      if (pfs == 1 || !strcmp(types, "guess"))
        error(_("mount: %s: unknown device"), spec);
      else if (pfs == 0) {
	char *lowtype, *p;
	int u;

	error (_("mount: unknown filesystem type '%s'"), types);

	/* maybe this loser asked for FAT or ISO9660 or isofs */
	lowtype = xstrdup(types);
	u = 0;
	for(p=lowtype; *p; p++) {
	  if(tolower(*p) != *p) {
	    *p = tolower(*p);
	    u++;
	  }
	}
	if (u && fsprobe_known_fstype_in_procfs(lowtype) == 1)
	  error (_("mount: probably you meant %s"), lowtype);
	else if (!strncmp(lowtype, "iso", 3) &&
			fsprobe_known_fstype_in_procfs("iso9660") == 1)
	  error (_("mount: maybe you meant 'iso9660'?"));
	else if (!strncmp(lowtype, "fat", 3) &&
			fsprobe_known_fstype_in_procfs("vfat") == 1)
	  error (_("mount: maybe you meant 'vfat'?"));
	free(lowtype);
      } else
	error (_("mount: %s has wrong device number or fs type %s not supported"),
	       spec, types);
      break;
    }
    case ENOTBLK:
      if (stat (spec, &statbuf)) /* strange ... */
	error (_("mount: %s is not a block device, and stat fails?"), spec);
      else if (S_ISBLK(statbuf.st_mode))
        error (_("mount: the kernel does not recognize %s as a block device\n"
	       "       (maybe `insmod driver'?)"), spec);
      else if (S_ISREG(statbuf.st_mode))
	error (_("mount: %s is not a block device (maybe try `-o loop'?)"),
		 spec);
      else
	error (_("mount: %s is not a block device"), spec);
      break;
    case ENXIO:
      error (_("mount: %s is not a valid block device"), spec); break;
    case EACCES:  /* pre-linux 1.1.38, 1.1.41 and later */
    case EROFS:   /* linux 1.1.38 and later */
    { char *bd = (loop ? "" : _("block device "));
      if (ro || (flags & MS_RDONLY)) {
          error (_("mount: cannot mount %s%s read-only"),
		 bd, spec);
          break;
      } else if (readwrite) {
	  error (_("mount: %s%s is write-protected but explicit `-w' flag given"),
		 bd, spec);
	  break;
      } else {
	 if (loop) {
	     opts = opts0;
	     types = types0;
	 }
         if (opts) {
	     char *opts2 = append_opt(xstrdup(opts), "ro", NULL);
	     my_free(opts1);
	     opts = opts1 = opts2;
         } else
             opts = "ro";
	 if (types && !strcmp(types, "guess"))
	     types = 0;
         error (_("mount: %s%s is write-protected, mounting read-only"),
		bd, spec0);
	 res = try_mount_one (spec0, node0, types, opts, freq, pass, bg, 1);
	 goto out;
      }
      break;
    }
    default:
      error ("mount: %s", strerror (mnt_err)); break;
    }
  }
  res = EX_FAIL;

 out:
  my_free(extra_opts1);
  my_free(spec1);
  my_free(node1);
  my_free(opts1);
  my_free(types1);

  return res;
}

/*
 * set_proc_name()
 *	Update the argument vector, so that this process may be easily
 *	identified in a "ps" listing.
 */
static void
set_proc_name (const char *spec)
{
#ifdef DO_PS_FIDDLING
	setproctitle ("mount", spec);
#endif
}

static char *
subst_string(const char *s, const char *sub, int sublen, const char *repl) {
	char *n;

	n = (char *) xmalloc(strlen(s)-sublen+strlen(repl)+1);
	strncpy (n, s, sub-s);
	strcpy (n + (sub-s), repl);
	strcat (n, sub+sublen);
	return n;
}

static char *
usersubst(const char *opts) {
	char *s, *w;
	char id[40];

	if (!opts)
		return NULL;

	s = "uid=useruid";
	if (opts && (w = strstr(opts, s)) != NULL) {
		sprintf(id, "uid=%d", getuid());
		opts = subst_string(opts, w, strlen(s), id);
	}
	s = "gid=usergid";
	if (opts && (w = strstr(opts, s)) != NULL) {
		sprintf(id, "gid=%d", getgid());
		opts = subst_string(opts, w, strlen(s), id);
	}
	return xstrdup(opts);
}

static int
is_existing_file (const char *s) {
	struct stat statbuf;

	return (stat(s, &statbuf) == 0);
}

/*
 * Return 0 for success (either mounted sth or -a and NOAUTO was given)
 */
static int
mount_one (const char *spec, const char *node, const char *types,
	   const char *fstabopts, char *cmdlineopts, int freq, int pass) {
	int status, status2;
	const char *nspec;
	char *opts;

	/* Substitute values in opts, if required */
	opts = usersubst(fstabopts);

	/* Merge the fstab and command line options.  */
	opts = append_opt(opts, cmdlineopts, NULL);

	/* Handle possible LABEL= and UUID= forms of spec */
	nspec = fsprobe_get_devname_for_mounting(spec);
	if (nspec)
		spec = nspec;

	if (types == NULL && !mounttype && !is_existing_file(spec)) {
		if (strchr (spec, ':') != NULL) {
			types = "nfs";
			if (verbose)
				printf(_("mount: no type was given - "
					 "I'll assume nfs because of "
					 "the colon\n"));
		} else if(!strncmp(spec, "//", 2)) {
			types = "cifs";
			if (verbose)
				printf(_("mount: no type was given - "
					 "I'll assume cifs because of "
					 "the // prefix\n"));
		}
	}

	/*
	 * Try to mount the file system. When the exit status is EX_BG,
	 * we will retry in the background. Otherwise, we're done.
	 */
	status = try_mount_one (spec, node, types, opts, freq, pass, 0, 0);
	if (status != EX_BG)
		return status;

	/*
	 * Retry in the background.
	 */
	printf (_("mount: backgrounding \"%s\"\n"), spec);
	fflush( stdout );		/* prevent duplicate output */
	if (fork() > 0)
		return 0;			/* parent returns "success" */
	spec = xstrdup(spec);		/* arguments will be destroyed */
	node = xstrdup(node);		/* by set_proc_name()          */
	types = xstrdup(types);
	set_proc_name (spec);		/* make a nice "ps" listing */
	status2 = try_mount_one (spec, node, types, opts, freq, pass, 1, 0);
	if (verbose && status2)
		printf (_("mount: giving up \"%s\"\n"), spec);

	my_free(opts);
	my_free(node);
	my_free(types);
	exit (0);			/* child stops here */
}

/* Check if an fsname/dir pair was already in the old mtab.  */
static int
mounted (const char *spec0, const char *node0) {
	struct mntentchn *mc, *mc0;
	char *spec, *node;
	int ret = 0;

	/* Handle possible UUID= and LABEL= in spec */
	spec0 = fsprobe_get_devname(spec0);
	if (!spec0)
		return ret;

	spec = canonicalize(spec0);
	node = canonicalize(node0);

	mc0 = mtab_head();
	for (mc = mc0->nxt; mc && mc != mc0; mc = mc->nxt)
		if (streq (spec, mc->m.mnt_fsname) &&
		    streq (node, mc->m.mnt_dir)) {
			ret = 1;
			break;
		}

	my_free(spec);
	my_free(node);

	return ret;
}

/* avoid using stat() on things we are not going to mount anyway.. */
static int
has_noauto (const char *opts) {
	char *s;

	if (!opts)
		return 0;
	s = strstr(opts, "noauto");
	if (!s)
		return 0;
	return (s == opts || s[-1] == ',') && (s[6] == 0 || s[6] == ',');
}

/* Mount all filesystems of the specified types except swap and root.  */
/* With the --fork option: fork and let different incarnations of
   mount handle different filesystems.  However, try to avoid several
   simultaneous mounts on the same physical disk, since that is very slow. */
#define DISKMAJOR(m)	(((int) m) & ~0xf)

static int
do_mount_all (char *types, char *options, char *test_opts) {
	struct mntentchn *mc, *mc0, *mtmp;
	int status = 0;
	struct stat statbuf;
	struct child {
		pid_t pid;
		char *group;
		struct mntentchn *mec;
		struct mntentchn *meclast;
		struct child *nxt;
	} childhead, *childtail, *cp;
	char major[22];
	char *g, *colon;

	/* build a chain of what we have to do, or maybe
	   several chains, one for each major or NFS host */
	childhead.nxt = 0;
	childtail = &childhead;
	mc0 = fstab_head();
	for (mc = mc0->nxt; mc && mc != mc0; mc = mc->nxt) {
		if (has_noauto (mc->m.mnt_opts))
			continue;
		if (matching_type (mc->m.mnt_type, types)
		    && matching_opts (mc->m.mnt_opts, test_opts)
		    && !streq (mc->m.mnt_dir, "/")
		    && !streq (mc->m.mnt_dir, "root")) {

			if (mounted (mc->m.mnt_fsname, mc->m.mnt_dir)) {
				if (verbose)
					printf(_("mount: %s already mounted "
						 "on %s\n"),
					       mc->m.mnt_fsname,
					       mc->m.mnt_dir);
				continue;
			}

			mtmp = (struct mntentchn *) xmalloc(sizeof(*mtmp));
			*mtmp = *mc;
			mtmp->nxt = 0;
			g = NULL;
			if (optfork) {
				if (stat(mc->m.mnt_fsname, &statbuf) == 0 &&
				    S_ISBLK(statbuf.st_mode)) {
					sprintf(major, "#%x",
						DISKMAJOR(statbuf.st_rdev));
					g = major;
				}
				if (strcmp(mc->m.mnt_type, "nfs") == 0) {
					g = xstrdup(mc->m.mnt_fsname);
					colon = strchr(g, ':');
					if (colon)
						*colon = '\0';
				}
			}
			if (g) {
				for (cp = childhead.nxt; cp; cp = cp->nxt)
					if (cp->group &&
					    strcmp(cp->group, g) == 0) {
						cp->meclast->nxt = mtmp;
						cp->meclast = mtmp;
						goto fnd;
					}
			}
			cp = (struct child *) xmalloc(sizeof *cp);
			cp->nxt = 0;
			cp->mec = cp->meclast = mtmp;
			cp->group = xstrdup(g);
			cp->pid = 0;
			childtail->nxt = cp;
			childtail = cp;
		fnd:;

		}
	}
			      
	/* now do everything */
	for (cp = childhead.nxt; cp; cp = cp->nxt) {
		pid_t p = -1;
		if (optfork) {
			p = fork();
			if (p == -1) {
				int errsv = errno;
				error(_("mount: cannot fork: %s"),
				      strerror (errsv));
			}
			else if (p != 0)
				cp->pid = p;
		}

		/* if child, or not forked, do the mounting */
		if (p == 0 || p == -1) {
			for (mc = cp->mec; mc; mc = mc->nxt) {
				status |= mount_one (mc->m.mnt_fsname,
						     mc->m.mnt_dir,
						     mc->m.mnt_type,
						     mc->m.mnt_opts,
						     options, 0, 0);
			}
			if (mountcount)
				status |= EX_SOMEOK;
			if (p == 0)
				exit(status);
		}
	}

	/* wait for children, if any */
	while ((cp = childhead.nxt) != NULL) {
		childhead.nxt = cp->nxt;
		if (cp->pid) {
			int ret;
		keep_waiting:
			if(waitpid(cp->pid, &ret, 0) == -1) {
				if (errno == EINTR)
					goto keep_waiting;
				perror("waitpid");
			} else if (WIFEXITED(ret))
				status |= WEXITSTATUS(ret);
			else
				status |= EX_SYSERR;
		}
	}
	if (mountcount)
		status |= EX_SOMEOK;
	return status;
}

static struct option longopts[] = {
	{ "all", 0, 0, 'a' },
	{ "fake", 0, 0, 'f' },
	{ "fork", 0, 0, 'F' },
	{ "help", 0, 0, 'h' },
	{ "no-mtab", 0, 0, 'n' },
	{ "read-only", 0, 0, 'r' },
	{ "ro", 0, 0, 'r' },
	{ "verbose", 0, 0, 'v' },
	{ "version", 0, 0, 'V' },
	{ "read-write", 0, 0, 'w' },
	{ "rw", 0, 0, 'w' },
	{ "options", 1, 0, 'o' },
	{ "test-opts", 1, 0, 'O' },
	{ "pass-fd", 1, 0, 'p' },
	{ "types", 1, 0, 't' },
	{ "bind", 0, 0, 128 },
	{ "replace", 0, 0, 129 },
	{ "after", 0, 0, 130 },
	{ "before", 0, 0, 131 },
	{ "over", 0, 0, 132 },
	{ "move", 0, 0, 133 },
	{ "guess-fstype", 1, 0, 134 },
	{ "rbind", 0, 0, 135 },
	{ "make-shared", 0, 0, 136 },
	{ "make-slave", 0, 0, 137 },
	{ "make-private", 0, 0, 138 },
	{ "make-unbindable", 0, 0, 139 },
	{ "make-rshared", 0, 0, 140 },
	{ "make-rslave", 0, 0, 141 },
	{ "make-rprivate", 0, 0, 142 },
	{ "make-runbindable", 0, 0, 143 },
	{ "internal-only", 0, 0, 'i' },
	{ NULL, 0, 0, 0 }
};

/* Keep the usage message at max 22 lines, each at most 70 chars long.
   The user should not need a pager to read it. */
static void
usage (FILE *fp, int n) {
	fprintf(fp, _(
	  "Usage: mount -V                 : print version\n"
	  "       mount -h                 : print this help\n"
	  "       mount                    : list mounted filesystems\n"
	  "       mount -l                 : idem, including volume labels\n"
	  "So far the informational part. Next the mounting.\n"
	  "The command is `mount [-t fstype] something somewhere'.\n"
	  "Details found in /etc/fstab may be omitted.\n"
	  "       mount -a [-t|-O] ...     : mount all stuff from /etc/fstab\n"
	  "       mount device             : mount device at the known place\n"
	  "       mount directory          : mount known device here\n"
	  "       mount -t type dev dir    : ordinary mount command\n"
	  "Note that one does not really mount a device, one mounts\n"
	  "a filesystem (of the given type) found on the device.\n"
	  "One can also mount an already visible directory tree elsewhere:\n"
	  "       mount --bind olddir newdir\n"
	  "or move a subtree:\n"
	  "       mount --move olddir newdir\n"
	  "One can change the type of mount containing the directory dir:\n"
	  "       mount --make-shared dir\n"
	  "       mount --make-slave dir\n"
	  "       mount --make-private dir\n"
	  "       mount --make-unbindable dir\n"
	  "One can change the type of all the mounts in a mount subtree\n"
	  "containing the directory dir:\n"
	  "       mount --make-rshared dir\n"
	  "       mount --make-rslave dir\n"
	  "       mount --make-rprivate dir\n"
	  "       mount --make-runbindable dir\n"
	  "A device can be given by name, say /dev/hda1 or /dev/cdrom,\n"
	  "or by label, using  -L label  or by uuid, using  -U uuid .\n"
	  "Other options: [-nfFrsvw] [-o options] [-p passwdfd].\n"
	  "For many more details, say  man 8 mount .\n"
	));
/*
	  "Union or stack mounts are specified using one of\n"
	  "       --replace, --after, --before, --over\n"
*/
	unlock_mtab();
	exit (n);
}

/* returns mount entry from fstab */
static struct mntentchn *
getfs(const char *spec, const char *uuid, const char *label)
{
	struct mntentchn *mc = NULL;
	const char *devname = NULL;

	/*
	 * A) 99% of all cases, the spec on cmdline matches
	 *    with spec in fstab
	 */
	if (uuid)
		mc = getfs_by_uuid(uuid);
	else if (label)
		mc = getfs_by_label(label);
	else {
		mc = getfs_by_spec(spec);

		if (!mc)
			mc = getfs_by_dir(spec);
	}
	if (mc)
		return mc;

	/*
	 * B) UUID or LABEL on cmdline, but devname in fstab
	 */
	if (uuid)
		devname = fsprobe_get_devname_by_uuid(uuid);
	else if (label)
		devname = fsprobe_get_devname_by_label(label);
	else
		devname = fsprobe_get_devname(spec);

	if (devname)
		mc = getfs_by_devname(devname);

	/*
	 * C) mixed
	 */
	if (!mc && devname) {
		const char *id = NULL;

		if (!label && (!spec || strncmp(spec, "LABEL=", 6))) {
			id = fsprobe_get_label_by_devname(devname);
			if (id)
				mc = getfs_by_label(id);
		}
		if (!mc && !uuid && (!spec || strncmp(spec, "UUID=", 5))) {
			id = fsprobe_get_uuid_by_devname(devname);
			if (id)
				mc = getfs_by_uuid(id);
		}
		my_free(id);

		if (mc) {
			/* use real device name to avoid repetitional
			 * conversion from LABEL/UUID to devname
			 */
			my_free(mc->m.mnt_fsname);
			mc->m.mnt_fsname = xstrdup(devname);
		}
	}

	/*
	 * D) remount -- try /etc/mtab
	 *    Earlier mtab was tried first, but this would sometimes try the
	 *    wrong mount in case mtab had the root device entry wrong.
	 */
	if (!mc)
		mc = getmntfile (devname ? devname : spec);

	if (devname)
		my_free(devname);
	return mc;
}

char *progname;

int
main(int argc, char *argv[]) {
	int c, result = 0, specseen;
	char *options = NULL, *test_opts = NULL, *node;
	const char *spec = NULL;
	char *label = NULL;
	char *uuid = NULL;
	char *types = NULL;
	char *p;
	struct mntentchn *mc;
	int fd;

	sanitize_env();
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	progname = argv[0];
	if ((p = strrchr(progname, '/')) != NULL)
		progname = p+1;

	umask(022);

	/* People report that a mount called from init without console
	   writes error messages to /etc/mtab
	   Let us try to avoid getting fd's 0,1,2 */
	while((fd = open("/dev/null", O_RDWR)) == 0 || fd == 1 || fd == 2) ;
	if (fd > 2)
		close(fd);

	fsprobe_init();

#ifdef DO_PS_FIDDLING
	initproctitle(argc, argv);
#endif

	while ((c = getopt_long (argc, argv, "afFhilL:no:O:p:rsU:vVwt:",
				 longopts, NULL)) != -1) {
		switch (c) {
		case 'a':	       /* mount everything in fstab */
			++mount_all;
			break;
		case 'f':	       /* fake: don't actually call mount(2) */
			++fake;
			break;
		case 'F':
			++optfork;
			break;
		case 'h':		/* help */
			usage (stdout, 0);
			break;
		case 'i':
			external_allowed = 0;
			break;
		case 'l':
			list_with_volumelabel = 1;
			break;
		case 'L':
			label = optarg;
			break;
		case 'n':		/* do not write /etc/mtab */
			++nomtab;
			break;
		case 'o':		/* specify mount options */
			options = append_opt(options, optarg, NULL);
			break;
		case 'O':		/* with -t: mount only if (not) opt */
			test_opts = append_opt(test_opts, optarg, NULL);
			break;
		case 'p':		/* fd on which to read passwd */
			set_pfd(optarg);
			break;
		case 'r':		/* mount readonly */
			readonly = 1;
			readwrite = 0;
			break;
		case 's':		/* allow sloppy mount options */
			sloppy = 1;
			break;
		case 't':		/* specify file system types */
			types = optarg;
			break;
		case 'U':
			uuid = optarg;
			break;
		case 'v':		/* be chatty - more so if repeated */
			++verbose;
			break;
		case 'V':		/* version */
			printf ("mount (%s)\n", PACKAGE_STRING);
			exit (0);
		case 'w':		/* mount read/write */
			readwrite = 1;
			readonly = 0;
			break;
		case 0:
			break;

		case 128: /* bind */
			mounttype = MS_BIND;
			break;
		case 129: /* replace */
			mounttype = MS_REPLACE;
			break;
		case 130: /* after */
			mounttype = MS_AFTER;
			break;
		case 131: /* before */
			mounttype = MS_BEFORE;
			break;
		case 132: /* over */
			mounttype = MS_OVER;
			break;
		case 133: /* move */
			mounttype = MS_MOVE;
			break;
		case 134:
			/* undocumented, may go away again:
			   call: mount --guess-fstype device
			   use only for testing purposes -
			   the guessing is not reliable at all */
		    {
			const char *fstype;
			fstype = fsprobe_get_fstype_by_devname(optarg);
			printf("%s\n", fstype ? fstype : "unknown");
			exit(fstype ? 0 : EX_FAIL);
		    }
		case 135:
			mounttype = (MS_BIND | MS_REC);
			break;

		case 136:
			mounttype = MS_SHARED;
			break;

		case 137:
			mounttype = MS_SLAVE;
			break;

		case 138:
			mounttype = MS_PRIVATE;
			break;

		case 139:
			mounttype = MS_UNBINDABLE;
			break;

		case 140:
			mounttype = (MS_SHARED | MS_REC);
			break;

		case 141:
			mounttype = (MS_SLAVE | MS_REC);
			break;

		case 142:
			mounttype = (MS_PRIVATE | MS_REC);
			break;

		case 143:
			mounttype = (MS_UNBINDABLE | MS_REC);
			break;

		case '?':
		default:
			usage (stderr, EX_USAGE);
		}
	}

	if (verbose > 2) {
		printf("mount: fstab path: \"%s\"\n", _PATH_FSTAB);
		printf("mount: lock path:  \"%s\"\n", MOUNTED_LOCK);
		printf("mount: temp path:  \"%s\"\n", MOUNTED_TEMP);
	}

	argc -= optind;
	argv += optind;

	specseen = (uuid || label) ? 1 : 0;	/* yes, .. i know */

	if (argc+specseen == 0 && !mount_all) {
		if (options || mounttype)
			usage (stderr, EX_USAGE);
		return print_all (types);
	}

	if (getuid () != geteuid ()) {
		suid = 1;
		if (types || options || readwrite || nomtab || mount_all ||
		    fake || mounttype || (argc + specseen) != 1)
			die (EX_USAGE, _("mount: only root can do that"));
	}

	if (!nomtab && mtab_does_not_exist()) {
		if (verbose > 1)
			printf(_("mount: no %s found - creating it..\n"),
			       MOUNTED);
		create_mtab ();
	}

	switch (argc+specseen) {
	case 0:
		/* mount -a */
		result = do_mount_all (types, options, test_opts);
		if (result == 0 && verbose && !fake)
			error(_("nothing was mounted"));
		break;

	case 1:
		/* mount [-nfrvw] [-o options] special | node
		 * (/etc/fstab is necessary)
		 */
		if (types != NULL)
			usage (stderr, EX_USAGE);

		mc = getfs(*argv, uuid, label);
		if (!mc) {
			if (uuid || label)
				die (EX_USAGE, _("mount: no such partition found"));

			die (EX_USAGE,
			     _("mount: can't find %s in %s or %s"),
			     *argv, _PATH_FSTAB, MOUNTED);
		}

		result = mount_one (xstrdup (mc->m.mnt_fsname),
				    xstrdup (mc->m.mnt_dir),
				    xstrdup (mc->m.mnt_type),
				    mc->m.mnt_opts, options, 0, 0);
		break;

	case 2:
		/* mount special node  (/etc/fstab is not necessary) */
		if (specseen) {
			/* mount -L label node   (or -U uuid) */
			spec = uuid ?	fsprobe_get_devname_by_uuid(uuid) :
					fsprobe_get_devname_by_label(label);
			node = argv[0];
		} else {
			/* mount special node */
			spec = argv[0];
			node = argv[1];
		}
		if (!spec)
			die (EX_USAGE, _("mount: no such partition found"));

		result = mount_one (spec, node, types, NULL, options, 0, 0);
		break;

	default:
		usage (stderr, EX_USAGE);
	}

	if (result == EX_SOMEOK)
		result = 0;

	fsprobe_exit();

	exit (result);
}
