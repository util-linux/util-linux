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

#define MNT_FL_EXTERN_FS	(1 << 15)	/* cxt->fs is not private */
#define MNT_FL_EXTERN_FSTAB	(1 << 16)	/* cxt->fstab is not private */
#define MNT_FL_EXTERN_CACHE	(1 << 17)	/* cxt->cache is not private */

#define MNT_FL_MOUNTDATA	(1 << 20)
#define MNT_FL_FSTAB_APPLIED	(1 << 21)
#define MNT_FL_MOUNTFLAGS_MERGED (1 << 22)	/* MS_* flags was read from optstr */

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

	DBG(CXT, mnt_debug_h(cxt, "allocate"));

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
	cxt->spec = NULL;

	cxt->mountflags = 0;
	cxt->user_mountflags = 0;
	cxt->mountdata = NULL;
	cxt->flags = MNT_FL_DEFAULT;

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
int mnt_context_disable_nomtab(mnt_context *cxt, int disable)
{
	return mnt_context_set_flag(cxt, MNT_FL_NOMTAB, disable);
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

static int mnt_context_subst_optstr(mnt_context *cxt)
{
	int rc = 0;
	char *o, *o0;
	char *val = NULL;
	size_t valsz = 0;

	if (!cxt || !cxt->fs)
		return -EINVAL;

	o0 = o = (char *) mnt_fs_get_optstr(cxt->fs);
	if (!o)
		return 0;

	if (!mnt_optstr_get_option(o, "uid", &val, &valsz) && val &&
	    !strncmp(val, "useruid", 7)) {
		char id[40];

		snprintf(id, sizeof(id), "%u", getuid());
		rc = mnt_optstr_set_option(&o, "uid", id);
		if (rc)
			return rc;
	}

	val = NULL, valsz = 0;

	if (!mnt_optstr_get_option(o, "gid", &val, &valsz) && val &&
	    !strncmp(val, "usergid", 7)) {
		char id[40];

		snprintf(id, sizeof(id), "%u", getgid());
		rc = mnt_optstr_set_option(&o, "gid", id);
		if (rc)
			return rc;
	}

	if (o != o0)
		rc = mnt_fs_set_optstr(cxt->fs, o);

	return rc;
}

static int mnt_context_check_permissions(mnt_context *cxt)
{
	return 0; /* TODO */
}

static int mnt_context_prepare_srcpath(mnt_context *cxt)
{
	const char *path;
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

	if (!mnt_fs_get_tag(cxt->fs, &t, &v)) {
		/*
		 * Source is TAG (evaluate)
		 */
		if (cache)
			path = mnt_resolve_tag(t, v, cache);

		rc = path ? mnt_fs_set_source(cxt->fs, path) : -EINVAL;
	} else {
		/*
		 * Source is PATH (canonicalize)
		 */
		if (cache) {
			path = mnt_resolve_path(src, cache);
			if (strcmp(path, src))
				rc = mnt_fs_set_source(cxt->fs, path);
		} else
			path = src;
	}

	if (rc) {
		DBG(CXT, mnt_debug_h(cxt, "failed to prepare srcpath"));
		return rc;
	}

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

static int mnt_context_detect_fstype(mnt_context *cxt)
{
	return 0; /* TODO */
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
	if (rc)
		goto err;

	/* prepare mount flags */
	{
		unsigned long fl = 0;

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
	}

	rc = mnt_context_subst_optstr(cxt);
	if (rc)
		goto err;

	rc = mnt_context_check_permissions(cxt);
	if (rc)
		goto err;

	rc = mnt_context_prepare_srcpath(cxt);
	if (rc)
		goto err;

	rc = mnt_context_detect_fstype(cxt);
	if (rc)
		goto err;


	/* TODO: prepare mtab update */

	DBG(CXT, mnt_debug_h(cxt, "sucessfully prepared"));
	return 0;
err:
	DBG(CXT, mnt_debug_h(cxt, "prepare failed"));
	return rc;
}

int mnt_context_mount_fs(mnt_context *cxt)
{
	if (!cxt)
		return -EINVAL;

	DBG(CXT, mnt_debug_h(cxt, "mounting:"));
	DBG(CXT, mnt_fs_print_debug(cxt->fs, stderr));
	DBG(CXT, mnt_debug_h(cxt, "mountflags: 0x%lx", cxt->mountflags));

	return 0;
}

#ifdef TEST_PROGRAM

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
		fprintf(stderr, "failed to prepare mount\n");
	else {
		rc = mnt_context_mount_fs(cxt);
		if (rc)
			fprintf(stderr, "failed to mount\n");
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
