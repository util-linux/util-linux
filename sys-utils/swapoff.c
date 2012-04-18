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
		printf(_("swapoff  %s\n"), orig_special);

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

static void usage(FILE *out, int n)
{
	fputs(_("\nUsage:\n"), out);
	fprintf(out, _(" %s [options] [<spec>]\n"), program_invocation_short_name);

	fputs(_("\nOptions:\n"), out);
	fputs(_(" -a, --all              disable all swaps from /proc/swaps\n"
		" -h, --help             display help and exit\n"
		" -v, --verbose          verbose mode\n"
		" -V, --version          display version and exit\n"), out);

	fputs(_("\nThe <spec> parameter:\n" \
		" -L <label>             LABEL of device to be used\n" \
		" -U <uuid>              UUID of device to be used\n" \
		" LABEL=<label>          LABEL of device to be used\n" \
		" UUID=<uuid>            UUID of device to be used\n" \
		" <device>               name of device to be used\n" \
		" <file>                 name of file to be used\n\n"), out);
	exit(n);
}


int main(int argc, char *argv[])
{
	FILE *fp;
	struct mntent *fstab;
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
			usage(stdout, 0);
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
		case 0:
			break;
		case '?':
		default:
			usage(stderr, 1);
		}
	}
	argv += optind;

	if (!all && !numof_labels() && !numof_uuids() && *argv == NULL)
		usage(stderr, 2);

	mnt_init_debug(0);
	mntcache = mnt_new_cache();

	/*
	 * swapoff any explicitly given arguments.
	 * Complain in case the swapoff call fails.
	 */
	for (i = 0; i < numof_labels(); i++)
		status |= swapoff_by_label(get_label(i), !QUIET);

	for (i = 0; i < numof_uuids(); i++)
		status |= swapoff_by_uuid(get_uuid(i), !QUIET);

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
		struct libmnt_table *st = get_swaps();

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

			if (strcmp(fstab->mnt_type, MNTTYPE_SWAP) != 0)
				continue;

			special = mnt_resolve_spec(fstab->mnt_fsname, mntcache);
			if (!special)
				continue;

			if (!is_active_swap(special))
				do_swapoff(special, QUIET, CANONIC);
		}
		fclose(fp);
	}

	free_tables();
	mnt_free_cache(mntcache);

	return status;
}
