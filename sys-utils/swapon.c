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
#include <ctype.h>

#include <blkid.h>
#include <libmount.h>

#include "bitops.h"
#include "blkdev.h"
#include "nls.h"
#include "pathnames.h"
#include "swapheader.h"
#include "mangle.h"
#include "canonicalize.h"
#include "xalloc.h"
#include "c.h"
#include "closestream.h"

#define PATH_MKSWAP	"/sbin/mkswap"

#ifdef HAVE_SYS_SWAP_H
# include <sys/swap.h>
#endif

#ifndef SWAP_FLAG_DISCARD
# define SWAP_FLAG_DISCARD	0x10000 /* discard swap cluster after use */
#endif

#ifndef SWAP_FLAG_PREFER
# define SWAP_FLAG_PREFER	0x8000	/* set if swap priority specified */
#endif

#ifndef SWAP_FLAG_PRIO_MASK
# define SWAP_FLAG_PRIO_MASK	0x7fff
#endif

#ifndef SWAP_FLAG_PRIO_SHIFT
# define SWAP_FLAG_PRIO_SHIFT	0
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

static int all;
static int priority = -1;	/* non-prioritized swap by default */
static int discard;

/* If true, don't complain if the device/file doesn't exist */
static int ifexists;
static int fixpgsz;

static int verbose;
static char *progname;

static const struct option longswaponopts[] = {
		/* swapon only */
	{ "priority", required_argument, 0, 'p' },
	{ "discard", 0, 0, 'd' },
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

static const struct option *longswapoffopts = &longswaponopts[4];

static int cannot_find(const char *special);

#define PRINT_USAGE_SPECIAL(_fp) \
	fputs(_("\nThe <spec> parameter:\n" \
		" -L <label>             LABEL of device to be used\n" \
		" -U <uuid>              UUID of device to be used\n" \
		" LABEL=<label>          LABEL of device to be used\n" \
		" UUID=<uuid>            UUID of device to be used\n" \
		" <device>               name of device to be used\n" \
		" <file>                 name of file to be used\n\n"), _fp)

static void
swapon_usage(FILE *out, int n) {
	fputs(_("\nUsage:\n"), out);
	fprintf(out, _(" %s [options] [<spec>]\n"), progname);

	fputs(_("\nOptions:\n"), out);
	fputs(_(" -a, --all              enable all swaps from /etc/fstab\n"
		" -d, --discard          discard freed pages before they are reused\n"
		" -e, --ifexists         silently skip devices that do not exis\n"
		" -f, --fixpgsz          reinitialize the swap space if necessary\n"
		" -h, --help             display help and exit\n"
		" -p, --priority <prio>  specify the priority of the swap device\n"
		" -s, --summary          display summary about used swap devices and exit\n"
		" -v, --verbose          verbose mode\n"
		" -V, --version          display version and exit\n"), out);

	PRINT_USAGE_SPECIAL(out);

	exit(n);
}

static void
swapoff_usage(FILE *out, int n) {
	fputs(_("\nUsage:\n"), out);
	fprintf(out, _(" %s [options] [<spec>]\n"), progname);

	fputs(_("\nOptions:\n"), out);
	fputs(_(" -a, --all              disable all swaps from /proc/swaps\n"
		" -h, --help             display help and exit\n"
		" -v, --verbose          verbose mode\n"
		" -V, --version          display version and exit\n"), out);

	PRINT_USAGE_SPECIAL(out);

	exit(n);
}

/*
 * contents of /proc/swaps
 */
static struct libmnt_table *swaps;
static struct libmnt_cache *mntcache;

static struct libmnt_table *get_swaps_table(void)
{
	if (!swaps) {
		swaps = mnt_new_table();
		if (!swaps)
			return NULL;
		if (!mntcache)
			mntcache = mnt_new_cache();

		mnt_table_set_cache(swaps, mntcache);
		if (mnt_table_parse_swaps(swaps, NULL) != 0)
			return NULL;
	}

	return swaps;
}

static int is_active_swap(const char *filename)
{
	struct libmnt_table *st = get_swaps_table();
	return st && mnt_table_find_srcpath(st, filename, MNT_ITER_BACKWARD);
}

static int
display_summary(void)
{
	struct libmnt_table *st = get_swaps_table();
	struct libmnt_iter *itr;
	struct libmnt_fs *fs;

	if (!st)
		return -1;

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr)
		err(EXIT_FAILURE, _("failed to initialize libmount iterator"));

	if (mnt_table_get_nents(st) > 0)
		printf(_("%-39s\tType\tSize\tUsed\tPriority\n"), _("Filename"));

	while (mnt_table_next_fs(st, itr, &fs) == 0) {
		printf("%-39s\t%s\t%jd\t%jd\t%d\n",
			mnt_fs_get_source(fs),
			mnt_fs_get_swaptype(fs),
			mnt_fs_get_size(fs),
			mnt_fs_get_usedsize(fs),
			mnt_fs_get_priority(fs));
	}

	mnt_free_iter(itr);
	return 0;
}

/* calls mkswap */
static int
swap_reinitialize(const char *device, const char *label, const char *uuid)
{
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
		break;
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
		 memcmp(buf, "\xed\xc3\x02\xe9\x98\x56\xe5\x0c", 8) == 0 ||
		 memcmp(buf, "LINHIB0001", 10) == 0)
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

	buf = xmalloc(MAX_PAGESIZE);

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
		if (datasz < 0 || (size_t) datasz < (page - SWAP_SIGNATURE_SZ))
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

void
swap_get_info(const char *hdr, char **label, char **uuid)
{
	struct swap_header_v1_2 *s = (struct swap_header_v1_2 *) hdr;

	if (s && *s->volume_name && label)
		*label = xstrdup(s->volume_name);

	if (s && *s->uuid && uuid) {
		const unsigned char *u = s->uuid;
		char str[37];

		snprintf(str, sizeof(str),
			"%02x%02x%02x%02x-"
			"%02x%02x-%02x%02x-"
			"%02x%02x-%02x%02x%02x%02x%02x%02x",
			u[0], u[1], u[2], u[3],
			u[4], u[5], u[6], u[7],
			u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
		*uuid = xstrdup(str);
	}
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

		if (S_ISREG(st.st_mode) && st.st_uid != 0)
			warnx(_("%s: insecure file owner %d, 0 (root) suggested."),
				special, st.st_uid);
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
		int syspg = getpagesize();

		if (verbose)
			warnx(_("%s: pagesize=%d, swapsize=%llu, devsize=%llu"),
				special, pagesize, swapsize, devsize);

		if (swapsize > devsize) {
			if (verbose)
				warnx(_("%s: last_page 0x%08llx is larger"
					" than actual size of swapspace"),
					special, swapsize);
		} else if (syspg < 0 || (unsigned) syspg != pagesize) {
			if (fixpgsz) {
				char *label = NULL, *uuid = NULL;
				int rc;

				swap_get_info(hdr, &label, &uuid);

				warnx(_("%s: swap format pagesize does not match."),
					special);
				rc = swap_reinitialize(special, label, uuid);
				free(label);
				free(uuid);
				if (rc < 0)
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
do_swapon(const char *orig_special, int prio, int fl_discard, int canonic) {
	int status;
	const char *special = orig_special;
	int flags = 0;

	if (verbose)
		printf(_("%s on %s\n"), progname, orig_special);

	if (!canonic) {
		special = blkid_evaluate_spec(orig_special, NULL);
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
	if (fl_discard)
		flags |= SWAP_FLAG_DISCARD;

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
swapon_by_label(const char *label, int prio, int dsc) {
	const char *special = blkid_evaluate_tag("LABEL", label, NULL);
	return special ? do_swapon(special, prio, dsc, CANONIC) :
			 cannot_find(label);
}

static int
swapon_by_uuid(const char *uuid, int prio, int dsc) {
	const char *special = blkid_evaluate_tag("UUID", uuid, NULL);
	return special ? do_swapon(special, prio, dsc, CANONIC) :
			 cannot_find(uuid);
}

static int
do_swapoff(const char *orig_special, int quiet, int canonic) {
        const char *special = orig_special;

	if (verbose)
		printf(_("%s on %s\n"), progname, orig_special);

	if (!canonic) {
		special = blkid_evaluate_spec(orig_special, NULL);
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
	const char *special = blkid_evaluate_tag("LABEL", label, NULL);
	return special ? do_swapoff(special, quiet, CANONIC) : cannot_find(label);
}

static int
swapoff_by_uuid(const char *uuid, int quiet) {
	const char *special = blkid_evaluate_tag("UUID", uuid, NULL);
	return special ? do_swapoff(special, quiet, CANONIC) : cannot_find(uuid);
}

static int
swapon_all(void) {
	FILE *fp;
	struct mntent *fstab;
	int status = 0;

	fp = setmntent(_PATH_MNTTAB, "r");
	if (fp == NULL)
		err(2, _("%s: open failed"), _PATH_MNTTAB);

	while ((fstab = getmntent(fp)) != NULL) {
		const char *special;
		int skip = 0, nofail = ifexists;
		int pri = priority, dsc = discard;
		char *opt, *opts;

		if (!streq(fstab->mnt_type, MNTTYPE_SWAP))
			continue;

		opts = xstrdup(fstab->mnt_opts);

		for (opt = strtok(opts, ","); opt != NULL;
		     opt = strtok(NULL, ",")) {
			if (strncmp(opt, "pri=", 4) == 0)
				pri = atoi(opt+4);
			if (strcmp(opt, "discard") == 0)
				dsc = 1;
			if (strcmp(opt, "noauto") == 0)
				skip = 1;
			if (strcmp(opt, "nofail") == 0)
				nofail = 1;
		}
		free(opts);

		if (skip)
			continue;

		special = blkid_evaluate_spec(fstab->mnt_fsname, NULL);
		if (!special) {
			if (!nofail)
				status |= cannot_find(fstab->mnt_fsname);
			continue;
		}

		if (!is_active_swap(special) &&
		    (!nofail || !access(special, R_OK)))
			status |= do_swapon(special, pri, dsc, CANONIC);

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

	while ((c = getopt_long(argc, argv, "ahdefp:svVL:U:",
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
		case 'd':
			discard = 1;
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
			printf(_("%s (%s)\n"), progname, PACKAGE_STRING);
			exit(EXIT_SUCCESS);
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
		status |= swapon_by_label(llist[i], priority, discard);

	for (i = 0; i < ulct; i++)
		status |= swapon_by_uuid(ulist[i], priority, discard);

	while (*argv != NULL)
		status |= do_swapon(*argv++, priority, discard, !CANONIC);

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
			printf(_("%s (%s)\n"), progname, PACKAGE_STRING);
			exit(EXIT_SUCCESS);
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
		struct libmnt_table *st = get_swaps_table();

		if (st && mnt_table_get_nents(st) > 0) {
			struct libmnt_iter *itr = mnt_new_iter(MNT_ITER_BACKWARD);
			struct libmnt_fs *fs;

			while (itr && mnt_table_next_fs(st, itr, &fs) == 0)
				status |= do_swapoff(mnt_fs_get_source(fs),
						     QUIET, CANONIC);

			mnt_free_iter(itr);
		}

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

			special = blkid_evaluate_spec(fstab->mnt_fsname, NULL);
			if (!special)
				continue;

			if (!is_active_swap(special))
				do_swapoff(special, QUIET, CANONIC);
		}
		fclose(fp);
	}

	return status;
}

int
main(int argc, char *argv[]) {

	int status;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	progname = program_invocation_short_name;
	if (!progname) {
		char *p = strrchr(argv[0], '/');
		progname = p ? p+1 : argv[0];
	}

	mnt_init_debug(0);

	if (streq(progname, "swapon"))
		status = main_swapon(argc, argv);
	else if (streq(progname, "swapoff"))
		status = main_swapoff(argc, argv);
	else
		errx(EXIT_FAILURE, _("'%s' is unsupported program name "
			"(must be 'swapon' or 'swapoff')."), progname);

	mnt_free_table(swaps);
	mnt_free_cache(mntcache);
	return status;
}
