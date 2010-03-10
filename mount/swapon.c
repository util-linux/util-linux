/*
 * A swapon(8)/swapoff(8) for Linux 0.99.
 */
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <mntent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdint.h>
#include <err.h>

#include "bitops.h"
#include "blkdev.h"
#include "swap_constants.h"
#include "nls.h"
#include "fsprobe.h"
#include "pathnames.h"
#include "swapheader.h"

#define PATH_MKSWAP	"/sbin/mkswap"

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

#define MAX_PAGESIZE	(64 * 1024)

enum {
	SIG_SWAPSPACE = 1,
	SIG_SWSUSPEND
};

#define SWAP_SIGNATURE		"SWAPSPACE2"
#define SWAP_SIGNATURE_SZ	(sizeof(SWAP_SIGNATURE) - 1)

int all;
int priority = -1;	/* non-prioritized swap by default */

/* If true, don't complain if the device/file doesn't exist */
int ifexists;
int fixpgsz;

int verbose;
char *progname;

static struct option longswaponopts[] = {
		/* swapon only */
	{ "priority", required_argument, 0, 'p' },
	{ "ifexists", 0, 0, 'e' },
	{ "summary", 0, 0, 's' },
	{ "fixpgsz", 0, 0, 'f' },
		/* also for swapoff */
	{ "all", 0, 0, 'a' },
	{ "help", 0, 0, 'h' },
	{ "verbose", 0, 0, 'v' },
	{ "version", 0, 0, 'V' },
	{ NULL, 0, 0, 0 }
};

static struct option *longswapoffopts = &longswaponopts[4];

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
	" %1$s -a [-e] [-v] [-f]             enable all swaps from /etc/fstab\n"
	" %1$s [-p priority] [-v] [-f] <special>  enable given swap\n"
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

#define DELETED_SUFFIX		"\\040(deleted)"
#define DELETED_SUFFIX_SZ	(sizeof(DELETED_SUFFIX) - 1)

static void
read_proc_swaps(void) {
	FILE *swaps;
	char line[1024];
	char *p, **q;
	size_t sz;

	numSwaps = 0;
	swapFiles = NULL;

	swaps = fopen(_PATH_PROC_SWAPS, "r");
	if (swaps == NULL)
		return;		/* nothing wrong */

	/* skip the first line */
	if (!fgets(line, sizeof(line), swaps)) {
		/* do not whine about an empty file */
		if (ferror(swaps))
			warn(_("%s: unexpected file format"), _PATH_PROC_SWAPS);
		fclose(swaps);
		return;
	}
	/* make sure the first line is the header */
	if (line[0] != '\0' && strncmp(line, "Filename\t", 9))
		goto valid_first_line;

	while (fgets(line, sizeof(line), swaps)) {
 valid_first_line:
		/*
		 * Cut the line "swap_device  ... more info" after device.
		 * This will fail with names with embedded spaces.
		 */
		for (p = line; *p && *p != ' '; p++);
		*p = '\0';

		/* the kernel can use " (deleted)" suffix for paths
		 * in /proc/swaps, we have to remove this junk.
		 */
		sz = strlen(line);
		if (sz > DELETED_SUFFIX_SZ) {
		       p = line + (sz - DELETED_SUFFIX_SZ);
		       if (strcmp(p, DELETED_SUFFIX) == 0)
			       *p = '\0';
		}

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

       if ((swaps = fopen(_PATH_PROC_SWAPS, "r")) == NULL) {
               warn(_("%s: open failed"), _PATH_PROC_SWAPS);
               return -1;
       }

       while (fgets(line, sizeof(line), swaps))
               printf("%s", line);

       fclose(swaps);
       return 0 ;
}

/* calls mkswap */
static int
swap_reinitialize(const char *device) {
	const char *label = fsprobe_get_label_by_devname(device);
	const char *uuid  = fsprobe_get_uuid_by_devname(device);
	pid_t pid;
	int status, ret;
	char *cmd[7];
	int idx=0;

	warnx(_("%s: reinitializing the swap."), device);

	switch((pid=fork())) {
	case -1: /* fork error */
		warn(_("fork failed"));
		return -1;

	case 0:	/* child */
		cmd[idx++] = PATH_MKSWAP;
		if (label && *label) {
			cmd[idx++] = "-L";
			cmd[idx++] = (char *) label;
		}
		if (uuid && *uuid) {
			cmd[idx++] = "-U";
			cmd[idx++] = (char *) uuid;
		}
		cmd[idx++] = (char *) device;
		cmd[idx++] = NULL;
		execv(cmd[0], cmd);
		err(EXIT_FAILURE, _("execv failed"));

	default: /* parent */
		do {
			if ((ret = waitpid(pid, &status, 0)) < 0
					&& errno == EINTR)
				continue;
			else if (ret < 0) {
				warn(_("waitpid failed"));
				return -1;
			}
		} while (0);

		/* mkswap returns: 0=suss, 1=error */
		if (WIFEXITED(status) && WEXITSTATUS(status)==0)
			return 0; /* ok */
	}
	return -1; /* error */
}

static int
swap_rewrite_signature(const char *devname, unsigned int pagesize)
{
	int fd, rc = -1;

	fd = open(devname, O_WRONLY);
	if (fd == -1) {
		warn(_("%s: open failed"), devname);
		return -1;
	}

	if (lseek(fd, pagesize - SWAP_SIGNATURE_SZ, SEEK_SET) < 0) {
		warn(_("%s: lseek failed"), devname);
		goto err;
	}

	if (write(fd, (void *) SWAP_SIGNATURE,
			SWAP_SIGNATURE_SZ) != SWAP_SIGNATURE_SZ) {
		warn(_("%s: write signature failed"), devname);
		goto err;
	}

	rc  = 0;
err:
	close(fd);
	return rc;
}

static int
swap_detect_signature(const char *buf, int *sig)
{
	if (memcmp(buf, "SWAP-SPACE", 10) == 0 ||
            memcmp(buf, "SWAPSPACE2", 10) == 0)
		*sig = SIG_SWAPSPACE;

	else if (memcmp(buf, "S1SUSPEND", 9) == 0 ||
		 memcmp(buf, "S2SUSPEND", 9) == 0 ||
		 memcmp(buf, "ULSUSPEND", 9) == 0 ||
		 memcmp(buf, "\xed\xc3\x02\xe9\x98\x56\xe5\x0c", 8) == 0)
		*sig = SIG_SWSUSPEND;
	else
		return 0;

	return 1;
}

static char *
swap_get_header(int fd, int *sig, unsigned int *pagesize)
{
	char *buf;
	ssize_t datasz;
	unsigned int page;

	*pagesize = 0;
	*sig = 0;

	buf = malloc(MAX_PAGESIZE);
	if (!buf)
		return NULL;

	datasz = read(fd, buf, MAX_PAGESIZE);
	if (datasz == (ssize_t) -1)
		goto err;

	for (page = 0x1000; page <= MAX_PAGESIZE; page <<= 1) {
		/* skip 32k pagesize since this does not seem to
		 * be supported */
		if (page == 0x8000)
			continue;
		/* the smallest swap area is PAGE_SIZE*10, it means
		 * 40k, that's less than MAX_PAGESIZE */
		if (datasz < (page - SWAP_SIGNATURE_SZ))
			break;
		if (swap_detect_signature(buf + page - SWAP_SIGNATURE_SZ, sig)) {
			*pagesize = page;
			break;
		}
	}

	if (*pagesize)
		return buf;
err:
	free(buf);
	return NULL;
}

/* returns real size of swap space */
unsigned long long
swap_get_size(const char *hdr, const char *devname, unsigned int pagesize)
{
	unsigned int last_page = 0;
	int swap_version = 0;
	int flip = 0;
	struct swap_header_v1_2 *s;

	s = (struct swap_header_v1_2 *) hdr;
	if (s->version == 1) {
		swap_version = 1;
		last_page = s->last_page;
	} else if (swab32(s->version) == 1) {
		flip = 1;
		swap_version = 1;
		last_page = swab32(s->last_page);
	}
	if (verbose)
		warnx(_("%s: found swap signature: version %d, "
			"page-size %d, %s byte order"),
			devname,
			swap_version,
			pagesize / 1024,
			flip ? _("different") : _("same"));

	return ((unsigned long long) last_page + 1) * pagesize;
}

static int
swapon_checks(const char *special)
{
	struct stat st;
	int fd = -1, sig;
	char *hdr = NULL;
	unsigned int pagesize;
	unsigned long long devsize = 0;

	if (stat(special, &st) < 0) {
		warn(_("%s: stat failed"), special);
		goto err;
	}

	/* people generally dislike this warning - now it is printed
	   only when `verbose' is set */
	if (verbose) {
		int permMask = (S_ISBLK(st.st_mode) ? 07007 : 07077);

		if ((st.st_mode & permMask) != 0)
			warnx(_("%s: insecure permissions %04o, %04o suggested."),
				special, st.st_mode & 07777,
				~permMask & 0666);
	}

	/* test for holes by LBT */
	if (S_ISREG(st.st_mode)) {
		if (st.st_blocks * 512 < st.st_size) {
			warnx(_("%s: skipping - it appears to have holes."),
				special);
			goto err;
		}
		devsize = st.st_size;
	}

	fd = open(special, O_RDONLY);
	if (fd == -1) {
		warn(_("%s: open failed"), special);
		goto err;
	}

	if (S_ISBLK(st.st_mode) && blkdev_get_size(fd, &devsize)) {
		warn(_("%s: get size failed"), special);
		goto err;
	}

	hdr = swap_get_header(fd, &sig, &pagesize);
	if (!hdr) {
		warn(_("%s: read swap header failed"), special);
		goto err;
	}

	if (sig == SIG_SWAPSPACE && pagesize) {
		unsigned long long swapsize =
				swap_get_size(hdr, special, pagesize);
		if (verbose)
			warnx("%s: pagesize=%d, swapsize=%llu, devsize=%llu",
				special, pagesize, swapsize, devsize);

		if (swapsize > devsize) {
			if (verbose)
				warnx(_("%s: last_page 0x%08llx is larger"
					" than actual size of swapspace"),
					special, swapsize);
		} else if (getpagesize() != pagesize) {
			if (fixpgsz) {
				warnx(_("%s: swap format pagesize does not match."),
					special);
				if (swap_reinitialize(special) < 0)
					goto err;
			} else
				warnx(_("%s: swap format pagesize does not match. "
					"(Use --fixpgsz to reinitialize it.)"),
					special);
		}
	} else if (sig == SIG_SWSUSPEND) {
		/* We have to reinitialize swap with old (=useless) software suspend
		 * data. The problem is that if we don't do it, then we get data
		 * corruption the next time an attempt at unsuspending is made.
		 */
		warnx(_("%s: software suspend data detected. "
				"Rewriting the swap signature."),
			special);
		if (swap_rewrite_signature(special, pagesize) < 0)
			goto err;
	}

	free(hdr);
	close(fd);
	return 0;
err:
	if (fd != -1)
		close(fd);
	free(hdr);
	return -1;
}

static int
do_swapon(const char *orig_special, int prio, int canonic) {
	int status;
	const char *special = orig_special;
	int flags = 0;

	if (verbose)
		printf(_("%s on %s\n"), progname, orig_special);

	if (!canonic) {
		special = fsprobe_get_devname_by_spec(orig_special);
		if (!special)
			return cannot_find(orig_special);
	}

	if (swapon_checks(special))
		return -1;

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
	if (status < 0)
		warn(_("%s: swapon failed"), orig_special);

	return status;
}

static int
cannot_find(const char *special) {
	warnx(_("cannot find the device for %s"), special);
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
		special = fsprobe_get_devname_by_spec(orig_special);
		if (!special)
			return cannot_find(orig_special);
	}

	if (swapoff(special) == 0)
		return 0;	/* success */

	if (errno == EPERM)
		errx(EXIT_FAILURE, _("Not superuser."));

	if (!quiet || errno == ENOMEM)
		warn(_("%s: swapoff failed"), orig_special);

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
	if (fp == NULL)
		err(2, _("%s: open failed"), _PATH_MNTTAB);

	while ((fstab = getmntent(fp)) != NULL) {
		const char *special;
		int skip = 0;
		int pri = priority;
		char *opt, *opts;

		if (!streq(fstab->mnt_type, MNTTYPE_SWAP))
			continue;

		opts = strdup(fstab->mnt_opts);

		for (opt = strtok(opts, ","); opt != NULL;
		     opt = strtok(NULL, ",")) {
			if (strncmp(opt, "pri=", 4) == 0)
				pri = atoi(opt+4);
			if (strcmp(opt, "noauto") == 0)
				skip = 1;
		}
		free(opts);

		if (skip)
			continue;

		special = fsprobe_get_devname_by_spec(fstab->mnt_fsname);
		if (!special) {
			if (!ifexists)
				status |= cannot_find(fstab->mnt_fsname);
			continue;
		}

		if (!is_in_proc_swaps(special) &&
		    (!ifexists || !access(special, R_OK)))
			status |= do_swapon(special, pri, CANONIC);

		free((void *) special);
	}
	fclose(fp);

	return status;
}

static const char **llist = NULL;
static int llct = 0;
static const char **ulist = NULL;
static int ulct = 0;

static void addl(const char *label) {
	llist = (const char **) realloc(llist, (++llct) * sizeof(char *));
	if (!llist)
		exit(EXIT_FAILURE);
	llist[llct-1] = label;
}

static void addu(const char *uuid) {
	ulist = (const char **) realloc(ulist, (++ulct) * sizeof(char *));
	if (!ulist)
		exit(EXIT_FAILURE);
	ulist[ulct-1] = uuid;
}

static int
main_swapon(int argc, char *argv[]) {
	int status = 0;
	int c, i;

	while ((c = getopt_long(argc, argv, "ahefp:svVL:U:",
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
		case 'f':
			fixpgsz = 1;
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
		if (fp == NULL)
			err(2, _("%s: open failed"), _PATH_MNTTAB);

		while ((fstab = getmntent(fp)) != NULL) {
			const char *special;

			if (!streq(fstab->mnt_type, MNTTYPE_SWAP))
				continue;

			special = fsprobe_get_devname_by_spec(fstab->mnt_fsname);
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

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	progname = program_invocation_short_name;
	if (!progname) {
		char *p = strrchr(argv[0], '/');
		progname = p ? p+1 : argv[0];
	}

	if (streq(progname, "swapon"))
		return main_swapon(argc, argv);
	else if (streq(progname, "swapoff"))
		return main_swapoff(argc, argv);

	errx(EXIT_FAILURE, _("'%s' is unsupported program name "
			"(must be 'swapon' or 'swapoff')."), progname);
}
