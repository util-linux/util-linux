/*
 * A swapon(8)/swapoff(8) for Linux 0.99.
 */
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <mntent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "xmalloc.h"
#include "swap_constants.h"
#include "nls.h"
#include "fsprobe.h"
#include "realpath.h"
#include "pathnames.h"

#ifdef HAVE_SYS_SWAP_H
# include <sys/swap.h>
#endif

#ifndef SWAPON_HAS_TWO_ARGS
/* libc is insane, let's call the kernel */
# include <sys/syscall.h>
# define swapon(path, flags) syscall(SYS_swapon, path, flags)
# define swapoff(path) syscall(SYS_swapoff, path)
#endif

#define streq(s, t)	(strcmp ((s), (t)) == 0)

#define QUIET	1
#define CANONIC	1

int all = 0;
int priority = -1;	/* non-prioritized swap by default */
int verbose;
char *progname;

/* If true, don't complain if the device/file doesn't exist */
int ifexists = 0;

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

static int cannot_find(const char *special);

#define PRINT_USAGE_SPECIAL(_fp) \
		fprintf(_fp, _( \
	"The <special> parameter:\n" \
	" {-L label | LABEL=label}             LABEL of device to be used\n" \
	" {-U uuid  | UUID=uuid}               UUID of device to be used\n" \
	" <device>                             name of device to be used\n" \
	" <file>                               name of file to be used\n\n"))

static void
swapon_usage(FILE *fp, int n) {
	fprintf(fp, _("\nUsage:\n"
	" %1$s -a [-e] [-v]                  enable all swaps from /etc/fstab\n"
	" %1$s [-p priority] [-v] <special>  enable given swap\n"
	" %1$s -s                            display swap usage summary\n"
	" %1$s -h                            display help\n"
	" %1$s -V                            display version\n\n"), progname);

	PRINT_USAGE_SPECIAL(fp);

	exit(n);
}

static void
swapoff_usage(FILE *fp, int n) {
	fprintf(fp, _("\nUsage:\n"
	" %1$s -a [-v]                      disable all swaps\n"
	" %1$s [-v] <special>               disable given swap\n"
	" %1$s -h                           display help\n"
	" %1$s -V                           display version\n\n"), progname);

	PRINT_USAGE_SPECIAL(fp);

	exit(n);
}

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

	swaps = fopen(_PATH_PROC_SWAPS, "r");
	if (swaps == NULL)
		return;		/* nothing wrong */

	/* skip the first line */
	if (!fgets(line, sizeof(line), swaps)) {
		fprintf (stderr, _("%s: %s: unexpected file format\n"),
			progname, _PATH_PROC_SWAPS);
		fclose(swaps);
		return;
	}
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
	char canonical[PATH_MAX + 2];

	if (!myrealpath(fname, canonical, PATH_MAX + 1)) {
		fprintf(stderr, _("%s: cannot canonicalize %s: %s\n"),
			progname, fname, strerror(errno));
		strncpy(canonical, fname, PATH_MAX + 1);
		*(canonical + (PATH_MAX + 1)) = '\0';
	}

	for (i = 0; i < numSwaps; i++)
		if (swapFiles[i] && !strcmp(canonical, swapFiles[i]))
			return 1;
	return 0;
}

static int
display_summary(void)
{
       FILE *swaps;
       char line[1024] ;

       if ((swaps = fopen(_PATH_PROC_SWAPS, "r")) == NULL) {
               int errsv = errno;
               fprintf(stderr, "%s: %s: %s\n", progname, _PATH_PROC_SWAPS,
			strerror(errsv));
               return -1;
       }

       while (fgets(line, sizeof(line), swaps))
               printf("%s", line);

       fclose(swaps);
       return 0 ;
}

static int
do_swapon(const char *orig_special, int prio, int canonic) {
	int status;
	struct stat st;
	const char *special = orig_special;

	if (verbose)
		printf(_("%s on %s\n"), progname, orig_special);

	if (!canonic) {
		special = fsprobe_get_devname(orig_special);
		if (!special)
			return cannot_find(orig_special);
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
	const char *special = fsprobe_get_devname_by_label(label);
	return special ? do_swapon(special, prio, CANONIC) : cannot_find(label);
}

static int
swapon_by_uuid(const char *uuid, int prio) {
	const char *special = fsprobe_get_devname_by_uuid(uuid);
	return special ? do_swapon(special, prio, CANONIC) : cannot_find(uuid);
}

static int
do_swapoff(const char *orig_special, int quiet, int canonic) {
        const char *special = orig_special;

	if (verbose)
		printf(_("%s on %s\n"), progname, orig_special);

	if (!canonic) {
		special = fsprobe_get_devname(orig_special);
		if (!special)
			return cannot_find(orig_special);
	}

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
	const char *special = fsprobe_get_devname_by_label(label);
	return special ? do_swapoff(special, quiet, CANONIC) : cannot_find(label);
}

static int
swapoff_by_uuid(const char *uuid, int quiet) {
	const char *special = fsprobe_get_devname_by_uuid(uuid);
	return special ? do_swapoff(special, quiet, CANONIC) : cannot_find(uuid);
}

static int
swapon_all(void) {
	FILE *fp;
	struct mntent *fstab;
	int status = 0;

	read_proc_swaps();

	fp = setmntent(_PATH_MNTTAB, "r");
	if (fp == NULL) {
		int errsv = errno;
		fprintf(stderr, _("%s: cannot open %s: %s\n"),
			progname, _PATH_MNTTAB, strerror(errsv));
		exit(2);
	}

	while ((fstab = getmntent(fp)) != NULL) {
		const char *special;
		int skip = 0;
		int pri = priority;

		if (!streq(fstab->mnt_type, MNTTYPE_SWAP))
			continue;

		special = fsprobe_get_devname(fstab->mnt_fsname);
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
				status |= do_swapon(special, pri, CANONIC);
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
		status |= do_swapon(*argv++, priority, !CANONIC);

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
		status |= do_swapoff(*argv++, !QUIET, !CANONIC);

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
			status |= do_swapoff(swapFiles[i], QUIET, CANONIC);

		/*
		 * Unswap stuff mentioned in /etc/fstab.
		 * Probably it was unmounted already, so errors are not bad.
		 * Doing swapoff -a twice should not give error messages.
		 */
		fp = setmntent(_PATH_MNTTAB, "r");
		if (fp == NULL) {
			int errsv = errno;
			fprintf(stderr, _("%s: cannot open %s: %s\n"),
				progname, _PATH_MNTTAB, strerror(errsv));
			exit(2);
		}
		while ((fstab = getmntent(fp)) != NULL) {
			const char *special;

			if (!streq(fstab->mnt_type, MNTTYPE_SWAP))
				continue;

			special = fsprobe_get_devname(fstab->mnt_fsname);
			if (!special)
				continue;

			if (!is_in_proc_swaps(special))
				do_swapoff(special, QUIET, CANONIC);
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
