/*
 * umount(8) -- mount a filesystem
 *
 * Copyright (C) 2011 Red Hat, Inc. All rights reserved.
 * Written by Karel Zak <kzak@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/types.h>

#include <libmount.h>

#include "nls.h"
#include "c.h"
#include "env.h"
#include "optutils.h"

static int table_parser_errcb(struct libmnt_table *tb __attribute__((__unused__)),
			const char *filename, int line)
{
	if (filename)
		warnx(_("%s: parse error: ignore entry at line %d."),
							filename, line);
	return 0;
}

static void __attribute__((__noreturn__)) print_version(void)
{
	const char *ver = NULL;

	mnt_get_library_version(&ver);

	printf(_("%s from %s (libmount %s)\n"),
			program_invocation_short_name, PACKAGE_STRING, ver);
	exit(EXIT_SUCCESS);
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(
		" %1$s [-hV]\n"
		" %1$s -a [options]\n"
		" %1$s [options] <source> | <directory>\n"),
		program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fprintf(out, _(
	" -a, --all               mount all filesystems mentioned in fstab\n"
	" -c, --no-canonicalize   don't canonicalize paths\n"
	" -d, --detach-loop       if mounted loop device, also free this loop device\n"
	"     --fake              dry run; skip the umount(2) syscall\n"
	" -f, --force             force unmount (in case of an unreachable NFS system)\n"));
	fprintf(out, _(
	" -i, --internal-only     don't call the umount.<type> helpers\n"
	" -n, --no-mtab           don't write to /etc/mtab\n"
	" -l, --lazy              detach the filesystem now, and cleanup all later\n"));
	fprintf(out, _(
	" -O, --test-opts <list>  limit the set of filesystems (use with -a)\n"
	" -r, --read-only         In case unmounting fails, try to remount read-only\n"
	" -t, --types <list>      limit the set of filesystem types\n"
	" -v, --verbose           say what is being done\n"));

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("umount(8)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void __attribute__((__noreturn__)) exit_non_root(const char *option)
{
	const uid_t ruid = getuid();
	const uid_t euid = geteuid();

	if (ruid == 0 && euid != 0) {
		/* user is root, but setuid to non-root */
		if (option)
			errx(EXIT_FAILURE,
				_("only root can use \"--%s\" option "
				 "(effective UID is %u)"),
					option, euid);
		errx(EXIT_FAILURE, _("only root can do that "
				 "(effective UID is %u)"), euid);
	}
	if (option)
		errx(EXIT_FAILURE, _("only root can use \"--%s\" option"), option);
	errx(EXIT_FAILURE, _("only root can do that"));
}

static int umount_all(struct libmnt_context *cxt __attribute__((__unused__)))
{
	return -1;	/* TODO */
}

static int umount_one(struct libmnt_context *cxt, const char *spec)
{
	int rc;

	if (!spec)
		return -EINVAL;

	if (mnt_context_set_target(cxt, spec))
		err(EXIT_FAILURE, _("failed to set umount target"));

	rc = mnt_context_umount(cxt);
	if (rc)
		/* TODO mnt_context_strerror() */
		warnx(_("%s: umount failed"), spec);

	mnt_reset_context(cxt);
	return rc;
}

int main(int argc, char **argv)
{
	int c, rc, all = 0;
	struct libmnt_context *cxt;
	char *types = NULL;

	enum {
		UMOUNT_OPT_FAKE = CHAR_MAX + 1,
	};

	static const struct option longopts[] = {
		{ "all", 0, 0, 'a' },
		{ "detach-loop", 0, 0, 'd' },
		{ "fake", 0, 0, UMOUNT_OPT_FAKE },
		{ "force", 0, 0, 'f' },
		{ "help", 0, 0, 'h' },
		{ "internal-only", 0, 0, 'i' },
		{ "lazy", 0, 0, 'l' },
		{ "no-canonicalize", 0, 0, 'c' },
		{ "no-mtab", 0, 0, 'n' },
		{ "read-only", 0, 0, 'r' },
		{ "test-opts", 1, 0, 'O' },
		{ "types", 1, 0, 't' },
		{ "verbose", 0, 0, 'v' },
		{ "version", 0, 0, 'V' },
		{ NULL, 0, 0, 0 }
	};

	sanitize_env();
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	mnt_init_debug(0);
	cxt = mnt_new_context();
	if (!cxt)
		err(EXIT_FAILURE, _("libmount context allocation failed"));

	mnt_context_set_tables_errcb(cxt, table_parser_errcb);

	while ((c = getopt_long(argc, argv, "acdfhilnrO:t:vV",
					longopts, NULL)) != -1) {


		/* only few options are allowed for non-root users */
		if (mnt_context_is_restricted(cxt) && !strchr("hdilVv", c))
			exit_non_root(option_to_longopt(c, longopts));

		switch(c) {
		case 'a':
			all = 1;
			break;
		case 'c':
			mnt_context_disable_canonicalize(cxt, TRUE);
			break;
		case 'd':
			mnt_context_enable_loopdel(cxt, TRUE);
			break;
		case UMOUNT_OPT_FAKE:
			mnt_context_enable_fake(cxt, TRUE);
			break;
		case 'f':
			mnt_context_enable_force(cxt, TRUE);
			break;
		case 'h':
			usage(stdout);
			break;
		case 'i':
			mnt_context_disable_helpers(cxt, TRUE);
			break;
		case 'l':
			mnt_context_enable_lazy(cxt, TRUE);
			break;
		case 'n':
			mnt_context_disable_mtab(cxt, TRUE);
			break;
		case 'r':
			mnt_context_enable_rdonly_umount(cxt, TRUE);
			break;
		case 'O':
			if (mnt_context_set_options_pattern(cxt, optarg))
				err(EXIT_FAILURE, _("failed to set options pattern"));
			break;
		case 't':
			types = optarg;
			break;
		case 'v':
			mnt_context_enable_verbose(cxt, TRUE);
			break;
		case 'V':
			print_version();
			break;
		default:
			usage(stderr);
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (all) {
		if (!types)
			types = "noproc,nodevfs,nodevpts,nosysfs,norpc_pipefs,nonfsd";

		mnt_context_set_fstype_pattern(cxt, types);
		rc = umount_all(cxt);

	} else if (argc < 1) {
		usage(stderr);

	} else while (argc--) {
		rc += umount_one(cxt, *argv++);
	}

	mnt_free_context(cxt);
	return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}

