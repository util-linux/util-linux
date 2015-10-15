/*
 * mount(8) -- mount a filesystem
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
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <libmount.h>
#include <ctype.h>

#include "nls.h"
#include "c.h"
#include "env.h"
#include "strutils.h"
#include "exitcodes.h"
#include "xalloc.h"
#include "closestream.h"
#include "canonicalize.h"

#define OPTUTILS_EXIT_CODE MOUNT_EX_USAGE
#include "optutils.h"

/*** TODO: DOCS:
 *
 *  --options-mode={ignore,append,prepend,replace}	MNT_OMODE_{IGNORE, ...}
 *  --options-source={fstab,mtab,disable}		MNT_OMODE_{FSTAB,MTAB,NOTAB}
 *  --options-source-force				MNT_OMODE_FORCE
 */

static int readwrite;

static int mk_exit_code(struct libmnt_context *cxt, int rc);

static void __attribute__((__noreturn__)) exit_non_root(const char *option)
{
	const uid_t ruid = getuid();
	const uid_t euid = geteuid();

	if (ruid == 0 && euid != 0) {
		/* user is root, but setuid to non-root */
		if (option)
			errx(MOUNT_EX_USAGE, _("only root can use \"--%s\" option "
					 "(effective UID is %u)"),
					option, euid);
		errx(MOUNT_EX_USAGE, _("only root can do that "
				 "(effective UID is %u)"), euid);
	}
	if (option)
		errx(MOUNT_EX_USAGE, _("only root can use \"--%s\" option"), option);
	errx(MOUNT_EX_USAGE, _("only root can do that"));
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

static int table_parser_errcb(struct libmnt_table *tb __attribute__((__unused__)),
			const char *filename, int line)
{
	if (filename)
		warnx(_("%s: parse error: ignore entry at line %d."),
							filename, line);
	return 1;
}

/*
 * Replace control chars with '?' to be compatible with coreutils. For more
 * robust solution use findmnt(1) where we use \x?? hex encoding.
 */
static void safe_fputs(const char *data)
{
	const char *p;

	for (p = data; p && *p; p++) {
		if (iscntrl((unsigned char) *p))
			fputc('?', stdout);
		else
			fputc(*p, stdout);
	}
}

static void print_all(struct libmnt_context *cxt, char *pattern, int show_label)
{
	struct libmnt_table *tb;
	struct libmnt_iter *itr = NULL;
	struct libmnt_fs *fs;
	struct libmnt_cache *cache = NULL;

	if (mnt_context_get_mtab(cxt, &tb))
		err(MOUNT_EX_SYSERR, _("failed to read mtab"));

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr)
		err(MOUNT_EX_SYSERR, _("failed to initialize libmount iterator"));
	if (show_label)
		cache = mnt_new_cache();

	while (mnt_table_next_fs(tb, itr, &fs) == 0) {
		const char *type = mnt_fs_get_fstype(fs);
		const char *src = mnt_fs_get_source(fs);
		const char *optstr = mnt_fs_get_options(fs);
		char *xsrc = NULL;

		if (type && pattern && !mnt_match_fstype(type, pattern))
			continue;

		if (!mnt_fs_is_pseudofs(fs))
			xsrc = mnt_pretty_path(src, cache);
		printf ("%s on ", xsrc ? xsrc : src);
		safe_fputs(mnt_fs_get_target(fs));

		if (type)
			printf (" type %s", type);
		if (optstr)
			printf (" (%s)", optstr);
		if (show_label && src) {
			char *lb = mnt_cache_find_tag_value(cache, src, "LABEL");
			if (lb)
				printf (" [%s]", lb);
		}
		fputc('\n', stdout);
		free(xsrc);
	}

	mnt_unref_cache(cache);
	mnt_free_iter(itr);
}

/*
 * mount -a [-F]
 */
static int mount_all(struct libmnt_context *cxt)
{
	struct libmnt_iter *itr;
	struct libmnt_fs *fs;
	int mntrc, ignored, rc = MOUNT_EX_SUCCESS;

	int nsucc = 0, nerrs = 0;

	itr = mnt_new_iter(MNT_ITER_FORWARD);
	if (!itr) {
		warn(_("failed to initialize libmount iterator"));
		return MOUNT_EX_SYSERR;
	}

	while (mnt_context_next_mount(cxt, itr, &fs, &mntrc, &ignored) == 0) {

		const char *tgt = mnt_fs_get_target(fs);

		if (ignored) {
			if (mnt_context_is_verbose(cxt))
				printf(ignored == 1 ? _("%-25s: ignored\n") :
						      _("%-25s: already mounted\n"),
						tgt);
		} else if (mnt_context_is_fork(cxt)) {
			if (mnt_context_is_verbose(cxt))
				printf("%-25s: mount successfully forked\n", tgt);
		} else {
			if (mk_exit_code(cxt, mntrc) == MOUNT_EX_SUCCESS) {
				nsucc++;

				/* Note that MOUNT_EX_SUCCESS return code does
				 * not mean that FS has been really mounted
				 * (e.g. nofail option) */
				if (mnt_context_get_status(cxt) 
				    && mnt_context_is_verbose(cxt))
					printf("%-25s: successfully mounted\n", tgt);
			} else
				nerrs++;
		}
	}

	if (mnt_context_is_parent(cxt)) {
		/* wait for mount --fork children */
		int nchildren = 0;

		nerrs = 0, nsucc = 0;

		rc = mnt_context_wait_for_children(cxt, &nchildren, &nerrs);
		if (!rc && nchildren)
			nsucc = nchildren - nerrs;
	}

	if (nerrs == 0)
		rc = MOUNT_EX_SUCCESS;		/* all success */
	else if (nsucc == 0)
		rc = MOUNT_EX_FAIL;		/* all failed */
	else
		rc = MOUNT_EX_SOMEOK;		/* some success, some failed */

	mnt_free_iter(itr);
	return rc;
}

static void success_message(struct libmnt_context *cxt)
{
	unsigned long mflags = 0;
	const char *tgt, *src, *pr = program_invocation_short_name;

	if (mnt_context_helper_executed(cxt)
	    || mnt_context_get_status(cxt) != 1)
		return;

	mnt_context_get_mflags(cxt, &mflags);
	tgt = mnt_context_get_target(cxt);
	src = mnt_context_get_source(cxt);

	if (mflags & MS_MOVE)
		printf(_("%s: %s moved to %s.\n"), pr, src, tgt);
	else if (mflags & MS_BIND)
		printf(_("%s: %s bound on %s.\n"), pr, src, tgt);
	else if (mflags & MS_PROPAGATION) {
		if (src && strcmp(src, "none") != 0 && tgt)
			printf(_("%s: %s mounted on %s.\n"), pr, src, tgt);

		printf(_("%s: %s propagation flags changed.\n"), pr, tgt);
	} else
		printf(_("%s: %s mounted on %s.\n"), pr, src, tgt);
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

#if defined(HAVE_LIBSELINUX) && defined(HAVE_SECURITY_GET_INITIAL_CONTEXT)
#include <selinux/selinux.h>
#include <selinux/context.h>

static void selinux_warning(struct libmnt_context *cxt, const char *tgt)
{

	if (tgt && mnt_context_is_verbose(cxt) && is_selinux_enabled() > 0) {
		security_context_t raw = NULL, def = NULL;

		if (getfilecon(tgt, &raw) > 0
		    && security_get_initial_context("file", &def) == 0) {

		if (!selinux_file_context_cmp(raw, def))
			printf(_(
	"mount: %s does not contain SELinux labels.\n"
	"       You just mounted an file system that supports labels which does not\n"
	"       contain labels, onto an SELinux box. It is likely that confined\n"
	"       applications will generate AVC messages and not be allowed access to\n"
	"       this file system.  For more details see restorecon(8) and mount(8).\n"),
				tgt);
		}
		freecon(raw);
		freecon(def);
	}
}
#else
# define selinux_warning(_x, _y)
#endif

/*
 * Returns 1 if @dir parent is shared
 */
static int is_shared_tree(struct libmnt_context *cxt, const char *dir)
{
	struct libmnt_table *tb = NULL;
	struct libmnt_fs *fs;
	unsigned long mflags = 0;
	char *mnt = NULL, *p;
	int rc = 0;

	if (!dir)
		return 0;
	if (mnt_context_get_mtab(cxt, &tb) || !tb)
		goto done;
	mnt = xstrdup(dir);
	p = strrchr(mnt, '/');
	if (!p)
		goto done;
	if (p > mnt)
		*p = '\0';
	fs = mnt_table_find_mountpoint(tb, mnt, MNT_ITER_BACKWARD);

	rc = fs && mnt_fs_is_kernel(fs)
		&& mnt_fs_get_propagation(fs, &mflags) == 0
		&& (mflags & MS_SHARED);
done:
	free(mnt);
	return rc;
}

/*
 * rc = 0 success
 *     <0 error (usually -errno or -1)
 *
 * Returns exit status (MOUNT_EX_*) and/or prints error message.
 */
static int mk_exit_code(struct libmnt_context *cxt, int rc)
{
	int syserr;
	struct stat st;
	unsigned long uflags = 0, mflags = 0;

	int restricted = mnt_context_is_restricted(cxt);
	const char *tgt = mnt_context_get_target(cxt);
	const char *src = mnt_context_get_source(cxt);

try_readonly:
	if (mnt_context_helper_executed(cxt)) {
		/*
		 * /sbin/mount.<type> called, return status
		 */
		if (rc == -MNT_ERR_APPLYFLAGS)
			warnx(_("WARNING: failed to apply propagation flags"));
		return mnt_context_get_helper_status(cxt);
	}

	if (rc == 0 && mnt_context_get_status(cxt) == 1) {
		/*
		 * Libmount success && syscall success.
		 */
		selinux_warning(cxt, tgt);

		return MOUNT_EX_SUCCESS;	/* mount(2) success */
	}

	mnt_context_get_mflags(cxt, &mflags);		/* mount(2) flags */
	mnt_context_get_user_mflags(cxt, &uflags);	/* userspace flags */

	if (!mnt_context_syscall_called(cxt)) {
		/*
		 * libmount errors (extra library checks)
		 */
		switch (rc) {
		case -EPERM:
			warnx(_("only root can mount %s on %s"), src, tgt);
			return MOUNT_EX_USAGE;
		case -EBUSY:
			warnx(_("%s is already mounted"), src);
			return MOUNT_EX_USAGE;
		case -MNT_ERR_NOFSTAB:
			if (mnt_context_is_swapmatch(cxt)) {
				warnx(_("can't find %s in %s"),
						src ? src : tgt,
						mnt_get_fstab_path());
				return MOUNT_EX_USAGE;
			}
			/* source/target explicitly defined */
			if (tgt)
				warnx(_("can't find mountpoint %s in %s"),
						tgt, mnt_get_fstab_path());
			else
				warnx(_("can't find mount source %s in %s"),
						src, mnt_get_fstab_path());
			return MOUNT_EX_USAGE;
		case -MNT_ERR_AMBIFS:
			warnx(_("%s: more filesystems detected. This should not happen,\n"
			  "       use -t <type> to explicitly specify the filesystem type or\n"
			  "       use wipefs(8) to clean up the device."), src);
			return MOUNT_EX_USAGE;
		case -MNT_ERR_NOFSTYPE:
			if (restricted)
				warnx(_("I could not determine the filesystem type, "
					"and none was specified"));
			else
				warnx(_("you must specify the filesystem type"));
			return MOUNT_EX_USAGE;
		case -MNT_ERR_NOSOURCE:
			if (uflags & MNT_MS_NOFAIL)
				return MOUNT_EX_SUCCESS;
			if (src)
				warnx(_("can't find %s"), src);
			else
				warnx(_("mount source not defined"));
			return MOUNT_EX_USAGE;
		case -MNT_ERR_MOUNTOPT:
			if (errno)
				warn(_("failed to parse mount options"));
			else
				warnx(_("failed to parse mount options"));
			return MOUNT_EX_USAGE;
		case -MNT_ERR_LOOPDEV:
			warn(_("%s: failed to setup loop device"), src);
			return MOUNT_EX_FAIL;
		default:
			return handle_generic_errors(rc, _("%s: mount failed"),
					     tgt ? tgt : src);
		}
	} else if (mnt_context_get_syscall_errno(cxt) == 0) {
		/*
		 * mount(2) syscall success, but something else failed
		 * (probably error in mtab processing).
		 */
		if (rc < 0)
			return handle_generic_errors(rc,
				_("%s: filesystem mounted, but mount(8) failed"),
				tgt ? tgt : src);

		return MOUNT_EX_SOFTWARE;	/* internal error */

	}

	/*
	 * mount(2) errors
	 */
	syserr = mnt_context_get_syscall_errno(cxt);


	switch(syserr) {
	case EPERM:
		if (geteuid() == 0) {
			if (stat(tgt, &st) || !S_ISDIR(st.st_mode))
				warnx(_("mount point %s is not a directory"), tgt);
			else
				warnx(_("permission denied"));
		} else
			warnx(_("must be superuser to use mount"));
	      break;

	case EBUSY:
	{
		struct libmnt_table *tb;

		if (mflags & MS_REMOUNT) {
			warnx(_("%s is busy"), tgt);
			break;
		}

		warnx(_("%s is already mounted or %s busy"), src, tgt);

		if (src && mnt_context_get_mtab(cxt, &tb) == 0) {
			struct libmnt_iter *itr = mnt_new_iter(MNT_ITER_FORWARD);
			struct libmnt_fs *fs;

			while(mnt_table_next_fs(tb, itr, &fs) == 0) {
				const char *s = mnt_fs_get_srcpath(fs),
					   *t = mnt_fs_get_target(fs);

				if (t && s && mnt_fs_streq_srcpath(fs, src))
					fprintf(stderr, _(
						"       %s is already mounted on %s\n"), s, t);
			}
			mnt_free_iter(itr);
		}
		break;
	}
	case ENOENT:
		if (tgt && lstat(tgt, &st))
			warnx(_("mount point %s does not exist"), tgt);
		else if (tgt && stat(tgt, &st))
			warnx(_("mount point %s is a symbolic link to nowhere"), tgt);
		else if (src && stat(src, &st)) {
			if (uflags & MNT_MS_NOFAIL)
				return MOUNT_EX_SUCCESS;

			warnx(_("special device %s does not exist"), src);
		} else {
			errno = syserr;
			warn(_("mount(2) failed"));	/* print errno */
		}
		break;

	case ENOTDIR:
		if (stat(tgt, &st) || ! S_ISDIR(st.st_mode))
			warnx(_("mount point %s is not a directory"), tgt);
		else if (src && stat(src, &st) && errno == ENOTDIR) {
			if (uflags & MNT_MS_NOFAIL)
				return MOUNT_EX_SUCCESS;

			warnx(_("special device %s does not exist "
				 "(a path prefix is not a directory)"), src);
		} else {
			errno = syserr;
			warn(_("mount(2) failed"));     /* print errno */
		}
		break;

	case EINVAL:
		if (mflags & MS_REMOUNT)
			warnx(_("%s not mounted or bad option"), tgt);
		else if (rc == -MNT_ERR_APPLYFLAGS)
			warnx(_("%s is not mountpoint or bad option"), tgt);
		else if ((mflags & MS_MOVE) && is_shared_tree(cxt, src))
			warnx(_("bad option. Note that moving a mount residing under a shared\n"
				"       mount is unsupported."));
		else
			warnx(_("wrong fs type, bad option, bad superblock on %s,\n"
				"       missing codepage or helper program, or other error"),
				src);

		if (mnt_fs_is_netfs(mnt_context_get_fs(cxt)))
			fprintf(stderr, _(
				"       (for several filesystems (e.g. nfs, cifs) you might\n"
				"       need a /sbin/mount.<type> helper program)\n"));

		fprintf(stderr, _("\n"
				"       In some cases useful info is found in syslog - try\n"
				"       dmesg | tail or so.\n"));
		break;

	case EMFILE:
		warnx(_("mount table full"));
		break;

	case EIO:
		warnx(_("%s: can't read superblock"), src);
		break;

	case ENODEV:
		if (mnt_context_get_fstype(cxt))
			warnx(_("unknown filesystem type '%s'"), mnt_context_get_fstype(cxt));
		else
			warnx(_("unknown filesystem type"));
		break;

	case ENOTBLK:
		if (uflags & MNT_MS_NOFAIL)
			return MOUNT_EX_SUCCESS;

		if (stat(src, &st))
			warnx(_("%s is not a block device, and stat(2) fails?"), src);
		else if (S_ISBLK(st.st_mode))
			warnx(_("the kernel does not recognize %s as a block device\n"
				"       (maybe `modprobe driver'?)"), src);
		else if (S_ISREG(st.st_mode))
			warnx(_("%s is not a block device (maybe try `-o loop'?)"), src);
		else
			warnx(_(" %s is not a block device"), src);
		break;

	case ENXIO:
		if (uflags & MNT_MS_NOFAIL)
			return MOUNT_EX_SUCCESS;

		warnx(_("%s is not a valid block device"), src);
		break;

	case EACCES:
	case EROFS:
		if (mflags & MS_RDONLY)
			warnx(_("cannot mount %s read-only"), src);

		else if (readwrite)
			warnx(_("%s is write-protected but explicit `-w' flag given"), src);

		else if (mflags & MS_REMOUNT)
			warnx(_("cannot remount %s read-write, is write-protected"), src);

		else if (mflags & MS_BIND)
			warn(_("mount %s on %s failed"), src, tgt);

		else {
			warnx(_("%s is write-protected, mounting read-only"), src);

			mnt_context_reset_status(cxt);
			mnt_context_set_mflags(cxt, mflags | MS_RDONLY);
			rc = mnt_context_do_mount(cxt);
			if (!rc)
				rc = mnt_context_finalize_mount(cxt);

			goto try_readonly;
		}
		break;

	case ENOMEDIUM:
		warnx(_("no medium found on %s"), src);
		break;

	default:
		warn(_("mount %s on %s failed"), src, tgt);
		break;
	}

	return MOUNT_EX_FAIL;
}

static struct libmnt_table *append_fstab(struct libmnt_context *cxt,
					 struct libmnt_table *fstab,
					 const char *path)
{

	if (!fstab) {
		fstab = mnt_new_table();
		if (!fstab)
			err(MOUNT_EX_SYSERR, _("failed to initialize libmount table"));

		mnt_table_set_parser_errcb(fstab, table_parser_errcb);
		mnt_context_set_fstab(cxt, fstab);

		mnt_unref_table(fstab);	/* reference is handled by @cxt now */
	}

	if (mnt_table_parse_fstab(fstab, path))
		errx(MOUNT_EX_USAGE,_("%s: failed to parse"), path);

	return fstab;
}

/*
 * Check source and target paths -- non-root user should not be able to
 * resolve paths which are unreadable for him.
 */
static void sanitize_paths(struct libmnt_context *cxt)
{
	const char *p;
	struct libmnt_fs *fs = mnt_context_get_fs(cxt);

	if (!fs)
		return;

	p = mnt_fs_get_target(fs);
	if (p) {
		char *np = canonicalize_path_restricted(p);
		if (!np)
			err(MOUNT_EX_USAGE, "%s", p);
		mnt_fs_set_target(fs, np);
		free(np);
	}

	p = mnt_fs_get_srcpath(fs);
	if (p) {
		char *np = canonicalize_path_restricted(p);
		if (!np)
			err(MOUNT_EX_USAGE, "%s", p);
		mnt_fs_set_source(fs, np);
		free(np);
	}
}

static void append_option(struct libmnt_context *cxt, const char *opt)
{
	if (opt && (*opt == '=' || *opt == '\'' || *opt == '\"' || isblank(*opt)))
		errx(MOUNT_EX_USAGE, _("unsupported option format: %s"), opt);
	if (mnt_context_append_options(cxt, opt))
		err(MOUNT_EX_SYSERR, _("failed to append option '%s'"), opt);
}

static int has_remount_flag(struct libmnt_context *cxt)
{
	unsigned long mflags = 0;

	if (mnt_context_get_mflags(cxt, &mflags))
		return 0;

	return mflags & MS_REMOUNT;
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(
		" %1$s [-lhV]\n"
		" %1$s -a [options]\n"
		" %1$s [options] [--source] <source> | [--target] <directory>\n"
		" %1$s [options] <source> <directory>\n"
		" %1$s <operation> <mountpoint> [<target>]\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Mount a filesystem.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fprintf(out, _(
	" -a, --all               mount all filesystems mentioned in fstab\n"
	" -c, --no-canonicalize   don't canonicalize paths\n"
	" -f, --fake              dry run; skip the mount(2) syscall\n"
	" -F, --fork              fork off for each device (use with -a)\n"
	" -T, --fstab <path>      alternative file to /etc/fstab\n"));
	fprintf(out, _(
	" -i, --internal-only     don't call the mount.<type> helpers\n"));
	fprintf(out, _(
	" -l, --show-labels       show also filesystem labels\n"));
	fprintf(out, _(
	" -n, --no-mtab           don't write to /etc/mtab\n"));
	fprintf(out, _(
	" -o, --options <list>    comma-separated list of mount options\n"
	" -O, --test-opts <list>  limit the set of filesystems (use with -a)\n"
	" -r, --read-only         mount the filesystem read-only (same as -o ro)\n"
	" -t, --types <list>      limit the set of filesystem types\n"));
	fprintf(out, _(
	"     --source <src>      explicitly specifies source (path, label, uuid)\n"
	"     --target <target>   explicitly specifies mountpoint\n"));
	fprintf(out, _(
	" -v, --verbose           say what is being done\n"));
	fprintf(out, _(
	" -w, --rw, --read-write  mount the filesystem read-write (default)\n"));

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	fprintf(out, _(
	"\nSource:\n"
	" -L, --label <label>     synonym for LABEL=<label>\n"
	" -U, --uuid <uuid>       synonym for UUID=<uuid>\n"
	" LABEL=<label>           specifies device by filesystem label\n"
	" UUID=<uuid>             specifies device by filesystem UUID\n"
	" PARTLABEL=<label>       specifies device by partition label\n"
	" PARTUUID=<uuid>         specifies device by partition UUID\n"));

	fprintf(out, _(
	" <device>                specifies device by path\n"
	" <directory>             mountpoint for bind mounts (see --bind/rbind)\n"
	" <file>                  regular file for loopdev setup\n"));

	fprintf(out, _(
	"\nOperations:\n"
	" -B, --bind              mount a subtree somewhere else (same as -o bind)\n"
	" -M, --move              move a subtree to some other place\n"
	" -R, --rbind             mount a subtree and all submounts somewhere else\n"));
	fprintf(out, _(
	" --make-shared           mark a subtree as shared\n"
	" --make-slave            mark a subtree as slave\n"
	" --make-private          mark a subtree as private\n"
	" --make-unbindable       mark a subtree as unbindable\n"));
	fprintf(out, _(
	" --make-rshared          recursively mark a whole subtree as shared\n"
	" --make-rslave           recursively mark a whole subtree as slave\n"
	" --make-rprivate         recursively mark a whole subtree as private\n"
	" --make-runbindable      recursively mark a whole subtree as unbindable\n"));

	fprintf(out, USAGE_MAN_TAIL("mount(8)"));

	exit(out == stderr ? MOUNT_EX_USAGE : MOUNT_EX_SUCCESS);
}

int main(int argc, char **argv)
{
	int c, rc = MOUNT_EX_SUCCESS, all = 0, show_labels = 0;
	struct libmnt_context *cxt;
	struct libmnt_table *fstab = NULL;
	char *srcbuf = NULL;
	char *types = NULL;
	unsigned long oper = 0;
	int propa = 0;

	enum {
		MOUNT_OPT_SHARED = CHAR_MAX + 1,
		MOUNT_OPT_SLAVE,
		MOUNT_OPT_PRIVATE,
		MOUNT_OPT_UNBINDABLE,
		MOUNT_OPT_RSHARED,
		MOUNT_OPT_RSLAVE,
		MOUNT_OPT_RPRIVATE,
		MOUNT_OPT_RUNBINDABLE,
		MOUNT_OPT_TARGET,
		MOUNT_OPT_SOURCE
	};

	static const struct option longopts[] = {
		{ "all", 0, 0, 'a' },
		{ "fake", 0, 0, 'f' },
		{ "fstab", 1, 0, 'T' },
		{ "fork", 0, 0, 'F' },
		{ "help", 0, 0, 'h' },
		{ "no-mtab", 0, 0, 'n' },
		{ "read-only", 0, 0, 'r' },
		{ "ro", 0, 0, 'r' },
		{ "verbose", 0, 0, 'v' },
		{ "version", 0, 0, 'V' },
		{ "read-write", 0, 0, 'w' },
		{ "rw", 0, 0, 'w' },
		{ "options", 1, 0, 'o' },
		{ "test-opts", 1, 0, 'O' },
		{ "types", 1, 0, 't' },
		{ "uuid", 1, 0, 'U' },
		{ "label", 1, 0, 'L'},
		{ "bind", 0, 0, 'B' },
		{ "move", 0, 0, 'M' },
		{ "rbind", 0, 0, 'R' },
		{ "make-shared", 0, 0, MOUNT_OPT_SHARED },
		{ "make-slave", 0, 0, MOUNT_OPT_SLAVE },
		{ "make-private", 0, 0, MOUNT_OPT_PRIVATE },
		{ "make-unbindable", 0, 0, MOUNT_OPT_UNBINDABLE },
		{ "make-rshared", 0, 0, MOUNT_OPT_RSHARED },
		{ "make-rslave", 0, 0, MOUNT_OPT_RSLAVE },
		{ "make-rprivate", 0, 0, MOUNT_OPT_RPRIVATE },
		{ "make-runbindable", 0, 0, MOUNT_OPT_RUNBINDABLE },
		{ "no-canonicalize", 0, 0, 'c' },
		{ "internal-only", 0, 0, 'i' },
		{ "show-labels", 0, 0, 'l' },
		{ "target", 1, 0, MOUNT_OPT_TARGET },
		{ "source", 1, 0, MOUNT_OPT_SOURCE },
		{ NULL, 0, 0, 0 }
	};

	static const ul_excl_t excl[] = {       /* rows and cols in in ASCII order */
		{ 'B','M','R' },			/* bind,move,rbind */
		{ 'L','U', MOUNT_OPT_SOURCE },	/* label,uuid,source */
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

	while ((c = getopt_long(argc, argv, "aBcfFhilL:Mno:O:rRsU:vVwt:T:",
					longopts, NULL)) != -1) {

		/* only few options are allowed for non-root users */
		if (mnt_context_is_restricted(cxt) &&
		    !strchr("hlLUVvrist", c) &&
		    c != MOUNT_OPT_TARGET &&
		    c != MOUNT_OPT_SOURCE)
			exit_non_root(option_to_longopt(c, longopts));

		err_exclusive_options(c, longopts, excl, excl_st);

		switch(c) {
		case 'a':
			all = 1;
			break;
		case 'c':
			mnt_context_disable_canonicalize(cxt, TRUE);
			break;
		case 'f':
			mnt_context_enable_fake(cxt, TRUE);
			break;
		case 'F':
			mnt_context_enable_fork(cxt, TRUE);
			break;
		case 'h':
			usage(stdout);
			break;
		case 'i':
			mnt_context_disable_helpers(cxt, TRUE);
			break;
		case 'n':
			mnt_context_disable_mtab(cxt, TRUE);
			break;
		case 'r':
			append_option(cxt, "ro");
			readwrite = 0;
			break;
		case 'v':
			mnt_context_enable_verbose(cxt, TRUE);
			break;
		case 'V':
			print_version();
			break;
		case 'w':
			append_option(cxt, "rw");
			readwrite = 1;
			break;
		case 'o':
			append_option(cxt, optarg);
			break;
		case 'O':
			if (mnt_context_set_options_pattern(cxt, optarg))
				err(MOUNT_EX_SYSERR, _("failed to set options pattern"));
			break;
		case 'L':
			xasprintf(&srcbuf, "LABEL=\"%s\"", optarg);
			mnt_context_disable_swapmatch(cxt, 1);
			mnt_context_set_source(cxt, srcbuf);
			free(srcbuf);
			break;
		case 'U':
			xasprintf(&srcbuf, "UUID=\"%s\"", optarg);
			mnt_context_disable_swapmatch(cxt, 1);
			mnt_context_set_source(cxt, srcbuf);
			free(srcbuf);
			break;
		case 'l':
			show_labels = 1;
			break;
		case 't':
			types = optarg;
			break;
		case 'T':
			fstab = append_fstab(cxt, fstab, optarg);
			break;
		case 's':
			mnt_context_enable_sloppy(cxt, TRUE);
			break;
		case 'B':
			oper |= MS_BIND;
			break;
		case 'M':
			oper |= MS_MOVE;
			break;
		case 'R':
			oper |= (MS_BIND | MS_REC);
			break;
		case MOUNT_OPT_SHARED:
			append_option(cxt, "shared");
			propa = 1;
			break;
		case MOUNT_OPT_SLAVE:
			append_option(cxt, "slave");
			propa = 1;
			break;
		case MOUNT_OPT_PRIVATE:
			append_option(cxt, "private");
			propa = 1;
			break;
		case MOUNT_OPT_UNBINDABLE:
			append_option(cxt, "unbindable");
			propa = 1;
			break;
		case MOUNT_OPT_RSHARED:
			append_option(cxt, "rshared");
			propa = 1;
			break;
		case MOUNT_OPT_RSLAVE:
			append_option(cxt, "rslave");
			propa = 1;
			break;
		case MOUNT_OPT_RPRIVATE:
			append_option(cxt, "rprivate");
			propa = 1;
			break;
		case MOUNT_OPT_RUNBINDABLE:
			append_option(cxt, "runbindable");
			propa = 1;
			break;
		case MOUNT_OPT_TARGET:
			mnt_context_disable_swapmatch(cxt, 1);
			mnt_context_set_target(cxt, optarg);
			break;
		case MOUNT_OPT_SOURCE:
			mnt_context_disable_swapmatch(cxt, 1);
			mnt_context_set_source(cxt, optarg);
			break;
		default:
			usage(stderr);
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (fstab && !mnt_context_is_nocanonicalize(cxt)) {
		/*
		 * We have external (context independent) fstab instance, let's
		 * make a connection between the fstab and the canonicalization
		 * cache.
		 */
		mnt_table_set_cache(fstab, mnt_context_get_cache(cxt));
	}

	if (!mnt_context_get_source(cxt) &&
	    !mnt_context_get_target(cxt) &&
	    !argc &&
	    !all) {
		if (oper || mnt_context_get_options(cxt))
			usage(stderr);
		print_all(cxt, types, show_labels);
		goto done;
	}

	/* Non-root users are allowed to use -t to print_all(),
	   but not to mount */
	if (mnt_context_is_restricted(cxt) && types)
		exit_non_root("types");

	if (oper && (types || all || mnt_context_get_source(cxt)))
		usage(stderr);

	if (types && (all || strchr(types, ',') ||
			     strncmp(types, "no", 2) == 0))
		mnt_context_set_fstype_pattern(cxt, types);
	else if (types)
		mnt_context_set_fstype(cxt, types);

	if (all) {
		/*
		 * A) Mount all
		 */
		rc = mount_all(cxt);
		goto done;

	} else if (argc == 0 && (mnt_context_get_source(cxt) ||
				 mnt_context_get_target(cxt))) {
		/*
		 * B) mount -L|-U|--source|--target
		 *
		 * non-root may specify source *or* target, but not both
		 */
		if (mnt_context_is_restricted(cxt) &&
		    mnt_context_get_source(cxt) &&
		    mnt_context_get_target(cxt))
			exit_non_root(NULL);

	} else if (argc == 1 && (!mnt_context_get_source(cxt) ||
				 !mnt_context_get_target(cxt))) {
		/*
		 * C) mount [-L|-U|--source] <target>
		 *    mount [--target <dir>] <source>
		 *    mount <source|target>
		 *
		 * non-root may specify source *or* target, but not both
		 *
		 * It does not matter for libmount if we set source or target
		 * here (the library is able to swap it), but it matters for
		 * sanitize_paths().
		 */
		int istag = mnt_tag_is_valid(argv[0]);

		if (istag && mnt_context_get_source(cxt))
			/* -L, -U or --source together with LABEL= or UUID= */
			errx(MOUNT_EX_USAGE, _("source specified more than once"));
		else if (istag || mnt_context_get_target(cxt))
			mnt_context_set_source(cxt, argv[0]);
		else
			mnt_context_set_target(cxt, argv[0]);

		if (mnt_context_is_restricted(cxt) &&
		    mnt_context_get_source(cxt) &&
		    mnt_context_get_target(cxt))
			exit_non_root(NULL);

	} else if (argc == 2 && !mnt_context_get_source(cxt)
			     && !mnt_context_get_target(cxt)) {
		/*
		 * D) mount <source> <target>
		 */
		if (mnt_context_is_restricted(cxt))
			exit_non_root(NULL);

		mnt_context_set_source(cxt, argv[0]);
		mnt_context_set_target(cxt, argv[1]);

	} else
		usage(stderr);

	if (mnt_context_is_restricted(cxt))
		sanitize_paths(cxt);

	if (oper)
		/* BIND/MOVE operations, let's set the mount flags */
		mnt_context_set_mflags(cxt, oper);

	if ((oper && !has_remount_flag(cxt)) || propa)
		/* For --make-* or --bind is fstab/mtab unnecessary */
		mnt_context_set_optsmode(cxt, MNT_OMODE_NOTAB);

	rc = mnt_context_mount(cxt);
	rc = mk_exit_code(cxt, rc);

	if (rc == MOUNT_EX_SUCCESS && mnt_context_is_verbose(cxt))
		success_message(cxt);
done:
	mnt_free_context(cxt);
	return rc;
}

