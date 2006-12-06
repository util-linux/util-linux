/*
 * A swapon(8)/swapoff(8) for Linux 0.99.
 * swapon.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 *
 * 1997-02-xx <Vincent.Renardias@waw.com>
 * - added '-s' (summary option)
 * 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 * 1999-03-21 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 * - fixed strerr(errno) in gettext calls
 * 2001-03-22 Erik Troan <ewt@redhat.com>
 * - added -e option for -a
 * - -a shouldn't try to add swaps that are already enabled
 */

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <mntent.h>
#include <errno.h>
#include <sys/stat.h>
#include "swap_constants.h"
#include "swapargs.h"
#include "nls.h"

#define streq(s, t)	(strcmp ((s), (t)) == 0)

#define	_PATH_FSTAB     "/etc/fstab"
#define PROC_SWAPS      "/proc/swaps"

#define SWAPON_NEEDS_TWO_ARGS

#define QUIET	1

int all = 0;
int verbose = 0;
int priority = -1;	/* non-prioritized swap by default */

/* If true, don't complain if the device/file doesn't exist */
int ifexists = 0;

extern char version[];
static char *program_name;

static struct option longswaponopts[] = {
		/* swapon only */
	{ "priority", required_argument, 0, 'p' },
	{ "ifexists", 0, 0, 'e' },
	{ "summary", 0, 0, 's' },
		/* also for swapoff */
	{ "all", 0, 0, 'a' },
	{ "help", 0, 0, 'h' },
	{ "verbose", 0, 0, 'v' },
	{ "version", 0, 0, 'V' },
	{ NULL, 0, 0, 0 }
};

static struct option *longswapoffopts = &longswaponopts[2];

static void
swapon_usage(FILE *fp, int n) {
	fprintf(fp, _("usage: %s [-hV]\n"
		      "       %s -a [-e] [-v]\n"
		      "       %s [-v] [-p priority] special ...\n"
		      "       %s [-s]\n"),
		program_name, program_name, program_name, program_name);
	exit(n);
}

static void
swapoff_usage(FILE *fp, int n) {
	fprintf(fp, _("usage: %s [-hV]\n"
		      "       %s -a [-v]\n"
		      "       %s [-v] special ...\n"),
		program_name, program_name, program_name);
	exit(n);
}

#ifdef SWAPON_HAS_TWO_ARGS
#define SWAPON_NEEDS_TWO_ARGS
#endif

#ifdef SWAPON_NEEDS_TWO_ARGS
#ifdef SWAPON_HAS_TWO_ARGS
/* libc is OK */
#include <unistd.h>
#else
/* We want a swapon with two args, but have an old libc.
   Build the kernel call by hand. */
#include <linux/unistd.h>
static
_syscall2(int,  swapon,  const char *,  path, int, flags);
static
_syscall1(int,  swapoff,  const char *,  path);
#endif
#else
/* just do as libc says */
#include <unistd.h>
#endif


/*
 * contents of /proc/swaps
 */
static int numSwaps;
static char **swapFiles;	/* array of swap file and partition names */

static void
read_proc_swaps(void) {
	FILE *swaps;
	char line[1024];
	char *p;

	numSwaps = 0;
	swapFiles = NULL;

	swaps = fopen(PROC_SWAPS, "r");
	if (swaps == NULL)
		return;		/* nothing wrong */

	/* skip the first line */
	fgets(line, sizeof(line), swaps);

	while (fgets(line, sizeof(line), swaps)) {
		/*
		 * Cut the line "swap_device  ... more info" after device.
		 * This will fail with names with embedded spaces.
		 */
		for (p = line; *p && *p != ' '; p++);
		*p = 0;

		numSwaps++;
		swapFiles = realloc(swapFiles,
				    numSwaps * sizeof(*swapFiles));
		swapFiles[numSwaps-1] = strdup(line);
	}
	fclose(swaps);
}

static int
is_in_proc_swaps(char *fname) {
	int i;

	for (i = 0; i < numSwaps; i++)
		if (!strcmp(fname, swapFiles[i]))
			return 1;
	return 0;
}

static int
display_summary(void)
{
       FILE *swaps;
       char line[1024] ;

       if ((swaps = fopen(PROC_SWAPS, "r")) == NULL) {
       	       int errsv = errno;
               fprintf(stderr, "%s: %s: %s\n", program_name, PROC_SWAPS,
			strerror(errsv));
               return -1 ; 
       }

       while (fgets(line, sizeof(line), swaps))
               printf("%s", line);

       fclose(swaps);
       return 0 ;
}

static int
do_swapon(const char *special, int prio) {
	int status;
	struct stat st;

	if (verbose)
		printf(_("%s on %s\n"), program_name, special);

	if (stat(special, &st) < 0) {
		int errsv = errno;
		fprintf(stderr, _("swapon: cannot stat %s: %s\n"),
			 special, strerror(errsv));
		return -1;
	}

	/* people generally dislike this warning - now it is printed
	   only when `verbose' is set */
	if (verbose) {
		int permMask = (S_ISBLK(st.st_mode) ? 07007 : 07077);

		if ((st.st_mode & permMask) != 0) {
			fprintf(stderr, _("swapon: warning: %s has "
					  "insecure permissions %04o, "
					  "%04o suggested\n"),
				special, st.st_mode & 07777,
				~permMask & 0666);
		}
	}

	/* test for holes by LBT */
	if (S_ISREG(st.st_mode)) {
		if (st.st_blocks * 512 < st.st_size) {
			fprintf(stderr,
				_("swapon: Skipping file %s - it appears "
				  "to have holes.\n"),
				special);
			return -1;
		}
	}

#ifdef SWAPON_NEEDS_TWO_ARGS
	{
		int flags = 0;

#ifdef SWAP_FLAG_PREFER
		if (prio >= 0) {
			if (prio > SWAP_FLAG_PRIO_MASK)
				prio = SWAP_FLAG_PRIO_MASK;
			flags = SWAP_FLAG_PREFER
				| ((prio & SWAP_FLAG_PRIO_MASK)
				   << SWAP_FLAG_PRIO_SHIFT);
		}
#endif
		status = swapon(special, flags);
	}
#else
	status = swapon(special);
#endif
	if (status < 0) {
		int errsv = errno;
		fprintf(stderr, "%s: %s: %s\n", program_name,
			 special, strerror(errsv));
	}

	return status;
}

static int
do_swapoff(const char *special, int quiet) {
	if (verbose)
		printf(_("%s on %s\n"), program_name, special);

	if (swapoff(special) == 0)
		return 0;	/* success */

	if (errno == EPERM) {
		fprintf(stderr, _("Not superuser.\n"));
		exit(1);	/* any further swapoffs will also fail */
	}

	if (!quiet || errno == ENOMEM) {
		int errsv = errno;
		fprintf(stderr, "%s: %s: %s\n", program_name,
			 special, strerror(errsv));
	}
	return -1;
}

static int
main_swapon(int argc, char *argv[]) {
	FILE *fp;
	struct mntent *fstab;
	int status = 0;
	int c;

	while ((c = getopt_long(argc, argv, "ahep:svV",
				 longswaponopts, NULL)) != -1) {
		switch (c) {
		case 'a':		/* all */
			++all;
			break;
		case 'h':		/* help */
			swapon_usage(stdout, 0);
			break;
		case 'p':		/* priority */
			priority = atoi(optarg);
			break;
		case 'e':               /* ifexists */
		        ifexists = 1;
			break;
		case 's':		/* status report */
			status = display_summary();
			exit(status);
		case 'v':		/* be chatty */
			++verbose;
			break;
		case 'V':		/* version */
			printf("%s: %s\n", program_name, version);
			exit(0);
		case 0:
			break;
		case '?':
		default:
			swapon_usage(stderr, 1);
		}
	}
	argv += optind;

	if (!all && *argv == NULL)
		swapon_usage(stderr, 2);

	if (ifexists && (!all || strcmp(program_name, "swapon")))
	  swapon_usage(stderr, 1);

	if (all) {
		read_proc_swaps();

		fp = setmntent(_PATH_FSTAB, "r");
		if (fp == NULL) {
			int errsv = errno;
			fprintf(stderr, _("%s: cannot open %s: %s\n"),
				program_name, _PATH_FSTAB, strerror(errsv));
			exit(2);
		}
		while ((fstab = getmntent(fp)) != NULL) {
			char *special = fstab->mnt_fsname;
			int skip = 0;

			if (streq(fstab->mnt_type, MNTTYPE_SWAP) &&
			    !is_in_proc_swaps(special)
			    && (!ifexists || !access(special, R_OK))) {
				/* parse mount options; */
				char *opt, *opts = strdup(fstab->mnt_opts);
	   
				for (opt = strtok(opts, ","); opt != NULL;
				     opt = strtok(NULL, ",")) {
					if (strncmp(opt, "pri=", 4) == 0)
						priority = atoi(opt+4);
					if (strcmp(opt, "noauto") == 0)
						skip = 1;
				}
				if (!skip)
					status |= do_swapon(special, priority);
			}
		}
		fclose(fp);
	}

	while (*argv != NULL)
		status |= do_swapon(*argv++, priority);

	return status;
}

static int
main_swapoff(int argc, char *argv[]) {
	FILE *fp;
	struct mntent *fstab;
	int status = 0;
	int c, i;

	while ((c = getopt_long(argc, argv, "ahvV",
				 longswapoffopts, NULL)) != -1) {
		switch (c) {
		case 'a':		/* all */
			++all;
			break;
		case 'h':		/* help */
			swapoff_usage(stdout, 0);
			break;
		case 'v':		/* be chatty */
			++verbose;
			break;
		case 'V':		/* version */
			printf("%s: %s\n", program_name, version);
			exit(0);
		case 0:
			break;
		case '?':
		default:
			swapoff_usage(stderr, 1);
		}
	}
	argv += optind;

	if (!all && *argv == NULL)
		swapoff_usage(stderr, 2);

	/*
	 * swapoff any explicitly given arguments.
	 * Complain in case the swapoff call fails.
	 */
	while (*argv != NULL)
		status |= do_swapoff(*argv++, !QUIET);

	if (all) {
		/*
		 * In case /proc/swaps exists, unmount stuff listed there.
		 * We are quiet but report errors in status.
		 * Errors might mean that /proc/swaps
		 * exists as ordinary file, not in procfs.
		 * do_swapoff() exits immediately on EPERM.
		 */
		read_proc_swaps();
		for(i=0; i<numSwaps; i++)
			status |= do_swapoff(swapFiles[i], QUIET);

		/*
		 * Unmount stuff mentioned in /etc/fstab.
		 * Probably it was unmounted already, so errors are not bad.
		 * Doing swapoff -a twice should not give error messages.
		 */
		fp = setmntent(_PATH_FSTAB, "r");
		if (fp == NULL) {
			int errsv = errno;
			fprintf(stderr, _("%s: cannot open %s: %s\n"),
				program_name, _PATH_FSTAB, strerror(errsv));
			exit(2);
		}
		while ((fstab = getmntent(fp)) != NULL) {
			if (streq(fstab->mnt_type, MNTTYPE_SWAP) &&
			    !is_in_proc_swaps(fstab->mnt_fsname))
				do_swapoff(fstab->mnt_fsname, QUIET);
		}
		fclose(fp);
	}

	return status;
}

int
main(int argc, char *argv[]) {
	char *p;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	program_name = argv[0];
	p = strrchr(program_name, '/');
	if (p)
		program_name = p+1;

	if (streq(program_name, "swapon"))
		return main_swapon(argc, argv);
	else
		return main_swapoff(argc, argv);
}
