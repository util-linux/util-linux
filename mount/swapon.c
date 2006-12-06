/*
 * A swapon(8)/swapoff(8) for Linux 0.99.
 * swapon.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 */

#include "sundries.h"

/* Nonzero for chatty (-v).  This is a nonstandard flag (not in BSD).  */
int verbose = 0;
#ifdef SUPPORT_PRIORITIES
int priority = -1;	/* non-prioritized swap by default */
#endif

extern char version[];
static char *program_name;
static struct option longopts[] =
{
  { "all", 0, 0, 'a' },
  { "help", 0, 0, 'h' },
#ifdef SUPPORT_PRIORITIES
  { "priority", required_argument, 0, 'p' },
#endif
  { "verbose", 0, 0, 'v' },
  { "version", 0, 0, 'V' },
  { NULL, 0, 0, 0 }
};

#ifdef SUPPORT_PRIORITIES
const char *usage_string = "\
usage: %s [-hV]\n\
       %s -a [-v]\n\
       %s [-v] [-p priority] special ...\n\
";
#else
const char *usage_string = "\
usage: %s [-hV]\n\
       %s -a [-v]\n\
       %s [-v] [-p priority] special ...\n\
";
#endif

static void
usage (FILE *fp, int n)
{
  fprintf (fp, usage_string, program_name, program_name, program_name);
  exit (n);
}

static int
#ifdef SUPPORT_PRIORITIES
swap (const char *special, int prio)
#else
swap (const char *special)
#endif
{
  int status;
#ifdef SUPPORT_PRIORITIES
  int flags;
#endif

  if (verbose)
    printf("%s on device %s\n", program_name, special);

  if (streq (program_name, "swapon")) {
#ifdef SUPPORT_PRIORITIES
     flags = 0;
     if (prio >= 0) {
	flags = SWAP_FLAG_PREFER
		| ((prio & SWAP_FLAG_PRIO_MASK) << SWAP_FLAG_PRIO_SHIFT);
     }
     status = swapon (special, flags);
#else
     status = swapon (special);
#endif
  } else
     status = swapoff (special);

  if (status < 0)
    fprintf (stderr, "%s: %s: %s\n", program_name, special, strerror (errno));

  return status;
}

int
main (int argc, char *argv[])
{
  struct fstab *fstab;
  int status;
  int all = 0;
  int c;

  if (strrchr (argv[0], '/') != NULL)
    program_name = strrchr (argv[0], '/') + 1;
  else
    program_name = argv[0];

#ifdef SUPPORT_PRIORITIES
  while ((c = getopt_long (argc, argv, "ahp:vV", longopts, NULL)) != EOF)
#else
  while ((c = getopt_long (argc, argv, "ahvV", longopts, NULL)) != EOF)
#endif
    switch (c)
      {
      case 'a':			/* all */
	++all;
	break;
      case 'h':			/* help */
	usage (stdout, 0);
	break;
#ifdef SUPPORT_PRIORITIES
      case 'p':			/* priority */
	priority = atoi(optarg);
	break;
#endif
      case 'v':			/* be chatty */
	++verbose;
	break;
      case 'V':			/* version */
	printf ("swapon: %s\n", version);
	exit (0);
      case 0:
	break;
      case '?':
      default:
	usage (stderr, 1);
      }

  argv += optind;

  status = 0;

  if (all)
    {
      while ((fstab = getfsent()) != NULL)
	if (streq (fstab->fs_type, FSTAB_SW))
	{
#ifdef SUPPORT_PRIORITIES
	   /* parse mount options; */
	   char *opt, *opts = strdup(fstab->fs_mntopts);
	   
	   for (opt = strtok (opts, ",");
		opt != NULL;
		opt = strtok (NULL, ","))
	   {
	      if (strncmp(opt, "pri=", 4) == 0)
	      {
		 priority = atoi(opt+4);
	      }
	   }
	   status |= swap (fstab->fs_spec, priority);
#else
	   status |= swap (fstab->fs_spec);
#endif
	}
    }
  else if (*argv == NULL)
    {
      usage (stderr, 2);
    }
  else
    {
       while (*argv != NULL) {
#ifdef SUPPORT_PRIORITIES
	  status |= swap (*argv++,priority);
#else
	  status |= swap (*argv++);
#endif
       }
    }
  return status;
}
