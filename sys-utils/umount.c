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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include "exitcodes.h"
#include "closestream.h"
#include "pathnames.h"
#include "canonicalize.h"
#include "xalloc.h"

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
	const char **features = NULL, **p;

	mnt_get_library_version(&ver);
	mnt_get_library_features(&features);

	printf(_("%s from %s (libmount %s"),
			program_invocation_short_name,
			PACKAGE_STRING,
			ver);
	p = features;
	while (p && *p) {
		fputs(p == features ? ": " : ", ", stdout);
		fputs(*p++, stdout);
	}
	fputs(")\n", stdout);
	exit(MOUNT_EX_SUCCESS);
}
static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(
		" %1$s [-hV]\n"
		" %1$s -a [options]\n"
		" %1$s [options] <source> | <directory>\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Unmount filesystems.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -a, --all               unmount all filesystems\n"), out);
	fputs(_(" -A, --all-targets       unmount all mountpoints for the given device in the\n"
	        "                           current namespace\n"), out);
	fputs(_(" -c, --no-canonicalize   don't canonicalize paths\n"), out);
	fputs(_(" -d, --detach-loop       if mounted loop device, also free this loop device\n"), out);
	fputs(_("     --fake              dry run; skip the umount(2) syscall\n"), out);
	fputs(_(" -f, --force             force unmount (in case of an unreachable NFS system)\n"), out);
	fputs(_(" -i, --internal-only     don't call the umount.<type> helpers\n"), out);
	fputs(_(" -n, --no-mtab           don't write to /etc/mtab\n"), out);
	fputs(_(" -l, --lazy              detach the filesystem now, clean up things later\n"), out);
	fputs(_(" -O, --test-opts <list>  limit the set of filesystems (use with -a)\n"), out);
	fputs(_(" -R, --recursive         recursively unmount a target with all its children\n"), out);
	fputs(_(" -r, --read-only         in case unmounting fails, try to remount read-only\n"), out);
	fputs(_(" -t, --types <list>      limit the set of filesystem types\n"), out);
	fputs(_(" -v, --verbose           say what is being done\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("umount(8)"));

	exit(out == stderr ? MOUNT_EX_USAGE : MOUNT_EX_SUCCESS);
}

static void __attribute__((__noreturn__)) exit_non_root(const char *option)
{
	const uid_t ruid = getuid();
	const uid_t euid = geteuid();

	if (ruid == 0 && euid != 0) {
		/* user is root, but setuid to non-root */
		if (option)
			errx(MOUNT_EX_USAGE,
				_("only root can use \"--%s\" option "
				 "(effective UID is %u)"),
					option, euid);
		errx(MOUNT_EX_USAGE, _("only root can do that "
				 "(effective UID is %u)"), euid);
	}
	if (option)
		errx(MOUNT_EX_USAGE, _("only root can use \"--%s\" option"), option);
	errx(MOUNT_EX_USAGE, _("only root can do that"));
}

static void success_message(struct libmnt_context *cxt)
{
	const char *tgt, *src;

	if (mnt_context_helper_executed(cxt)
	    || mnt_context_get_status(cxt) != 1)
		return;

	tgt = mnt_context_get_target(cxt);
	if (!tgt)
		return;

	src = mnt_context_get_source(cxt);
	if (src)
		warnx(_("%s (%s) unmounted"), tgt, src);
	else
		warnx(_("%s unmounted"), tgt);
}

/*
 * Handles generic errors like ENOMEM, ...
 *
 * rc = 0 success
 *     <0 error (usually -errno)
 *
 * Returns exit status (MOUNT_EX_*) and prints error message.
 */
static int handle_generic_errors(int rc, const char *msg, ...)
{
	va_list va;

	va_start(va, msg);
	errno = -rc;

	switch(errno) {
	case EINVAL:
	case EPERM:
		vwarn(msg, va);
		rc = MOUNT_EX_USAGE;
		break;
	case ENOMEM:
		vwarn(msg, va);
		rc = MOUNT_EX_SYSERR;
		break;
	default:
		vwarn(msg, va);
		rc = MOUNT_EX_FAIL;
		break;
	}
	va_end(va);
	return rc;
}

static int mk_exit_code(struct libmnt_context *cxt, int rc)
{
	int syserr;
	const char *tgt = mnt_context_get_target(cxt);

	if (mnt_context_helper_executed(cxt))
		/*
		 * /sbin/umount.<type> called, return status
		 */
		return mnt_context_get_helper_status(cxt);

	if (rc == 0 && mnt_context_get_status(cxt) == 1)
		/*
		 * Libmount success && syscall success.
		 */
		return MOUNT_EX_SUCCESS;


	if (!mnt_context_syscall_called(cxt)) {
		/*
		 * libmount errors (extra library checks)
		 */
		if (rc == -EPERM && !mnt_context_tab_applied(cxt)) {
			/* failed to evaluate permissions because not found
			 * relevant entry in mtab */
			warnx(_("%s: not mounted"), tgt);
			return MOUNT_EX_USAGE;
		}
		return handle_generic_errors(rc, _("%s: umount failed"), tgt);

	} else if (mnt_context_get_syscall_errno(cxt) == 0) {
		/*
		 * umount(2) syscall success, but something else failed
		 * (probably error in mtab processing).
		 */
		if (rc < 0)
			return handle_generic_errors(rc,
				_("%s: filesystem was unmounted, but mount(8) failed"),
				tgt);

		return MOUNT_EX_SOFTWARE;	/* internal error */

	}

	/*
	 * umount(2) errors
	 */
	syserr = mnt_context_get_syscall_errno(cxt);

	switch(syserr) {
	case ENXIO:
		warnx(_("%s: invalid block device"), tgt);	/* ??? */
		break;
	case EINVAL:
		warnx(_("%s: not mounted"), tgt);
		break;
	case EIO:
		warnx(_("%s: can't write superblock"), tgt);
		break;
	case EBUSY:
		warnx(_("%s: target is busy\n"
		       "        (In some cases useful info about processes that\n"
		       "         use the device is found by lsof(8) or fuser(1).)"),
			tgt);
		break;
	case ENOENT:
		if (tgt && *tgt)
			warnx(_("%s: mountpoint not found"), tgt);
		else
			warnx(_("undefined mountpoint"));
		break;
	case EPERM:
		warnx(_("%s: must be superuser to unmount"), tgt);
		break;
	case EACCES:
		warnx(_("%s: block devices are not permitted on filesystem"), tgt);
		break;
	default:
		errno = syserr;
		warn("%s", tgt);
		break;
	}
	return MOUNT_EX_FAIL;
}

static int umount_all(struct libmnt_context *cxt)
{
	struct libmnt_iter *itr;
	struct libmnt_fs *fs;
	int mntrc, ignored, rc = 0;

	itr = mnt_new_iter(MNT_ITER_BACKWARD);
	if (!itr) {
		warn(_("failed to initialize libmount iterator"));
		return MOUNT_EX_SYSERR;
	}

	while (mnt_context_next_umount(cxt, itr, &fs, &mntrc, &ignored) == 0) {

		const char *tgt = mnt_fs_get_target(fs);

		if (ignored) {
			if (mnt_context_is_verbose(cxt))
				printf(_("%-25s: ignored\n"), tgt);
		} else {
			int xrc = mk_exit_code(cxt, mntrc);

			if (xrc == MOUNT_EX_SUCCESS
			    && mnt_context_is_verbose(cxt))
				printf("%-25s: successfully unmounted\n", tgt);
			rc |= xrc;
		}
	}

	mnt_free_iter(itr);
	return rc;
}

static int umount_one(struct libmnt_context *cxt, const char *spec)
{
	int rc;

	if (!spec)
		return MOUNT_EX_SOFTWARE;

	if (mnt_context_set_target(cxt, spec))
		err(MOUNT_EX_SYSERR, _("failed to set umount target"));

	rc = mnt_context_umount(cxt);
	rc = mk_exit_code(cxt, rc);

	if (rc == MOUNT_EX_SUCCESS && mnt_context_is_verbose(cxt))
		success_message(cxt);

	mnt_reset_context(cxt);
	return rc;
}

static struct libmnt_table *new_mountinfo(struct libmnt_context *cxt)
{
	struct libmnt_table *tb = mnt_new_table();
	if (!tb)
		err(MOUNT_EX_SYSERR, _("libmount table allocation failed"));

	mnt_table_set_parser_errcb(tb, table_parser_errcb);
	mnt_table_set_cache(tb, mnt_context_get_cache(cxt));

	if (mnt_table_parse_file(tb, _PATH_PROC_MOUNTINFO)) {
		warn(_("failed to parse %s"), _PATH_PROC_MOUNTINFO);
		mnt_unref_table(tb);
		tb = NULL;
	}

	return tb;
}

/*
 * like umount_one() but does not return error is @spec not mounted
 */
static int umount_one_if_mounted(struct libmnt_context *cxt, const char *spec)
{
	int rc;
	struct libmnt_fs *fs;

	rc = mnt_context_find_umount_fs(cxt, spec, &fs);
	if (rc == 1) {
		rc = MOUNT_EX_SUCCESS;		/* alredy unmounted */
		mnt_reset_context(cxt);
	} else if (rc < 0) {
		rc = mk_exit_code(cxt, rc);	/* error */
		mnt_reset_context(cxt);
	} else
		rc = umount_one(cxt, mnt_fs_get_target(fs));

	return rc;
}

static int umount_do_recurse(struct libmnt_context *cxt,
		struct libmnt_table *tb, struct libmnt_fs *fs)
{
	struct libmnt_fs *child;
	struct libmnt_iter *itr = mnt_new_iter(MNT_ITER_BACKWARD);
	int rc;

	if (!itr)
		err(MOUNT_EX_SYSERR, _("libmount iterator allocation failed"));

	/* umount all childern */
	for (;;) {
		rc = mnt_table_next_child_fs(tb, itr, fs, &child);
		if (rc < 0) {
			warnx(_("failed to get child fs of %s"),
					mnt_fs_get_target(fs));
			rc = MOUNT_EX_SOFTWARE;
			goto done;
		} else if (rc == 1)
			break;		/* no more children */

		rc = umount_do_recurse(cxt, tb, child);
		if (rc != MOUNT_EX_SUCCESS)
			goto done;
	}

	rc = umount_one_if_mounted(cxt, mnt_fs_get_target(fs));
done:
	mnt_free_iter(itr);
	return rc;
}

static int umount_recursive(struct libmnt_context *cxt, const char *spec)
{
	struct libmnt_table *tb;
	struct libmnt_fs *fs;
	int rc;

	tb = new_mountinfo(cxt);
	if (!tb)
		return MOUNT_EX_SOFTWARE;

	/* it's always real mountpoint, don't assume that the target maybe a device */
	mnt_context_disable_swapmatch(cxt, 1);

	fs = mnt_table_find_target(tb, spec, MNT_ITER_BACKWARD);
	if (fs)
		rc = umount_do_recurse(cxt, tb, fs);
	else {
		rc = MOUNT_EX_USAGE;
		warnx(access(spec, F_OK) == 0 ?
				_("%s: not mounted") :
				_("%s: not found"), spec);
	}

	mnt_unref_table(tb);
	return rc;
}

static int umount_alltargets(struct libmnt_context *cxt, const char *spec, int rec)
{
	struct libmnt_fs *fs;
	struct libmnt_table *tb;
	struct libmnt_iter *itr = NULL;
	dev_t devno = 0;
	int rc;

	/* Convert @spec to device name, Use the same logic like regular
	 * "umount <spec>".
	 */
	rc = mnt_context_find_umount_fs(cxt, spec, &fs);
	if (rc == 1) {
		rc = MOUNT_EX_USAGE;
		warnx(access(spec, F_OK) == 0 ?
				_("%s: not mounted") :
				_("%s: not found"), spec);
		return rc;
	}
	if (rc < 0)
		return mk_exit_code(cxt, rc);		/* error */

	if (!mnt_fs_get_srcpath(fs) || !mnt_fs_get_devno(fs))
		errx(MOUNT_EX_USAGE, _("%s: failed to determine source "
				"(--all-targets is unsupported on systems with "
				"regular mtab file)."), spec);

	itr = mnt_new_iter(MNT_ITER_BACKWARD);
	if (!itr)
		err(MOUNT_EX_SYSERR, _("libmount iterator allocation failed"));

	/* get on @cxt independent mountinfo */
	tb = new_mountinfo(cxt);
	if (!tb) {
		rc = MOUNT_EX_SOFTWARE;
		goto done;
	}

	/* Note that @fs is from mount context and the context will be reseted
	 * after each umount() call */
	devno = mnt_fs_get_devno(fs);
	fs = NULL;

	mnt_reset_context(cxt);

	while (mnt_table_next_fs(tb, itr, &fs) == 0) {
		if (mnt_fs_get_devno(fs) != devno)
			continue;
		mnt_context_disable_swapmatch(cxt, 1);
		if (rec)
			rc = umount_do_recurse(cxt, tb, fs);
		else
			rc = umount_one_if_mounted(cxt, mnt_fs_get_target(fs));

		if (rc != MOUNT_EX_SUCCESS)
			break;
	}

done:
	mnt_free_iter(itr);
	mnt_unref_table(tb);

	return rc;
}

/*
 * Check path -- non-root user should not be able to resolve path which is
 * unreadable for him.
 */
static char *sanitize_path(const char *path)
{
	char *p;

	if (!path)
		return NULL;

	p = canonicalize_path_restricted(path);
	if (!p)
		err(MOUNT_EX_USAGE, "%s", path);

	return p;
}

int main(int argc, char **argv)
{
	int c, rc = 0, all = 0, recursive = 0, alltargets = 0;
	struct libmnt_context *cxt;
	char *types = NULL;

	enum {
		UMOUNT_OPT_FAKE = CHAR_MAX + 1,
	};

	static const struct option longopts[] = {
		{ "all", 0, 0, 'a' },
		{ "all-targets", 0, 0, 'A' },
		{ "detach-loop", 0, 0, 'd' },
		{ "fake", 0, 0, UMOUNT_OPT_FAKE },
		{ "force", 0, 0, 'f' },
		{ "help", 0, 0, 'h' },
		{ "internal-only", 0, 0, 'i' },
		{ "lazy", 0, 0, 'l' },
		{ "no-canonicalize", 0, 0, 'c' },
		{ "no-mtab", 0, 0, 'n' },
		{ "read-only", 0, 0, 'r' },
		{ "recursive", 0, 0, 'R' },
		{ "test-opts", 1, 0, 'O' },
		{ "types", 1, 0, 't' },
		{ "verbose", 0, 0, 'v' },
		{ "version", 0, 0, 'V' },
		{ NULL, 0, 0, 0 }
	};

	static const ul_excl_t excl[] = {       /* rows and cols in in ASCII order */
		{ 'A','a' },			/* all-targets,all */
		{ 'R','a' },			/* recursive,all */
		{ 'O','R','t'},			/* options,recursive,types */
		{ 'R','r' },			/* recursive,read-only */
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	sanitize_env();
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	mnt_init_debug(0);
	cxt = mnt_new_context();
	if (!cxt)
		err(MOUNT_EX_SYSERR, _("libmount context allocation failed"));

	mnt_context_set_tables_errcb(cxt, table_parser_errcb);

	while ((c = getopt_long(argc, argv, "aAcdfhilnRrO:t:vV",
					longopts, NULL)) != -1) {


		/* only few options are allowed for non-root users */
		if (mnt_context_is_restricted(cxt) && !strchr("hdilVv", c))
			exit_non_root(option_to_longopt(c, longopts));

		err_exclusive_options(c, longopts, excl, excl_st);

		switch(c) {
		case 'a':
			all = 1;
			break;
		case 'A':
			alltargets = 1;
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
		case 'R':
			recursive = TRUE;
			break;
		case 'O':
			if (mnt_context_set_options_pattern(cxt, optarg))
				err(MOUNT_EX_SYSERR, _("failed to set options pattern"));
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

	} else if (alltargets) {
		while (argc--)
			rc += umount_alltargets(cxt, *argv++, recursive);
	} else if (recursive) {
		while (argc--)
			rc += umount_recursive(cxt, *argv++);
	} else {
		while (argc--) {
			char *path = *argv;

			if (mnt_context_is_restricted(cxt)
			    && !mnt_tag_is_valid(path))
				path = sanitize_path(path);

			rc += umount_one(cxt, path);

			if (path != *argv)
				free(path);
			argv++;
		}
	}

	mnt_free_context(cxt);
	return (rc < 256) ? rc : 255;
}

