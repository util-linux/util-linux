/*
 * A swapon(8)/swapoff(8) for Linux 0.99.
 * swapon.c,v 1.1.1.1 1993/11/18 08:40:51 jrs Exp
 */

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <mntent.h>
#include <errno.h>
#include <sys/stat.h>
#include "xmalloc.h"
#include "swap_constants.h"
#include "swapargs.h"
#include "nls.h"
#include "mount_blkid.h"
#include "mount_by_label.h"

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

char *progname;

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
		      "       %s [-v] [-p priority] special|LABEL=volume_name ...\n"
		      "       %s [-s]\n"),
		progname, progname, progname, progname);
	exit(n);
}

static void
swapoff_usage(FILE *fp, int n) {
	fprintf(fp, _("usage: %s [-hV]\n"
		      "       %s -a [-v]\n"
		      "       %s [-v] special ...\n"),
		progname, progname, progname);
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
	char *p, **q;

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

		q = realloc(swapFiles, (numSwaps+1) * sizeof(*swapFiles));
		if (q == NULL)
			break;
		swapFiles = q;

		swapFiles[numSwaps++] = strdup(line);
	}
	fclose(swaps);
}

static int
is_in_proc_swaps(const char *fname) {
	int i;

	for (i = 0; i < numSwaps; i++)
		if (swapFiles[i] && !strcmp(fname, swapFiles[i]))
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
               fprintf(stderr, "%s: %s: %s\n", progname, PROC_SWAPS,
			strerror(errsv));
               return -1 ; 
       }

       while (fgets(line, sizeof(line), swaps))
               printf("%s", line);

       fclose(swaps);
       return 0 ;
}

static int
do_swapon(const char *orig_special, int prio) {
	int status;
	struct stat st;
	const char *special;

	if (verbose)
		printf(_("%s on %s\n"), progname, orig_special);

	special = mount_get_devname(orig_special);
	if (!special) {
		fprintf(stderr, _("%s: cannot find the device for %s\n"),
			progname, orig_special);
		return -1;
	}

	if (stat(special, &st) < 0) {
		int errsv = errno;
		fprintf(stderr, _("%s: cannot stat %s: %s\n"),
			progname, special, strerror(errsv));
		return -1;
	}

	/* people generally dislike this warning - now it is printed
	   only when `verbose' is set */
	if (verbose) {
		int permMask = (S_ISBLK(st.st_mode) ? 07007 : 07077);

		if ((st.st_mode & permMask) != 0) {
			fprintf(stderr, _("%s: warning: %s has "
					  "insecure permissions %04o, "
					  "%04o suggested\n"),
				progname, special, st.st_mode & 07777,
				~permMask & 0666);
		}
	}

	/* test for holes by LBT */
	if (S_ISREG(st.st_mode)) {
		if (st.st_blocks * 512 < st.st_size) {
			fprintf(stderr,
				_("%s: Skipping file %s - it appears "
				  "to have holes.\n"),
				progname, special);
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
		fprintf(stderr, "%s: %s: %s\n",
			progname, orig_special, strerror(errsv));
	}

	return status;
}

static int
cannot_find(const char *special) {
	fprintf(stderr, _("%s: cannot find the device for %s\n"),
		progname, special);
	return -1;
}

static int
swapon_by_label(const char *label, int prio) {
	const char *special = mount_get_devname_by_label(label);
	return special ? do_swapon(special, prio) : cannot_find(label);
}

static int
swapon_by_uuid(const char *uuid, int prio) {
	const char *special = mount_get_devname_by_uuid(uuid);
	return special ? do_swapon(special, prio) : cannot_find(uuid);
}

static int
do_swapoff(const char *orig_special, int quiet) {
        const char *special;

	if (verbose)
		printf(_("%s on %s\n"), progname, orig_special);

	special = mount_get_devname(orig_special);
	if (!special)
		return cannot_find(orig_special);

	if (swapoff(special) == 0)
		return 0;	/* success */

	if (errno == EPERM) {
		fprintf(stderr, _("Not superuser.\n"));
		exit(1);	/* any further swapoffs will also fail */
	}

	if (!quiet || errno == ENOMEM) {
		fprintf(stderr, "%s: %s: %s\n",
			progname, orig_special, strerror(errno));
	}
	return -1;
}

static int
swapoff_by_label(const char *label, int quiet) {
	const char *special = mount_get_devname_by_label(label);
	return special ? do_swapoff(special, quiet) : cannot_find(label);
}

static int
swapoff_by_uuid(const char *uuid, int quiet) {
	const char *special = mount_get_devname_by_uuid(uuid);
	return special ? do_swapoff(special, quiet) : cannot_find(uuid);
}

static int
swapon_all(void) {
	FILE *fp;
	struct mntent *fstab;
	int status = 0;

	read_proc_swaps();

	fp = setmntent(_PATH_FSTAB, "r");
	if (fp == NULL) {
		int errsv = errno;
		fprintf(stderr, _("%s: cannot open %s: %s\n"),
			progname, _PATH_FSTAB, strerror(errsv));
		exit(2);
	}

	while ((fstab = getmntent(fp)) != NULL) {
		const char *orig_special = fstab->mnt_fsname;
		const char *special;
		int skip = 0;
		int pri = priority;

		if (!streq(fstab->mnt_type, MNTTYPE_SWAP))
			continue;

		special = mount_get_devname(orig_special);
		if (!special)
			continue;

		if (!is_in_proc_swaps(special) &&
		    (!ifexists || !access(special, R_OK))) {
			/* parse mount options; */
			char *opt, *opts = strdup(fstab->mnt_opts);
	   
			for (opt = strtok(opts, ","); opt != NULL;
			     opt = strtok(NULL, ",")) {
				if (strncmp(opt, "pri=", 4) == 0)
					pri = atoi(opt+4);
				if (strcmp(opt, "noauto") == 0)
					skip = 1;
			}
			if (!skip)
				status |= do_swapon(special, pri);
		}
	}
	fclose(fp);

	return status;
}

static const char **llist = NULL;
static int llct = 0;
static const char **ulist = NULL;
static int ulct = 0;

static void addl(const char *label) {
	llist = (const char **) xrealloc(llist, (++llct) * sizeof(char *));
	llist[llct-1] = label;
}

static void addu(const char *uuid) {
	ulist = (const char **) xrealloc(ulist, (++ulct) * sizeof(char *));
	ulist[ulct-1] = uuid;
}

static int
main_swapon(int argc, char *argv[]) {
	int status = 0;
	int c, i;

	while ((c = getopt_long(argc, argv, "ahep:svVL:U:",
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
		case 'L':
			addl(optarg);
			break;
		case 'U':
			addu(optarg);
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
			printf("%s: (%s)\n", progname, PACKAGE_STRING);
			exit(0);
		case 0:
			break;
		case '?':
		default:
			swapon_usage(stderr, 1);
		}
	}
	argv += optind;

	if (!all && !llct && !ulct && *argv == NULL)
		swapon_usage(stderr, 2);

	if (ifexists && (!all || strcmp(progname, "swapon")))
		swapon_usage(stderr, 1);

	if (all)
		status |= swapon_all();

	for (i = 0; i < llct; i++)
		status |= swapon_by_label(llist[i], priority);

	for (i = 0; i < ulct; i++)
		status |= swapon_by_uuid(ulist[i], priority);

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

	while ((c = getopt_long(argc, argv, "ahvVL:U:",
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
			printf("%s (%s)\n", progname, PACKAGE_STRING);
			exit(0);
		case 'L':
			addl(optarg);
			break;
		case 'U':
			addu(optarg);
			break;
		case 0:
			break;
		case '?':
		default:
			swapoff_usage(stderr, 1);
		}
	}
	argv += optind;

	if (!all && !llct && !ulct && *argv == NULL)
		swapoff_usage(stderr, 2);

	/*
	 * swapoff any explicitly given arguments.
	 * Complain in case the swapoff call fails.
	 */
	for (i = 0; i < llct; i++)
		status |= swapoff_by_label(llist[i], !QUIET);

	for (i = 0; i < ulct; i++)
		status |= swapoff_by_uuid(ulist[i], !QUIET);

	while (*argv != NULL)
		status |= do_swapoff(*argv++, !QUIET);

	if (all) {
		/*
		 * In case /proc/swaps exists, unswap stuff listed there.
		 * We are quiet but report errors in status.
		 * Errors might mean that /proc/swaps
		 * exists as ordinary file, not in procfs.
		 * do_swapoff() exits immediately on EPERM.
		 */
		read_proc_swaps();
		for(i=0; i<numSwaps; i++)
			status |= do_swapoff(swapFiles[i], QUIET);

		/*
		 * Unswap stuff mentioned in /etc/fstab.
		 * Probably it was unmounted already, so errors are not bad.
		 * Doing swapoff -a twice should not give error messages.
		 */
		fp = setmntent(_PATH_FSTAB, "r");
		if (fp == NULL) {
			int errsv = errno;
			fprintf(stderr, _("%s: cannot open %s: %s\n"),
				progname, _PATH_FSTAB, strerror(errsv));
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

	progname = argv[0];
	p = strrchr(progname, '/');
	if (p)
		progname = p+1;

	if (streq(progname, "swapon"))
		return main_swapon(argc, argv);
	else
		return main_swapoff(argc, argv);
}
