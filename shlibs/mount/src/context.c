/*
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/wait.h>
#include <sys/mount.h>

#ifdef HAVE_LIBSELINUX
#include <selinux/selinux.h>
#include <selinux/context.h>
#endif

#include "c.h"
#include "mountP.h"

/*
 * Mount context -- high-level API
 */
struct _mnt_context
{
	int	action;		/* MNT_ACT_{MOUNT,UMOUNT} */

	int	restricted;	/* root or not? */

	char	*fstype_pattern;	/* for mnt_match_fstype() */
	char	*optstr_pattern;	/* for mnt_match_options() */

	char	*spec;		/* unresolved source OR target */
	mnt_fs	*fs;		/* filesystem description (type, mountpopint, device, ...) */

	mnt_tab	*fstab;		/* fstab (or mtab for some remounts) entires */
	mnt_tab *mtab;		/* mtab entries */
	int	optsmode;	/* fstab optstr mode MNT_OPTSMODE_{AUTO,FORCE,IGNORE} */

	unsigned long	mountflags;	/* final mount(2) flags */
	const void	*mountdata;	/* final mount(2) data, string or binary data */

	unsigned long	user_mountflags;	/* MNT_MS_* (loop=, user=, ...) */

	mnt_update	*update;	/* mtab update */
	mnt_cache	*cache;		/* paths cache */

	int	flags;		/* private context flags */
	int	ambi;		/* libblkid returns ambivalent result */

	char	*helper;	/* name of the used /sbin/[u]mount.<type> helper */
	int	helper_status;	/* helper wait(2) status */

	char	*orig_user;	/* original (non-fixed) user= option */

	int	syscall_errno;	/* mount(2) or umount(2) error */
};

/* flags */
#define MNT_FL_NOMTAB		(1 << 1)
#define MNT_FL_FAKE		(1 << 2)
#define MNT_FL_SLOPPY		(1 << 3)
#define MNT_FL_VERBOSE		(1 << 4)
#define MNT_FL_NOHELPERS	(1 << 5)
#define MNT_FL_LOOPDEL		(1 << 6)
#define MNT_FL_LAZY		(1 << 7)
#define MNT_FL_FORCE		(1 << 8)
#define MNT_FL_NOCANONICALIZE	(1 << 9)
#define MNT_FL_NOLOCK		(1 << 10)	/* don't lock mtab file */

#define MNT_FL_EXTERN_FS	(1 << 15)	/* cxt->fs is not private */
#define MNT_FL_EXTERN_FSTAB	(1 << 16)	/* cxt->fstab is not private */
#define MNT_FL_EXTERN_CACHE	(1 << 17)	/* cxt->cache is not private */

#define MNT_FL_MOUNTDATA	(1 << 20)
#define MNT_FL_FSTAB_APPLIED	(1 << 21)
#define MNT_FL_MOUNTFLAGS_MERGED (1 << 22)	/* MS_* flags was read from optstr */
#define MNT_FL_SAVED_USER	(1 << 23)

/* default flags */
#define MNT_FL_DEFAULT		0

/* mode for mount options from fstab */
enum {
	MNT_OPTSMODE_AUTO = 0,		/* use options if source or target are not defined */
	MNT_OPTSMODE_IGNORE,		/* never use mount options from fstab */
	MNT_OPTSMODE_FORCE,		/* always use mount options from fstab */
	MNT_OPTSMODE_MTABFORCE,		/* for MS_REMOUNT use always options mountinfo/mtab */
};

/**
 * mnt_new_context:
 *
 * Returns: newly allocated mount context
 */
mnt_context *mnt_new_context()
{
	mnt_context *cxt;
	uid_t ruid, euid;

	cxt = calloc(1, sizeof(*cxt));
	if (!cxt)
		return NULL;

	ruid = getuid();
	euid = geteuid();

	/* if we're really root and aren't running setuid */
	cxt->restricted = (uid_t) 0 == ruid && ruid == euid ? 0 : 1;

	DBG(CXT, mnt_debug_h(cxt, "allocate %s",
				cxt->restricted ? "[RESTRICTED]" : ""));

	return cxt;
}

/**
 * mnt_free_context:
 * @cxt: mount context
 *
 * Deallocates context struct.
 */
void mnt_free_context(mnt_context *cxt)
{
	if (!cxt)
		return;

	DBG(CXT, mnt_debug_h(cxt, "free"));

	free(cxt->fstype_pattern);
	free(cxt->optstr_pattern);
	free(cxt->spec);
	free(cxt->helper);
	free(cxt->orig_user);

	if (!(cxt->flags & MNT_FL_EXTERN_FS))
		mnt_free_fs(cxt->fs);
	if (!(cxt->flags & MNT_FL_EXTERN_FSTAB))
		mnt_free_tab(cxt->fstab);
	if (!(cxt->flags & MNT_FL_EXTERN_CACHE))
		mnt_free_cache(cxt->cache);

	mnt_free_update(cxt->update);
	mnt_free_tab(cxt->mtab);

	free(cxt);
}

/**
 * mnt_reset_context:
 * @cxt: mount context
 *
 * Resets all information in the context that are directly related to
 * the latest mount (spec, source, target, mount options, ....)
 *
 * The match patters, cached fstab, cached canonicalized paths and tags and
 * [e]uid are not reseted. You have to use
 *
 *	mnt_context_set_fstab(cxt, NULL);
 *	mnt_context_set_cache(cxt, NULL);
 *	mnt_context_set_fstype_pattern(cxt, NULL);
 *	mnt_context_set_optstr_pattern(cxt, NULL);
 *
 * to reset these stuff.
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_reset_context(mnt_context *cxt)
{
	int fl;

	if (!cxt)
		return -EINVAL;

	fl = cxt->flags;

	if (cxt->update)
		mnt_update_set_fs(cxt->update, NULL);

	if (!(cxt->flags & MNT_FL_EXTERN_FS))
		mnt_free_fs(cxt->fs);

	mnt_free_tab(cxt->mtab);

	cxt->fs = NULL;
	cxt->mtab = NULL;

	free(cxt->spec);
	free(cxt->helper);
	free(cxt->orig_user);

	cxt->spec = NULL;
	cxt->helper = NULL;

	cxt->mountflags = 0;
	cxt->user_mountflags = 0;
	cxt->mountdata = NULL;
	cxt->flags = MNT_FL_DEFAULT;
	cxt->syscall_errno = 0;
	cxt->helper_status = 0;

	/* restore non-resetable flags */
	cxt->flags |= (fl & MNT_FL_EXTERN_FSTAB);
	cxt->flags |= (fl & MNT_FL_EXTERN_CACHE);

	return 0;
}

static int mnt_context_set_flag(mnt_context *cxt, int flag, int enable)
{
	if (!cxt)
		return -EINVAL;
	if (enable)
		cxt->flags |= flag;
	else
		cxt->flags &= ~flag;
	return 0;
}

static mnt_fs *mnt_context_get_fs(mnt_context *cxt)
{
	if (!cxt)
		return NULL;
	if (!cxt->fs) {
		cxt->fs = mnt_new_fs();
		cxt->flags &= ~MNT_FL_EXTERN_FS;
	}
	return cxt->fs;
}

/**
 * mnt_context_is_restricted:
 * @cxt: mount context
 *
 * Returns: 0 for unrestricted mount (user is root), or 1 for non-root mounts
 */
int mnt_context_is_restricted(mnt_context *cxt)
{
	assert(cxt);
	return cxt->restricted;
}

/**
 * mnt_context_set_optsmode
 * @cxt: mount context
 * @mode: MNT_OPTSMODE_{AUTO,FORCE,IGNORE,MTABFORCE}
 *
 * Defines a mode how libmount uses fstab mount options:
 *
 *  auto       - use options from fstab if source or target are not
 *               defined (this is mount(8) default).
 *
 *             - For remount operation it reads options from mtab if
 *               the target is not found in fstab.
 *
 *  ignore     - never use mount options from fstab
 *
 *  force      - always use mount options from fstab
 *
 *  mtab-force - for remount operation always use options from mtab (mountinfo)
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_set_optsmode(mnt_context *cxt, int mode)
{
	if (!cxt)
		return -EINVAL;
	cxt->optsmode = mode;
	return 0;
}

/**
 * mnt_context_disable_canonicalize:
 * @cxt: mount context
 * @disable: TRUE or FALSE
 *
 * Enable/disable paths canonicalization and tags evaluation. The libmount context
 * canonicalies paths when search in fstab and when prepare source and target paths
 * for mount(2) syscall.
 *
 * This fuction has effect to the private fstab instance only (see
 * mnt_context_set_fstab()). If you want to use an external fstab then you need
 * manage your private mnt_cache (see mnt_tab_set_cache(fstab, NULL).
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_disable_canonicalize(mnt_context *cxt, int disable)
{
	return mnt_context_set_flag(cxt, MNT_FL_NOCANONICALIZE, disable);
}

/**
 * mnt_context_enable_lazy:
 * @cxt: mount context
 * @enable: TRUE or FALSE
 *
 * Enable/disable lazy umount (see umount(8) man page, option -l).
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_enable_lazy(mnt_context *cxt, int enable)
{
	return mnt_context_set_flag(cxt, MNT_FL_LAZY, enable);
}

/**
 * mnt_context_disable_helpers:
 * @cxt: mount context
 * @disable: TRUE or FALSE
 *
 * Enable/disable /sbin/[u]mount.* helpers (see mount(8) man page, option -i).
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_disable_helpers(mnt_context *cxt, int disable)
{
	return mnt_context_set_flag(cxt, MNT_FL_NOHELPERS, disable);
}

/**
 * mnt_context_enable_sloppy:
 * @cxt: mount context
 * @enable: TRUE or FALSE
 *
 * Set/unset sloppy mounting (see mount(8) man page, option -s).
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_enable_sloppy(mnt_context *cxt, int enable)
{
	return mnt_context_set_flag(cxt, MNT_FL_SLOPPY, enable);
}

/**
 * mnt_context_enable_fake:
 * @cxt: mount context
 * @enable: TRUE or FALSE
 *
 * Enable/disable fake mounting (see mount(8) man page, option -f).
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_enable_fake(mnt_context *cxt, int enable)
{
	return mnt_context_set_flag(cxt, MNT_FL_FAKE, enable);
}

/**
 * mnt_context_disable_mtab:
 * @cxt: mount context
 * @disable: TRUE or FALSE
 *
 * Disable/enable mtab update (see mount(8) man page, option -n).
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_disable_mtab(mnt_context *cxt, int disable)
{
	return mnt_context_set_flag(cxt, MNT_FL_NOMTAB, disable);
}

/**
 * mnt_context_disable_lock:
 * @cxt: mount context
 * @disable: TRUE or FALSE
 *
 * Disable/enable mtab lock.
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_disable_lock(mnt_context *cxt, int disable)
{
	return mnt_context_set_flag(cxt, MNT_FL_NOLOCK, disable);
}

/**
 * mnt_context_enable_force:
 * @cxt: mount context
 * @enable: TRUE or FALSE
 *
 * Enable/disable force umounting (see umount(8) man page, option -f).
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_enable_force(mnt_context *cxt, int enable)
{
	return mnt_context_set_flag(cxt, MNT_FL_FORCE, enable);
}

/**
 * mnt_context_enable_verbose:
 * @cxt: mount context
 * @enable: TRUE or FALSE
 *
 * Enable/disable verbose output.
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_enable_verbose(mnt_context *cxt, int enable)
{
	return mnt_context_set_flag(cxt, MNT_FL_VERBOSE, enable);
}

/**
 * mnt_context_enable_loopdel:
 * @cxt: mount context
 * @enable: TRUE or FALSE
 *
 * Enable/disable loop delete (destroy) after umount (see umount(8), option -d)
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_enable_loopdel(mnt_context *cxt, int enable)
{
	return mnt_context_set_flag(cxt, MNT_FL_LOOPDEL, enable);
}

/**
 * mnt_context_set_spec:
 * @cxt: mount context
 * @spec: unresolved source (device, label, uuid, ...) or target (mountpoint)
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_set_spec(mnt_context *cxt, const char *spec)
{
	char *p;

	if (!cxt)
		return -EINVAL;
	p = strdup(spec);
	if (!p)
		return -ENOMEM;
	free(cxt->spec);
	cxt->spec = p;
	return 0;
}

/**
 * mnt_context_set_fs:
 * @cxt: mount context
 * @fs: filesystem description
 *
 * The mount context uses private @fs by default. This function allows to
 * overwrite the private @fs with an external instance. Note that the external
 * @fs instance is not deallocated by mnt_free_context() or mnt_reset_context().
 *
 * The @fs will be modified by mnt_context_set_{source,target,optstr,fstype}
 * functions, Ft the @fs is NULL then all current FS specific setting (source,
 * target, etc., exclude spec) is reseted.
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_set_fs(mnt_context *cxt, mnt_fs *fs)
{
	if (!cxt)
		return -EINVAL;
	if (!(cxt->flags & MNT_FL_EXTERN_FS))
		mnt_free_fs(cxt->fs);

	mnt_context_set_flag(cxt, MNT_FL_EXTERN_FS, fs != NULL);
	cxt->fs = fs;
	return 0;
}

/**
 * mnt_context_set_source:
 * @cxt: mount context
 * @source: mount source (device, directory, UUID, LABEL, ...)
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_set_source(mnt_context *cxt, const char *source)
{
	return mnt_fs_set_source(mnt_context_get_fs(cxt), source);
}

/**
 * mnt_context_set_target:
 * @cxt: mount context
 * @target: mountpoint
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_set_target(mnt_context *cxt, const char *target)
{
	return mnt_fs_set_target(mnt_context_get_fs(cxt), target);
}

/**
 * mnt_context_set_fstype:
 * @cxt: mount context
 * @fstype: filesystem type
 *
 * Note that the @fstype has to be the real FS type. For comma-separated list of
 * filesystems or for "no<fs>" notation use mnt_context_set_fstype_pattern().
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_set_fstype(mnt_context *cxt, const char *fstype)
{
	return mnt_fs_set_fstype(mnt_context_get_fs(cxt), fstype);
}

/**
 * mnt_context_set_optstr:
 * @cxt: mount context
 * @optstr: comma delimited mount options
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_set_optstr(mnt_context *cxt, const char *optstr)
{
	return mnt_fs_set_optstr(mnt_context_get_fs(cxt), optstr);
}

/**
 * mnt_context_append_optstr:
 * @cxt: mount context
 * @optstr: comma delimited mount options
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_append_optstr(mnt_context *cxt, const char *optstr)
{
	return mnt_fs_append_optstr(mnt_context_get_fs(cxt), optstr);
}

/**
 * mnt_context_set_fstype_pattern:
 * @cxt: mount context
 * @pattern: FS name pattern (or NULL to reset the current setting)
 *
 * See mount(8), option -t.
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_set_fstype_pattern(mnt_context *cxt, const char *pattern)
{
	char *p = NULL;

	if (!cxt)
		return -EINVAL;
	if (pattern) {
		p = strdup(pattern);
		if (!p)
			return -ENOMEM;
	}
	free(cxt->fstype_pattern);
	cxt->fstype_pattern = p;
	return 0;
}

/**
 * mnt_context_set_optstr_pattern:
 * @cxt: mount context
 * @pattern: options pattern (or NULL to reset the current setting)
 *
 * See mount(8), option -O.
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_set_optstr_pattern(mnt_context *cxt, const char *pattern)
{
	char *p = NULL;

	if (!cxt)
		return -EINVAL;
	if (pattern) {
		p = strdup(pattern);
		if (!p)
			return -ENOMEM;
	}
	free(cxt->optstr_pattern);
	cxt->optstr_pattern = p;
	return 0;
}

/**
 * mnt_context_set_fstab:
 * @cxt: mount context
 * @tb: fstab
 *
 * The mount context reads /etc/fstab to the the private mnt_tab by default.
 * This function allows to overwrite the private fstab with an external
 * instance. Note that the external instance is not deallocated by mnt_free_context().
 *
 * The fstab is used read-only and is not modified, it should be possible to
 * share the fstab between more mount contexts (TODO: tests it.)
 *
 * If the @tb argument is NULL then the current private fstab instance is
 * reseted.
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_set_fstab(mnt_context *cxt, mnt_tab *tb)
{
	if (!cxt)
		return -EINVAL;
	if (!(cxt->flags & MNT_FL_EXTERN_FSTAB))
		mnt_free_tab(cxt->fstab);

	mnt_context_set_flag(cxt, MNT_FL_EXTERN_FSTAB, tb != NULL);
	cxt->fstab = tb;
	return 0;
}

/**
 * mnt_context_set_cache:
 * @cxt: mount context
 * @cache: cache instance or nULL
 *
 * The mount context maintains a private mnt_cache by default.  This function
 * allows to overwrite the private cache with an external instance. Note that
 * the external instance is not deallocated by mnt_free_context().
 *
 * If the @cache argument is NULL then the current private cache instance is
 * reseted.
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_set_cache(mnt_context *cxt, mnt_cache *cache)
{
	if (!cxt)
		return -EINVAL;
	if (!(cxt->flags & MNT_FL_EXTERN_CACHE))
		mnt_free_cache(cxt->cache);

	mnt_context_set_flag(cxt, MNT_FL_EXTERN_CACHE, cache != NULL);
	cxt->cache = cache;
	return 0;
}

static mnt_cache *mnt_context_get_cache(mnt_context *cxt)
{
	if (!cxt || (cxt->flags & MNT_FL_NOCANONICALIZE))
		return NULL;

	if (!cxt->cache) {
		cxt->cache = mnt_new_cache();
		if (!cxt->cache)
			return NULL;
		cxt->flags &= ~MNT_FL_EXTERN_CACHE;
	}
	return cxt->cache;
}

/**
 * mnt_context_set_mountflags:
 * @cxt: mount context
 * @flags: mount(2) flags (MS_* flags)
 *
 * Note that mount context allows to define mount options by mount flags. It
 * means you can for example use
 *
 *	mnt_context_set_mountflags(cxt, MS_NOEXEC | MS_NOSUID);
 *
 * rather than
 *
 *	mnt_context_set_optstr(cxt, "noexec,nosuid");
 *
 * these both calls have the same effect.
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_set_mountflags(mnt_context *cxt, unsigned long flags)
{
	if (!cxt)
		return -EINVAL;
	cxt->mountflags = flags;
	return 0;
}

/**
 * mnt_context_get_mountflags:
 * @cxt: mount context
 * @flags: returns mount flags
 *
 * Converts mount options string to MS_* flags and bitewise-OR the result with
 * already defined flags (see mnt_context_set_mountflags()).
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_get_mountflags(mnt_context *cxt, unsigned long *flags)
{
	int rc = 0;
	if (!cxt || !flags)
		return -EINVAL;

	*flags = 0;
	if (!(cxt->flags & MNT_FL_MOUNTFLAGS_MERGED) && cxt->fs) {
		const char *o = mnt_fs_get_optstr(cxt->fs);
		if (o)
			rc = mnt_optstr_get_mountflags(o, flags);
	}
	if (!rc)
		*flags |= cxt->mountflags;
	return rc;
}

static int mnt_context_is_remount(mnt_context *cxt)
{
	unsigned long fl = 0;

	if (cxt->mountflags & MS_REMOUNT)
		return 1;
	if (!mnt_context_get_mountflags(cxt, &fl) && (fl & MS_REMOUNT))
		return 1;
	return 0;
}

/**
 * mnt_context_set_userspace_mountflags:
 * @cxt: mount context
 * @flags: mount(2) flags (MNT_MS_* flags, e.g. MNT_MS_LOOP)
 *
 * See also notest for mnt_context_set_mountflags().
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_set_userspace_mountflags(mnt_context *cxt, unsigned long flags)
{
	if (!cxt)
		return -EINVAL;
	cxt->user_mountflags = flags;
	return 0;
}

/**
 * mnt_context_get_userspace_mountflags:
 * @cxt: mount context
 * @flags: returns mount flags
 *
 * Converts mount options string to MNT_MS_* flags and bitewise-OR the result with
 * already defined flags (see mnt_context_set_userspace_mountflags()).
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_get_userspace_mountflags(mnt_context *cxt, unsigned long *flags)
{
	int rc = 0;
	if (!cxt || !flags)
		return -EINVAL;

	*flags = 0;
	if (!(cxt->flags & MNT_FL_MOUNTFLAGS_MERGED) && cxt->fs) {
		const char *o = mnt_fs_get_optstr(cxt->fs);
		if (o)
			rc = mnt_optstr_get_userspace_mountflags(o, flags);
	}
	if (!rc)
		*flags |= cxt->user_mountflags;
	return rc;
}

static int mnt_context_is_loop(mnt_context *cxt)
{
	unsigned long fl = 0;

	if (cxt->user_mountflags & MNT_MS_LOOP)
		return 1;
	if (!mnt_context_get_mountflags(cxt, &fl) && (fl & MNT_MS_LOOP))
		return 1;

	/* TODO:
	 *	- support MNT_MS_{OFFSET,SIZELIMIT,ENCRYPTION}
	 */
	return 0;
}


/**
 * mnt_context_set_mountdata:
 * @cxt: mount context
 * @data: mount(2) data
 *
 * The mount context generates mountdata from mount options by default. This
 * function allows to overwrite this behavior, and @data will be used instead
 * of mount options.
 *
 * The libmount does not deallocated the data by mnt_free_context(). Note that
 * NULL is also valid mount data.
 *
 * Returns: 0 on success, negative number in case of error.
 */
int mnt_context_set_mountdata(mnt_context *cxt, void *data)
{
	if (!cxt)
		return -EINVAL;
	cxt->mountdata = data;
	cxt->flags |= MNT_FL_MOUNTDATA;
	return 0;
}

static int mnt_context_apply_tab(mnt_context *cxt, mnt_tab *tb)
{
	mnt_fs *fs = NULL;
	const char *src = NULL, *tgt = NULL;
	int rc;

	if (!cxt->fs)
		return -EINVAL;

	src = mnt_fs_get_source(cxt->fs);
	tgt = mnt_fs_get_target(cxt->fs);

	if (tgt && src)
		;	/* TODO: search pair for MNT_OPTSMODE_FORCE */
	else if (src)
		fs = mnt_tab_find_source(tb, src, MNT_ITER_FORWARD);
	else if (tgt)
		fs = mnt_tab_find_target(tb, tgt, MNT_ITER_FORWARD);
	else if (cxt->spec) {
		fs = mnt_tab_find_source(tb, cxt->spec, MNT_ITER_FORWARD);

		if (!fs && (strncmp(cxt->spec, "LABEL=", 6) ||
			    strncmp(cxt->spec, "UUID=", 5)))
			fs = mnt_tab_find_target(tb, cxt->spec, MNT_ITER_FORWARD);
	}

	if (!fs)
		return -EINVAL;

	DBG(CXT, mnt_debug_h(cxt, "apply entry:"));
	DBG(CXT, mnt_fs_print_debug(fs, stderr));

	/* copy from fstab to our FS description
	 */
	rc = mnt_fs_set_source(cxt->fs, mnt_fs_get_source(fs));
	if (!rc)
		rc = mnt_fs_set_target(cxt->fs, mnt_fs_get_target(fs));

	if (!rc && !mnt_fs_get_fstype(cxt->fs))
		rc = mnt_fs_set_fstype(cxt->fs, mnt_fs_get_fstype(fs));

	if (!rc && cxt->optsmode != MNT_OPTSMODE_IGNORE)
		rc = mnt_fs_prepend_optstr(cxt->fs, mnt_fs_get_optstr(fs));

	if (!rc)
		cxt->flags |= MNT_FL_FSTAB_APPLIED;

	return rc;
}

int mnt_context_apply_fstab(mnt_context *cxt)
{
	int rc;
	mnt_cache *cache;
	const char *src = NULL, *tgt = NULL;

	if (!cxt || (!cxt->spec && !cxt->fs))
		return -EINVAL;

	if (cxt->flags & MNT_FL_FSTAB_APPLIED)
		return 0;

	if (cxt->fs) {
		src = mnt_fs_get_source(cxt->fs);
		tgt = mnt_fs_get_target(cxt->fs);
	}

	/* fstab is not required if source and target are specified */
	if (src && tgt && !(cxt->optsmode == MNT_OPTSMODE_FORCE ||
			    cxt->optsmode == MNT_OPTSMODE_MTABFORCE))
		return 0;

	DBG(CXT, mnt_debug_h(cxt,
		"trying to apply fstab (src=%s, target=%s, spec=%s)",
		src, tgt, cxt->spec));

	/* initialize fstab */
	if (!cxt->fstab) {
		cxt->fstab = mnt_new_tab();
		if (!cxt->fstab)
			goto errnomem;
		cxt->flags &= ~MNT_FL_EXTERN_FSTAB;
		rc = mnt_tab_parse_fstab(cxt->fstab);
		if (rc)
			goto err;
	}

	cache = mnt_context_get_cache(cxt);	/* NULL if MNT_FL_NOCANONICALIZE is enabled */

	/*  never touch an external fstab */
	if (!(cxt->flags & MNT_FL_EXTERN_FSTAB))
		mnt_tab_set_cache(cxt->fstab, cache);

	/* let's initialize cxt->fs */
	mnt_context_get_fs(cxt);

	/* try fstab */
	rc = mnt_context_apply_tab(cxt, cxt->fstab);

	/* try mtab */
	if (rc || (cxt->optsmode == MNT_OPTSMODE_MTABFORCE &&
		   mnt_context_is_remount(cxt))) {

		cxt->mtab = mnt_new_tab();
		if (!cxt->mtab)
			goto errnomem;
		rc = mnt_tab_parse_mtab(cxt->mtab);
		if (rc)
			goto err;

		mnt_tab_set_cache(cxt->mtab, cache);
		rc = mnt_context_apply_tab(cxt, cxt->mtab);
		if (rc)
			goto err;
	}
	return 0;

errnomem:
	rc = ENOMEM;
err:
	DBG(CXT, mnt_debug_h(cxt, "failed to found entry in fstab/mtab"));
	return rc;
}

/*
 * this has to be called after mnt_context_evaluate_permissions()
 */
static int mnt_context_fix_optstr(mnt_context *cxt)
{
	int rc = 0, rem_se = 0;
	char *next, **optstr;
	char *name, *val;
	size_t namesz, valsz;

	if (!cxt)
		return -EINVAL;
	if (!cxt->fs)
		return 0;

	assert((cxt->flags & MNT_FL_MOUNTFLAGS_MERGED));

	/*
	 * we directly work with optstr pointer here
	 */
	optstr = &cxt->fs->optstr;
	if (!optstr)
		return 0;

	/* The propagation flags should not be used together with any other flags */
	if (cxt->mountflags & MS_PROPAGATION)
		cxt->mountflags &= MS_PROPAGATION;

	if (!mnt_optstr_get_option(*optstr, "user", &val, &valsz)) {
		if (val) {
			cxt->orig_user = strndup(val, valsz);
			if (!cxt->orig_user) {
				rc = -ENOMEM;
				goto done;
			}
		}
		cxt->flags |= MNT_FL_SAVED_USER;
	}

	/*
	 * Sync mount options with mount flags
	 */
	rc = mnt_optstr_apply_flags(optstr, cxt->mountflags,
				mnt_get_builtin_optmap(MNT_LINUX_MAP));
	if (rc)
		goto done;

	rc = mnt_optstr_apply_flags(optstr, cxt->user_mountflags,
				mnt_get_builtin_optmap(MNT_USERSPACE_MAP));
	if (rc)
		goto done;

	next = *optstr;

#ifdef HAVE_LIBSELINUX
	rem_se = (cxt->mountflags & MS_REMOUNT) || !is_selinux_enabled();
#endif
	DBG(CXT, mnt_debug_h(cxt, "fixing mount options: '%s'", *optstr));

	while (!mnt_optstr_next_option(&next, &name, &namesz, &val, &valsz)) {

		if (namesz == 3 && !strncmp(name, "uid", 3))
			rc = mnt_optstr_fix_uid(optstr, val, valsz, &next);
		else if (namesz == 3 && !strncmp(name, "gid", 3))
			rc = mnt_optstr_fix_gid(optstr, val, valsz, &next);
#ifdef HAVE_LIBSELINUX
		else if (namesz >= 7 && (!strncmp(name, "context", 7) ||
					 !strncmp(name, "fscontext", 9) ||
					 !strncmp(name, "defcontext", 10) ||
					 !strncmp(name, "rootcontext", 11))) {
			if (rem_se) {
				/* remove context= option */
				next = name;
				rc = mnt_optstr_remove_option_at(optstr,
							name, val + valsz);
			} else
				rc = mnt_optstr_fix_secontext(optstr,
							val, valsz, &next);
		}
#endif
		else if (namesz == 4 && (cxt->user_mountflags && MNT_MS_USER) &&
			 !strncmp(name, "user", 4)) {

			rc = mnt_optstr_fix_user(optstr,
						 val ? val : name + namesz,
						 valsz, &next);
		}
		if (rc)
			goto done;
	}

done:
	__mnt_fs_set_optstr_ptr(cxt->fs, *optstr, TRUE);
	DBG(CXT, mnt_debug_h(cxt, "fixed options [rc=%d]: '%s'", rc, *optstr));
	return rc;
}

/*
 * Converts already evalulated and fixed options to the form that is comaptible
 * with /sbin/mount.<type> helpers.
 *
 * Retursn newly allocated string.
 */
static int mnt_context_get_helper_optstr(mnt_context *cxt, char **optstr)
{
	const char *o;
	int rc = 0;

	assert(cxt);
	assert(cxt->fs);
	assert(optstr);

	*optstr = NULL;
	o = mnt_fs_get_optstr(cxt->fs);

	if (o)
		rc = mnt_optstr_append_option(optstr, o, NULL);
	if (!rc && (cxt->flags & MNT_FL_SAVED_USER))
		rc = mnt_optstr_set_option(optstr, "user", cxt->orig_user);
	if (rc) {
		free(*optstr);
		*optstr = NULL;
	}
	return rc;
}


/*
 * this has to be called before mnt_context_fix_optstr()
 */
static int mnt_context_evaluate_permissions(mnt_context *cxt)
{
	unsigned long u_flags;
	const char *srcpath;

	if (!cxt)
		return -EINVAL;
	if (!cxt->fs)
		return 0;

	mnt_context_get_userspace_mountflags(cxt, &u_flags);

	if (!mnt_context_is_restricted(cxt)) {
		/*
		 * superuser mount
		 */
		cxt->user_mountflags &= ~MNT_MS_OWNER;
		cxt->user_mountflags &= ~MNT_MS_GROUP;
		cxt->user_mountflags &= ~MNT_MS_USER;
		cxt->user_mountflags &= ~MNT_MS_USERS;
	} else {
		/*
		 * user mount
		 */
		if (u_flags & (MNT_MS_OWNER | MNT_MS_GROUP))
			cxt->mountflags |= MS_OWNERSECURE;

		if (u_flags & (MNT_MS_USER | MNT_MS_USERS))
			cxt->mountflags |= MS_SECURE;

		srcpath = mnt_fs_get_srcpath(cxt->fs);
		if (!srcpath)
			return -EINVAL;

		/*
		 * MS_OWNER: Allow owners to mount when fstab contains the
		 * owner option.  Note that this should never be used in a high
		 * security environment, but may be useful to give people at
		 * the console the possibility of mounting a floppy.  MS_GROUP:
		 * Allow members of device group to mount. (Martin Dickopp)
		 */
		if (u_flags & (MNT_MS_OWNER | MNT_MS_GROUP)) {
			struct stat sb;

			if (strncmp(srcpath, "/dev/", 5) == 0 &&
			    stat(srcpath, &sb) == 0 &&
			    (((u_flags & MNT_MS_OWNER) && getuid() == sb.st_uid) ||
			     ((u_flags & MNT_MS_GROUP) && mnt_in_group(sb.st_gid))))

				cxt->user_mountflags |= MNT_MS_USER;
		}

		if (!(cxt->user_mountflags & (MNT_MS_USER | MNT_MS_USERS))) {
			DBG(CXT, mnt_debug_h(cxt, "permissions evaluation ends with -EPERMS"));
			return -EPERM;
		}
	}

	return 0;
}

static int mnt_context_prepare_srcpath(mnt_context *cxt)
{
	const char *path = NULL, *type;
	mnt_cache *cache;
	const char *t, *v, *src;
	int rc = 0;

	if (!cxt || !cxt->fs)
		return -EINVAL;

	src = mnt_fs_get_source(cxt->fs);

	/* ignore filesystems without a real source */
	if (!src || (cxt->fs->flags & (MNT_FS_PSEUDO | MNT_FS_NET)))
		return 0;

	DBG(CXT, mnt_debug_h(cxt, "preparing srcpath '%s'", src));

	cache = mnt_context_get_cache(cxt);
	type = mnt_fs_get_fstype(cxt->fs);

	if (!mnt_fs_get_tag(cxt->fs, &t, &v)) {
		/*
		 * Source is TAG (evaluate)
		 */
		if (cache)
			path = mnt_resolve_tag(t, v, cache);

		rc = path ? mnt_fs_set_source(cxt->fs, path) : -EINVAL;

	} else if (!type || (strncmp(type, "9p", 2) &&
			     strncmp(type, "nfs", 3) &&
			     strncmp(type, "cifs", 4) &&
			     strncmp(type, "smbfs", 5))) {
		/*
		 * Source is PATH (canonicalize)
		 */
		if (cache) {
			path = mnt_resolve_path(src, cache);
			if (strcmp(path, src))
				rc = mnt_fs_set_source(cxt->fs, path);
		}
	}

	if (rc) {
		DBG(CXT, mnt_debug_h(cxt, "failed to prepare srcpath"));
		return rc;
	}

	if (!path)
		path = src;

	/*
	 * Initialize loop device
	 */
	if (mnt_context_is_loop(cxt) &&
	    !(cxt->mountflags & (MS_BIND | MS_MOVE |
			         MS_PROPAGATION | MS_REMOUNT))) {
		; /* TODO */
	}

	DBG(CXT, mnt_debug_h(cxt, "final srcpath '%s'", path));
	return 0;
}

static int mnt_context_guess_fstype(mnt_context *cxt)
{
	char *type;
	const char *dev;
	int rc = -EINVAL;

	if (!cxt || !cxt->fs)
		return -EINVAL;

	if (cxt->mountflags & (MS_BIND | MS_MOVE | MS_PROPAGATION))
		goto none;

	type = (char *) mnt_fs_get_fstype(cxt->fs);
	if (type && !strcmp(type, "auto")) {
		mnt_fs_set_fstype(cxt->fs, NULL);
		type = NULL;
	}

	if (type)
		goto done;
	if (cxt->flags & MS_REMOUNT)
		goto none;
	dev = mnt_fs_get_srcpath(cxt->fs);
	if (!dev)
		goto err;

	if (access(dev, F_OK) == 0) {
		mnt_cache *cache = mnt_context_get_cache(cxt);

		type = mnt_get_fstype(dev, &cxt->ambi, cache);
		if (type) {
			rc = mnt_fs_set_fstype(cxt->fs, type);
			if (!cache)
				free(type);	/* type is not cached */
		}
	} else {
		if (strchr(dev, ':') != NULL)
			rc = mnt_fs_set_fstype(cxt->fs, "nfs");
		else if (!strncmp(dev, "//", 2))
			rc = mnt_fs_set_fstype(cxt->fs, "cifs");
	}
	if (rc)
		goto err;
done:
	DBG(CXT, mnt_debug_h(cxt, "detected FS type: %s",
				mnt_fs_get_fstype(cxt->fs)));
	return 0;
none:
	return mnt_fs_set_fstype(cxt->fs, "none");
err:
	DBG(CXT, mnt_debug_h(cxt, "failed to detect FS type"));
	return rc;
}

static int mnt_context_merge_mountflags(mnt_context *cxt)
{
	unsigned long fl = 0;
	int rc;

	rc = mnt_context_get_mountflags(cxt, &fl);
	if (rc)
		return rc;
	cxt->mountflags = fl;

	fl = 0;
	rc = mnt_context_get_userspace_mountflags(cxt, &fl);
	if (rc)
		return rc;
	cxt->user_mountflags = fl;

	cxt->flags |= MNT_FL_MOUNTFLAGS_MERGED;
	return 0;
}

/*
 * The default is to use fstype from cxt->fs, this could be overwritten by
 * @type. The @act is MNT_ACT_{MOUNT,UMOUNT}.
 *
 * Returns: 0 on success or negative number in case of error. Note that success
 * does not mean that there is any usable helper, you have to check cxt->helper.
 */
static int mnt_context_prepare_helper(mnt_context *cxt, int act, const char *type)
{
	char search_path[] = FS_SEARCH_PATH;		/* from config.h */
	char *p = NULL, *path;
	const char *name;

	assert(cxt);
	assert(cxt->fs);

	name = MNT_ACT_MOUNT ? "mount" : "umount";

	if (!type)
		type = mnt_fs_get_fstype(cxt->fs);

	if ((cxt->flags & MNT_FL_NOHELPERS) || !type ||
	    !strcmp(type, "none") || (cxt->fs->flags & MNT_FS_SWAP))
		return 0;

	path = strtok_r(search_path, ":", &p);
	while (path) {
		char helper[PATH_MAX];
		struct stat st;
		int rc;

		rc = snprintf(helper, sizeof(helper), "%s/%s.%s",
						path, name, type);
		path = strtok_r(NULL, ":", &p);

		if (rc >= sizeof(helper) || rc < 0)
			continue;

		rc = stat(helper, &st);
		if (rc == -1 && errno == ENOENT && strchr(type, '.')) {
			/* If type ends with ".subtype" try without it */
			*strrchr(helper, '.') = '\0';
			rc = stat(helper, &st);
		}

		DBG(CXT, mnt_debug_h(cxt, "%s ... %s", helper,
					rc ? "not found" : "found"));
		if (rc)
			continue;

		if (cxt->helper)
			free(cxt->helper);
		cxt->helper = strdup(helper);
		if (!cxt->helper)
			return -ENOMEM;
		return 0;
	}

	return 0;
}

static int mnt_context_prepare_update(mnt_context *cxt, int act)
{
	int rc;

	if ((cxt->flags & MNT_FL_NOMTAB) || cxt->helper)
		return 0;

	if (!cxt->update) {
		cxt->update = mnt_new_update(act, cxt->mountflags, cxt->fs);
		if (!cxt->update)
			return -ENOMEM;
	} else {
		rc = mnt_update_set_action(cxt->update, act);
		if (!rc)
			rc = mnt_update_set_mountflags(cxt->update, cxt->mountflags);
		if (!rc)
			rc = mnt_update_set_fs(cxt->update, cxt->fs);
		if (rc)
			return rc;
	}

	if (cxt->flags & MNT_FL_NOLOCK)
		mnt_update_disable_lock(cxt->update, TRUE);

	rc = mnt_prepare_update(cxt->update);

	if (rc == 1)
		/* mtab update is unnecessary for this system */
		rc = 0;

	return rc;
}

/**
 * mnt_context_get_lock:
 * @cxt: mount context
 *
 * The lock is available after mnt_context_prepare_mount() or
 * mnt_context_prepare_umount().
 *
 * The application that uses libmount context does not have to care about
 * mtab locking, but with a small exceptions: the application has to be able to
 * remove the lock file when interrupted by signal. It means that properly written
 * mount(8)-like application has to call mnt_unlock_file() from a signal handler.
 *
 * See also mnt_unlock_file(), mnt_context_disable_lock() and
 * mnt_context_disable_mtab().
 *
 * It's not error if this function returns NULL (it usually means that the
 * context is not prepared yet, or mtab update is unnecessary).
 *
 * Returns: pointer to lock struct.
 */
mnt_lock *mnt_context_get_lock(mnt_context *cxt)
{
	if (!cxt || !cxt->update || (cxt->flags & (MNT_FL_NOMTAB | MNT_FL_NOLOCK)))
		return NULL;
	return mnt_update_get_lock(cxt->update);
}

/**
 * mnt_context_prepare_mount:
 * @cxt: mount context
 *
 * This function:
 *	- read information from fstab (if necessary)
 *	- cleanup mount options
 *	- check premissions
 *	- prepare device (e.g. loop device)
 *	- detect FS type (if necessary)
 *	- generate mount flags and mount data (if not set yet)
 *	- prepare for mtab update (if necessary)
 *
 * It's strongly recommended to use this function before mnt_context_mount_fs().
 *
 * Returns: 0 on success, and negative number in case of error.
 */
int mnt_context_prepare_mount(mnt_context *cxt)
{
	int rc = 0;

	if (!cxt)
		return -EINVAL;

	if (!cxt->spec && !(cxt->fs && (mnt_fs_get_source(cxt->fs) ||
			                mnt_fs_get_target(cxt->fs))))
		return -EINVAL;

	rc = mnt_context_apply_fstab(cxt);
	if (!rc)
		rc = mnt_context_merge_mountflags(cxt);
	if (!rc)
		rc = mnt_context_evaluate_permissions(cxt);
	if (!rc)
		rc = mnt_context_fix_optstr(cxt);
	if (!rc)
		rc = mnt_context_prepare_srcpath(cxt);
	if (!rc)
		rc = mnt_context_guess_fstype(cxt);
	if (!rc)
		rc = mnt_context_prepare_helper(cxt, MNT_ACT_MOUNT, NULL);
	if (!rc)
		rc = mnt_context_prepare_update(cxt, MNT_ACT_MOUNT);

	if (!rc) {
		DBG(CXT, mnt_debug_h(cxt, "sucessfully prepared"));
		return 0;
	}

	DBG(CXT, mnt_debug_h(cxt, "prepare failed"));
	return rc;
}

static int exec_mount_helper(mnt_context *cxt)
{
	char *o = NULL;
	int rc;

	assert(cxt);
	assert(cxt->fs);
	assert(cxt->helper);

	rc = mnt_context_get_helper_optstr(cxt, &o);
	if (rc)
		goto done;

	DBG_FLUSH;

	switch (fork()) {
	case 0:
	{
		const char *args[12], *type;
		int i = 0;

		if (setgid(getgid()) < 0)
			exit(EXIT_FAILURE);

		if (setuid(getuid()) < 0)
			exit(EXIT_FAILURE);

		type = mnt_fs_get_fstype(cxt->fs);

		args[i++] = cxt->helper;			/* 1 */
		args[i++] = mnt_fs_get_srcpath(cxt->fs);	/* 2 */
		args[i++] = mnt_fs_get_target(cxt->fs);	/* 3 */

		if (cxt->flags & MNT_FL_SLOPPY)
			args[i++] = "-s";		/* 4 */
		if (cxt->flags & MNT_FL_FAKE)
			args[i++] = "-f";		/* 5 */
		if (cxt->flags & MNT_FL_NOMTAB)
			args[i++] = "-n";		/* 6 */
		if (cxt->flags & MNT_FL_VERBOSE)
			args[i++] = "-v";		/* 7 */
		if (o) {
			args[i++] = "-o";		/* 8 */
			args[i++] = o;			/* 9 */
		}
		if (type && !endswith(cxt->helper, type)) {
			args[i++] = "-t";		/* 10 */
			args[i++] = type;		/* 11 */
		}
		args[i] = NULL;				/* 12 */
#ifdef CONFIG_LIBMOUNT_DEBUG
		i = 0;
		for (i = 0; args[i]; i++)
			DBG(CXT, mnt_debug_h(cxt, "argv[%d] = \"%s\"",
							i, args[i]));
#endif
		DBG_FLUSH;
		execv(cxt->helper, (char * const *) args);
		exit(EXIT_FAILURE);
	}
	default:
	{
		int st;
		wait(&st);
		cxt->helper_status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;

		DBG(CXT, mnt_debug_h(cxt, "%s executed [status=%d]",
					cxt->helper, cxt->helper_status));
		rc = 0;
		break;
	}

	case -1:
		rc = -errno;
		DBG(CXT, mnt_debug_h(cxt, "fork() failed"));
		break;
	}

done:
	free(o);
	return rc;
}

/*
 * The default is to use fstype from cxt->fs, this could be overwritten by
 * @try_type argument.
 */
static int mnt_context_do_mount(mnt_context *cxt, const char *try_type)
{
	int rc;
	const char *src, *target, *type;
	unsigned long flags;

	assert(cxt);
	assert(cxt->fs);

	if (try_type && !cxt->helper) {
		rc = mnt_context_prepare_helper(cxt, MNT_ACT_MOUNT, try_type);
		if (!rc)
			return rc;
	}
	if (cxt->helper)
		return exec_mount_helper(cxt);

	type = try_type ? : mnt_fs_get_fstype(cxt->fs);

	flags = cxt->mountflags;
	src = mnt_fs_get_srcpath(cxt->fs);
	target = mnt_fs_get_target(cxt->fs);

	if (!src || !target)
		return -EINVAL;

	if (!(flags & MS_MGC_MSK))
		flags |= MS_MGC_VAL;

	DBG(CXT, mnt_debug_h(cxt, "calling mount(2) "
			"[source=%s, target=%s, type=%s, "
			" mountflags=%08lx, mountdata=%s]",
			src, target, type,
			flags, cxt->mountdata ? "yes" : "<none>"));

	if (mount(src, target, type, flags, cxt->mountdata)) {
		cxt->syscall_errno = errno;
		DBG(CXT, mnt_debug_h(cxt, "mount(2) failed [errno=%d]",
						cxt->syscall_errno));
		return -cxt->syscall_errno;
	}

	DBG(CXT, mnt_debug_h(cxt, "mount(2) success"));
	return 0;
}

/**
 * mnt_context_mount_fs:
 * @cxt: mount context
 *
 * Mount filesystem by mount(2) or fork()+exec(/sbin/mount.<type>).
 *
 * See also mnt_context_disable_helpers().
 *
 * Returns: 0 on success, and negative number in case of error.
 */
int mnt_context_mount_fs(mnt_context *cxt)
{
	int rc = -EINVAL;
	const char *type;

	if (!cxt || !cxt->fs || (cxt->fs->flags & MNT_FS_SWAP))
		return -EINVAL;

	if (!(cxt->flags & MNT_FL_MOUNTDATA))
		cxt->mountdata = (char *) mnt_fs_get_fs_optstr(cxt->fs);

	type = mnt_fs_get_fstype(cxt->fs);

	if (type && !strchr(type, ',')) {
		rc = mnt_context_do_mount(cxt, NULL);
		if (rc)
			return rc;
	}

	/* TODO: try all filesystems from comma separated list of types */

	/* TODO: try all filesystems from /proc/filesystems and /etc/filesystems */

	return rc;
}

/**
 * mnt_context_post_mount:
 * @cxt: mount context
 *
 * Updates mtab, etc. This function should be always called after
 * mnt_context_do_mount().
 *
 * Returns: 0 on success, and negative number in case of error.
 */
int mnt_context_post_mount(mnt_context *cxt)
{
	int rc = 0;

	if (!cxt)
		return -EINVAL;
	/*
	 * Update /etc/mtab or /var/run/mount/mountinfo
	 */
	if (!cxt->syscall_errno && !cxt->helper &&
	    !(cxt->flags & MNT_FL_NOMTAB) &&
	    cxt->update && !mnt_update_is_pointless(cxt->update)) {

		/* TODO: if mtab update is expected then checkif the target is really
		 *       mounted read-write to avoid 'ro' in mtab and 'rw' in /proc/mounts.
		 */
		rc = mnt_update_file(cxt->update);
	}

	return rc;
}

/**
 * mnt_context_get_mount_error:
 * @cxt: mount context
 * @buf: buffer for error message
 * @bufsiz: size of buffer
 *
 * Generates human readable error message for failed mnt_context_mount_fs().
 *
 * Returns: 0 on success, and negative number in case of error.
 */
int mnt_context_get_mount_error(mnt_context *cxt, char *buf, size_t bufsiz)
{
	/* TODO: based on cxt->syscall_errno or cxt->helper_status */
	return 0;
}

#ifdef TEST_PROGRAM

mnt_lock *lock;

static void lock_fallback(void)
{
	if (lock) {
		mnt_unlock_file(lock);
		mnt_free_lock(lock);
	}
}

int test_mount(struct mtest *ts, int argc, char *argv[])
{
	int idx = 1, rc = 0;
	mnt_context *cxt;

	if (argc < 2)
		return -EINVAL;

	cxt = mnt_new_context();
	if (!cxt)
		return -ENOMEM;

	if (!strcmp(argv[idx], "-o")) {
		mnt_context_set_optstr(cxt, argv[idx + 1]);
		idx += 2;
	}
	if (!strcmp(argv[idx], "-t")) {
		/* TODO: use mnt_context_set_fstype_pattern() */
		mnt_context_set_fstype(cxt, argv[idx + 1]);
		idx += 2;
	}

	if (argc == idx + 1)
		/* mount <spec> */
		mnt_context_set_spec(cxt, argv[idx++]);

	else if (argc == idx + 2) {
		/* mount <device> <mountpoint> */
		mnt_context_set_source(cxt, argv[idx++]);
		mnt_context_set_target(cxt, argv[idx++]);
	}

	rc = mnt_context_prepare_mount(cxt);
	if (rc)
		printf("failed to prepare mount\n");
	else {
		lock = mnt_context_get_lock(cxt);
		if (lock)
			atexit(lock_fallback);

		rc = mnt_context_mount_fs(cxt);
		if (rc)
			printf("failed to mount\n");
		else {
			printf("successfully mounted");
			rc = mnt_context_post_mount(cxt);
			if (rc)
				printf("mtab update failed\n");
		}
	}

	mnt_free_context(cxt);
	return rc;
}

int main(int argc, char *argv[])
{
	struct mtest tss[] = {
	{ "--mount",    test_mount,        "[-o <opts>] [-t <type>] <spec> | <src> <target>" },
	{ NULL }
	};

	return mnt_run_test(tss, argc, argv);
}

#endif /* TEST_PROGRAM */
