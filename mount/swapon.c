/*
 * A swapon(8)/swapoff(8) for Linux 0.99.
 * swapon.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 */

#include "sundries.h"

/* Nonzero for chatty (-v).  This is a nonstandard flag (not in BSD).  */
int verbose = 0;

extern char version[];
static char *program_name;
static struct option longopts[] =
{
  { "all", 0, 0, 'a' },
  { "help", 0, 0, 'h' },
  { "verbose", 0, 0, 'v' },
  { "version", 0, 0, 'V' },
  { NULL, 0, 0, 0 }
};

const char *usage_string = "\
usage: %s [-hV]\n\
       %s -a [-v]\n\
       %s [-v] special ...\n\
";

static void
usage (FILE *fp, int n)
{
  fprintf (fp, usage_string, program_name, program_name, program_name);
  exit (n);
}

static int
swap (const char *special)
{
  int status;

  if (verbose)
    printf("%s on device %s\n", program_name, special);

  if (streq (program_name, "swapon"))
    status = swapon (special);
  else
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

  while ((c = getopt_long (argc, argv, "ahvV", longopts, NULL)) != EOF)
    switch (c)
      {
      case 'a':			/* all */
	++all;
	break;
      case 'h':			/* help */
	usage (stdout, 0);
	break;
      case 'v':			/* be chatty */
	++verbose;
	break;
      case 'V':			/* version */
	printf ("%s\n", version);
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
	  status |= swap (fstab->fs_spec);
    }
  else if (*argv == NULL)
    {
      usage (stderr, 2);
    }
  else
    {
      while (*argv != NULL)
	status |= swap (*argv++);
    }
  return status;
}
