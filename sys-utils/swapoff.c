#include <stdio.h>
#include <errno.h>
#include <getopt.h>

#ifdef HAVE_SYS_SWAP_H
# include <sys/swap.h>
#endif

#include "nls.h"
#include "c.h"
#include "closestream.h"

#include "swapon-common.h"

#ifndef SWAPON_HAS_TWO_ARGS
/* libc is insane, let's call the kernel */
# include <sys/syscall.h>
# define swapoff(path) syscall(SYS_swapoff, path)
#endif

static int verbose;
static int all;

#define QUIET	1
#define CANONIC	1

static int do_swapoff(const char *orig_special, int quiet, int canonic)
{
        const char *special = orig_special;

	if (verbose)
		printf(_("swapoff %s\n"), orig_special);

	if (!canonic) {
		special = mnt_resolve_spec(orig_special, mntcache);
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

static int swapoff_by_label(const char *label, int quiet)
{
	const char *special = mnt_resolve_tag("LABEL", label, mntcache);
	return special ? do_swapoff(special, quiet, CANONIC) : cannot_find(label);
}

static int swapoff_by_uuid(const char *uuid, int quiet)
{
	const char *special = mnt_resolve_tag("UUID", uuid, mntcache);
	return special ? do_swapoff(special, quiet, CANONIC) : cannot_find(uuid);
}

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fputs(USAGE_HEADER, out);

	fprintf(out, _(" %s [options] [<spec>]\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --all              disable all swaps from /proc/swaps\n"
		" -v, --verbose          verbose mode\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fputs(_("\nThe <spec> parameter:\n" \
		" -L <label>             LABEL of device to be used\n" \
		" -U <uuid>              UUID of device to be used\n" \
		" LABEL=<label>          LABEL of device to be used\n" \
		" UUID=<uuid>            UUID of device to be used\n" \
		" <device>               name of device to be used\n" \
		" <file>                 name of file to be used\n"), out);

	fprintf(out, USAGE_MAN_TAIL("swapoff(8)"));
	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static int swapoff_all(void)
{
	int status = 0;
	struct libmnt_table *tb;
	struct libmnt_fs *fs;
	struct libmnt_iter *itr = mnt_new_iter(MNT_ITER_BACKWARD);

	if (!itr)
		err(EXIT_FAILURE, _("failed to initialize libmount iterator"));

	/*
	 * In case /proc/swaps exists, unswap stuff listed there.  We are quiet
	 * but report errors in status.  Errors might mean that /proc/swaps
	 * exists as ordinary file, not in procfs.  do_swapoff() exits
	 * immediately on EPERM.
	 */
	tb = get_swaps();

	while (tb && mnt_table_find_next_fs(tb, itr, match_swap, NULL, &fs) == 0)
		status |= do_swapoff(mnt_fs_get_source(fs), QUIET, CANONIC);

	/*
	 * Unswap stuff mentioned in /etc/fstab.  Probably it was unmounted
	 * already, so errors are not bad.  Doing swapoff -a twice should not
	 * give error messages.
	 */
	tb = get_fstab();
	mnt_reset_iter(itr, MNT_ITER_FORWARD);

	while (tb && mnt_table_find_next_fs(tb, itr, match_swap, NULL, &fs) == 0) {
		if (!is_active_swap(mnt_fs_get_source(fs)))
			do_swapoff(mnt_fs_get_source(fs), QUIET, !CANONIC);
	}

	mnt_free_iter(itr);
	return status;
}

int main(int argc, char *argv[])
{
	int status = 0, c;
	size_t i;

	static const struct option long_opts[] = {
		{ "all", 0, 0, 'a' },
		{ "help", 0, 0, 'h' },
		{ "verbose", 0, 0, 'v' },
		{ "version", 0, 0, 'V' },
		{ NULL, 0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "ahvVL:U:",
				 long_opts, NULL)) != -1) {
		switch (c) {
		case 'a':		/* all */
			++all;
			break;
		case 'h':		/* help */
			usage(stdout);
			break;
		case 'v':		/* be chatty */
			++verbose;
			break;
		case 'V':		/* version */
			printf(UTIL_LINUX_VERSION);
			return EXIT_SUCCESS;
		case 'L':
			add_label(optarg);
			break;
		case 'U':
			add_uuid(optarg);
			break;
		case '?':
		default:
			usage(stderr);
		}
	}
	argv += optind;

	if (!all && !numof_labels() && !numof_uuids() && *argv == NULL)
		usage(stderr);

	mnt_init_debug(0);
	mntcache = mnt_new_cache();

	for (i = 0; i < numof_labels(); i++)
		status |= swapoff_by_label(get_label(i), !QUIET);

	for (i = 0; i < numof_uuids(); i++)
		status |= swapoff_by_uuid(get_uuid(i), !QUIET);

	while (*argv != NULL)
		status |= do_swapoff(*argv++, !QUIET, !CANONIC);

	if (all)
		status |= swapoff_all();

	free_tables();
	mnt_unref_cache(mntcache);

	return status;
}
