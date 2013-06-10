#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdint.h>
#include <ctype.h>

#include <libmount.h>

#include "c.h"
#include "nls.h"
#include "bitops.h"
#include "blkdev.h"
#include "pathnames.h"
#include "xalloc.h"
#include "closestream.h"

#include "swapheader.h"
#include "swapon-common.h"
#include "strutils.h"
#include "tt.h"

#define PATH_MKSWAP	"/sbin/mkswap"

#ifdef HAVE_SYS_SWAP_H
# include <sys/swap.h>
#endif

#ifndef SWAP_FLAG_DISCARD
# define SWAP_FLAG_DISCARD	0x10000 /* enable discard for swap */
#endif

#ifndef SWAP_FLAG_DISCARD_ONCE
# define SWAP_FLAG_DISCARD_ONCE 0x20000 /* discard swap area at swapon-time */
#endif

#ifndef SWAP_FLAG_DISCARD_PAGES
# define SWAP_FLAG_DISCARD_PAGES 0x40000 /* discard page-clusters after use */
#endif

#define SWAP_FLAGS_DISCARD_VALID (SWAP_FLAG_DISCARD | SWAP_FLAG_DISCARD_ONCE | \
				  SWAP_FLAG_DISCARD_PAGES)

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
#endif

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
static int discard;		/* don't send swap discards by default */

/* If true, don't complain if the device/file doesn't exist */
static int ifexists;
static int fixpgsz;
static int verbose;

/* column names */
struct colinfo {
        const char *name; /* header */
        double     whint; /* width hint (N < 1 is in percent of termwidth) */
        int        flags; /* TT_FL_* */
        const char *help;
};
enum { COL_PATH, COL_TYPE, COL_SIZE, COL_USED, COL_PRIO };
struct colinfo infos[] = {
	[COL_PATH]     = { "NAME",	0.20, 0, N_("device file or partition path") },
	[COL_TYPE]     = { "TYPE",	0.20, TT_FL_TRUNC, N_("type of the device")},
	[COL_SIZE]     = { "SIZE",	0.20, TT_FL_RIGHT, N_("size of the swap area")},
	[COL_USED]     = { "USED",	0.20, TT_FL_RIGHT, N_("bytes in use")},
	[COL_PRIO]     = { "PRIO",	0.20, TT_FL_RIGHT, N_("swap priority")},
};
#define NCOLS ARRAY_SIZE(infos)
static int columns[NCOLS], ncolumns;

static int column_name_to_id(const char *name, size_t namesz)
{
	size_t i;

	assert(name);

	for (i = 0; i < NCOLS; i++) {
		const char *cn = infos[i].name;

		if (!strncasecmp(name, cn, namesz) && !*(cn + namesz))
			return i;
	}
	warnx(_("unknown column: %s"), name);
	return -1;
}

static inline int get_column_id(int num)
{
	assert(ARRAY_SIZE(columns) == NCOLS);
	assert(num < ncolumns);
	assert(columns[num] < (int)NCOLS);

	return columns[num];
}

static inline struct colinfo *get_column_info(unsigned num)
{
	return &infos[get_column_id(num)];
}

static void add_tt_line(struct tt *tt, struct libmnt_fs *fs, int bytes)
{
	int i;
	struct tt_line *line;

	assert(tt);
	assert(fs);

	line = tt_add_line(tt, NULL);
	if (!line) {
		warn(_("failed to add line to output"));
		return;
	}

	for (i = 0; i < ncolumns; i++) {
		char *str = NULL;
		off_t size;

		switch (get_column_id(i)) {
		case COL_PATH:
			xasprintf(&str, "%s", mnt_fs_get_source(fs));
			break;
		case COL_TYPE:
			xasprintf(&str, "%s", mnt_fs_get_swaptype(fs));
			break;
		case COL_SIZE:
			size = mnt_fs_get_size(fs);
			size *= 1024;	/* convert to bytes */
			if (bytes)
				xasprintf(&str, "%jd", size);
			else
				str = size_to_human_string(SIZE_SUFFIX_1LETTER, size);
			break;
		case COL_USED:
			size = mnt_fs_get_usedsize(fs);
			size *= 1024;	/* convert to bytes */
			if (bytes)
				xasprintf(&str, "%jd", size);
			else
				str = size_to_human_string(SIZE_SUFFIX_1LETTER, size);
			break;
		case COL_PRIO:
			xasprintf(&str, "%d", mnt_fs_get_priority(fs));
			break;
		default:
			break;
		}

		if (str)
			tt_line_set_data(line, i, str);
	}
	return;
}

static int display_summary(void)
{
	struct libmnt_table *st = get_swaps();
	struct libmnt_iter *itr;
	struct libmnt_fs *fs;

	if (!st)
		return -1;

	if (mnt_table_is_empty(st))
		return 0;

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr)
		err(EXIT_FAILURE, _("failed to initialize libmount iterator"));

	printf(_("%s\t\t\t\tType\t\tSize\tUsed\tPriority\n"), _("Filename"));

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

static int show_table(int tt_flags, int bytes)
{
	struct libmnt_table *st = get_swaps();
	struct libmnt_iter *itr = NULL;
	struct libmnt_fs *fs;

	int i, rc = 0;
	struct tt *tt = NULL;

	if (!st)
		return -1;

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr)
		err(EXIT_FAILURE, _("failed to initialize libmount iterator"));

	tt = tt_new_table(tt_flags | TT_FL_FREEDATA);
	if (!tt) {
		warn(_("failed to initialize output table"));
		goto done;
	}

	for (i = 0; i < ncolumns; i++) {
		struct colinfo *col = get_column_info(i);

		if (!tt_define_column(tt, col->name, col->whint, col->flags)) {
			warnx(_("failed to initialize output column"));
			rc = -1;
			goto done;
		}
	}

	while (mnt_table_next_fs(st, itr, &fs) == 0)
		add_tt_line(tt, fs, bytes);

	tt_print_table(tt);
 done:
	mnt_free_iter(itr);
	tt_free_table(tt);
	return rc;
}

/* calls mkswap */
static int swap_reinitialize(const char *device,
			     const char *label, const char *uuid)
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
		err(EXIT_FAILURE, _("failed to execute %s"), cmd[0]);

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

static int swap_rewrite_signature(const char *devname, unsigned int pagesize)
{
	int fd, rc = -1;

	fd = open(devname, O_WRONLY);
	if (fd == -1) {
		warn(_("cannot open %s"), devname);
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
	if (close_fd(fd) != 0) {
		warn(_("write failed: %s"), devname);
		rc = -1;
	}
	return rc;
}

static int swap_detect_signature(const char *buf, int *sig)
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

static char *swap_get_header(int fd, int *sig, unsigned int *pagesize)
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
static unsigned long long swap_get_size(const char *hdr, const char *devname,
					unsigned int pagesize)
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

static void swap_get_info(const char *hdr, char **label, char **uuid)
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

static int swapon_checks(const char *special)
{
	struct stat st;
	int fd = -1, sig;
	char *hdr = NULL;
	unsigned int pagesize;
	unsigned long long devsize = 0;
	int permMask;

	if (stat(special, &st) < 0) {
		warn(_("stat failed %s"), special);
		goto err;
	}

	permMask = S_ISBLK(st.st_mode) ? 07007 : 07077;
	if ((st.st_mode & permMask) != 0)
		warnx(_("%s: insecure permissions %04o, %04o suggested."),
				special, st.st_mode & 07777,
				~permMask & 0666);

	if (S_ISREG(st.st_mode) && st.st_uid != 0)
		warnx(_("%s: insecure file owner %d, 0 (root) suggested."),
				special, st.st_uid);

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
		warn(_("cannot open %s"), special);
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

static int do_swapon(const char *orig_special, int prio,
		     int fl_discard, int canonic)
{
	int status;
	const char *special = orig_special;
	int flags = 0;

	if (verbose)
		printf(_("swapon %s\n"), orig_special);

	if (!canonic) {
		special = mnt_resolve_spec(orig_special, mntcache);
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
	/*
	 * Validate the discard flags passed and set them
	 * accordingly before calling sys_swapon.
	 */
	if (fl_discard && !(fl_discard & ~SWAP_FLAGS_DISCARD_VALID)) {
		/*
		 * If we get here with both discard policy flags set,
		 * we just need to tell the kernel to enable discards
		 * and it will do correctly, just as we expect.
		 */
		if ((fl_discard & SWAP_FLAG_DISCARD_ONCE) &&
		    (fl_discard & SWAP_FLAG_DISCARD_PAGES))
			flags |= SWAP_FLAG_DISCARD;
		else
			flags |= fl_discard;
	}

	status = swapon(special, flags);
	if (status < 0)
		warn(_("%s: swapon failed"), orig_special);

	return status;
}

static int swapon_by_label(const char *label, int prio, int dsc)
{
	const char *special = mnt_resolve_tag("LABEL", label, mntcache);
	return special ? do_swapon(special, prio, dsc, CANONIC) :
			 cannot_find(label);
}

static int swapon_by_uuid(const char *uuid, int prio, int dsc)
{
	const char *special = mnt_resolve_tag("UUID", uuid, mntcache);
	return special ? do_swapon(special, prio, dsc, CANONIC) :
			 cannot_find(uuid);
}

static int swapon_all(void)
{
	struct libmnt_table *tb = get_fstab();
	struct libmnt_iter *itr;
	struct libmnt_fs *fs;
	int status = 0;

	if (!tb)
		err(EXIT_FAILURE, _("failed to parse %s"), mnt_get_fstab_path());

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr)
		err(EXIT_FAILURE, _("failed to initialize libmount iterator"));

	while (mnt_table_find_next_fs(tb, itr, match_swap, NULL, &fs) == 0) {
		/* defaults */
		int pri = priority, dsc = discard, nofail = ifexists;
		char *p, *src, *dscarg;

		if (mnt_fs_get_option(fs, "noauto", NULL, NULL) == 0)
			continue;
		if (mnt_fs_get_option(fs, "discard", &dscarg, NULL) == 0) {
			dsc |= SWAP_FLAG_DISCARD;
			if (dscarg) {
				/* only single-time discards are wanted */
				if (strcmp(dscarg, "once") == 0)
					dsc |= SWAP_FLAG_DISCARD_ONCE;

				/* do discard for every released swap page */
				if (strcmp(dscarg, "pages") == 0)
					dsc |= SWAP_FLAG_DISCARD_PAGES;
			}
		}
		if (mnt_fs_get_option(fs, "nofail", NULL, NULL) == 0)
			nofail = 1;
		if (mnt_fs_get_option(fs, "pri", &p, NULL) == 0 && p)
			pri = atoi(p);

		src = mnt_resolve_spec(mnt_fs_get_source(fs), mntcache);
		if (!src) {
			if (!nofail)
				status |= cannot_find(mnt_fs_get_source(fs));
			continue;
		}

		if (!is_active_swap(src) &&
		    (!nofail || !access(src, R_OK)))
			status |= do_swapon(src, pri, dsc, CANONIC);
	}

	mnt_free_iter(itr);
	return status;
}

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	size_t i;
	fputs(USAGE_HEADER, out);

	fprintf(out, _(" %s [options] [<spec>]\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --all                enable all swaps from /etc/fstab\n"
		" -d, --discard[=<policy>] enable swap discards, if supported by device\n"
		" -e, --ifexists           silently skip devices that do not exist\n"
		" -f, --fixpgsz            reinitialize the swap space if necessary\n"
		" -p, --priority <prio>    specify the priority of the swap device\n"
		" -s, --summary            display summary about used swap devices\n"
		"     --show[=<columns>]   display summary in definable table\n"
		"     --noheadings         don't print headings, use with --show\n"
		"     --raw                use the raw output format, use with --show\n"
		"     --bytes              display swap size in bytes in --show output\n"
		" -v, --verbose            verbose mode\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fputs(_("\nThe <spec> parameter:\n" \
		" -L <label>             synonym for LABEL=<label>\n"
		" -U <uuid>              synonym for UUID=<uuid>\n"
		" LABEL=<label>          specifies device by swap area label\n"
		" UUID=<uuid>            specifies device by swap area UUID\n"
		" PARTLABEL=<label>      specifies device by partition label\n"
		" PARTUUID=<uuid>        specifies device by partition UUID\n"
		" <device>               name of device to be used\n"
		" <file>                 name of file to be used\n"), out);

	fputs(_("\nAvailable discard policy types (for --discard):\n"
		" once	  : only single-time area discards are issued. (swapon)\n"
		" pages	  : discard freed pages before they are reused.\n"
		" * if no policy is selected both discard types are enabled. (default)\n"), out);

	fputs(_("\nAvailable columns (for --show):\n"), out);
	for (i = 0; i < NCOLS; i++)
		fprintf(out, " %4s  %s\n", infos[i].name, _(infos[i].help));

	fprintf(out, USAGE_MAN_TAIL("swapon(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	int status = 0, c;
	int show = 0, tt_flags = 0;
	int bytes = 0;
	size_t i;

	enum {
		SHOW_OPTION = CHAR_MAX + 1,
		RAW_OPTION,
		NOHEADINGS_OPTION,
		BYTES_OPTION
	};

	static const struct option long_opts[] = {
		{ "priority", 1, 0, 'p' },
		{ "discard",  2, 0, 'd' },
		{ "ifexists", 0, 0, 'e' },
		{ "summary",  0, 0, 's' },
		{ "fixpgsz",  0, 0, 'f' },
		{ "all",      0, 0, 'a' },
		{ "help",     0, 0, 'h' },
		{ "verbose",  0, 0, 'v' },
		{ "version",  0, 0, 'V' },
		{ "show",     2, 0, SHOW_OPTION },
		{ "noheadings", 0, 0, NOHEADINGS_OPTION },
		{ "raw",      0, 0, RAW_OPTION },
		{ "bytes",    0, 0, BYTES_OPTION },
		{ NULL, 0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	mnt_init_debug(0);
	mntcache = mnt_new_cache();

	while ((c = getopt_long(argc, argv, "ahd::efp:svVL:U:",
				long_opts, NULL)) != -1) {
		switch (c) {
		case 'a':		/* all */
			++all;
			break;
		case 'h':		/* help */
			usage(stdout);
			break;
		case 'p':		/* priority */
			priority = strtos16_or_err(optarg,
					   _("failed to parse priority"));
			break;
		case 'L':
			add_label(optarg);
			break;
		case 'U':
			add_uuid(optarg);
			break;
		case 'd':
			discard |= SWAP_FLAG_DISCARD;
			if (optarg) {
				if (*optarg == '=')
					optarg++;

				if (strcmp(optarg, "once") == 0)
					discard |= SWAP_FLAG_DISCARD_ONCE;
				else if (strcmp(optarg, "pages") == 0)
					discard |= SWAP_FLAG_DISCARD_PAGES;
				else
					errx(EXIT_FAILURE, _("unsupported discard policy: %s"), optarg);
			}
			break;
		case 'e':               /* ifexists */
		        ifexists = 1;
			break;
		case 'f':
			fixpgsz = 1;
			break;
		case 's':		/* status report */
			status = display_summary();
			return status;
		case 'v':		/* be chatty */
			++verbose;
			break;
		case SHOW_OPTION:
			if (optarg) {
				ncolumns = string_to_idarray(optarg,
							     columns,
							     ARRAY_SIZE(columns),
							     column_name_to_id);
				if (ncolumns < 0)
					return EXIT_FAILURE;
			}
			show = 1;
			break;
		case NOHEADINGS_OPTION:
			tt_flags |= TT_FL_NOHEADINGS;
			break;
		case RAW_OPTION:
			tt_flags |= TT_FL_RAW;
			break;
		case BYTES_OPTION:
			bytes = 1;
			break;
		case 'V':		/* version */
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 0:
			break;
		case '?':
		default:
			usage(stderr);
		}
	}
	argv += optind;

	if (show || (!all && !numof_labels() && !numof_uuids() && *argv == NULL)) {
		if (!ncolumns) {
			/* default columns */
			columns[ncolumns++] = COL_PATH;
			columns[ncolumns++] = COL_TYPE;
			columns[ncolumns++] = COL_SIZE;
			columns[ncolumns++] = COL_USED;
			columns[ncolumns++] = COL_PRIO;
		}
		status = show_table(tt_flags, bytes);
		return status;
	}

	if (ifexists && !all)
		usage(stderr);

	if (all)
		status |= swapon_all();

	for (i = 0; i < numof_labels(); i++)
		status |= swapon_by_label(get_label(i), priority, discard);

	for (i = 0; i < numof_uuids(); i++)
		status |= swapon_by_uuid(get_uuid(i), priority, discard);

	while (*argv != NULL)
		status |= do_swapon(*argv++, priority, discard, !CANONIC);

	free_tables();
	mnt_unref_cache(mntcache);

	return status;
}
