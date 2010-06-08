/*
 * Copyright (C) 2008-2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: fs
 * @title: Filesystem
 * @short_description: mnt_fs represents one entry in fstab/mtab/mountinfo
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <blkid.h>

#include "nls.h"
#include "mountP.h"

/**
 * mnt_new_fs:
 *
 * Returns: newly allocated mnt_file fs.
 */
mnt_fs *mnt_new_fs(void)
{
	mnt_fs *fs = calloc(1, sizeof(struct _mnt_fs));
	if (!fs)
		return NULL;

	INIT_LIST_HEAD(&fs->ents);
	return fs;
}

/**
 * mnt_free_fs:
 * @fs: fs pointer
 *
 * Deallocates the fs.
 */
void mnt_free_fs(mnt_fs *fs)
{
	if (!fs)
		return;
	list_del(&fs->ents);

	free(fs->source);
	free(fs->tagname);
	free(fs->tagval);
	free(fs->mntroot);
	free(fs->target);
	free(fs->fstype);
	free(fs->optstr);
	free(fs->vfs_optstr);
	free(fs->fs_optstr);

	free(fs);
}

/**
 * mnt_fs_get_userdata:
 * @fs: mnt_file instance
 *
 * Returns: private data set by mnt_fs_set_userdata() or NULL.
 */
void *mnt_fs_get_userdata(mnt_fs *fs)
{
	return fs ? fs->userdata : NULL;
}

/**
 * mnt_fs_set_userdata:
 * @fs: mnt_file instance
 *
 * The "userdata" are library independent data.
 *
 * Returns: 0 or -1 in case of error (if @fs is NULL).
 */
int mnt_fs_set_userdata(mnt_fs *fs, void *data)
{
	if (!fs)
		return -1;
	fs->userdata = data;
	return 0;
}

/**
 * mnt_fs_get_srcpath:
 * @fs: mnt_file (fstab/mtab/mountinfo) fs
 *
 * The mount "source path" is:
 * - a directory for 'bind' mounts (in fstab or mtab only)
 * - a device name for standard mounts
 *
 * See also mnt_fs_get_tag() and mnt_fs_get_source().
 *
 * Returns: mount source path or NULL in case of error or when the path
 * is not defined.
 */
const char *mnt_fs_get_srcpath(mnt_fs *fs)
{
	assert(fs);
	if (!fs)
		return NULL;

	/* fstab-like fs */
	if (fs->tagname)
		return NULL;	/* the source contains a "NAME=value" */
	return fs->source;
}

/**
 * mnt_fs_get_source:
 * @fs: mnt_file (fstab/mtab/mountinfo) fs
 *
 * Returns: mount source. Note that the source could be unparsed TAG
 * (LABEL/UUID). See also mnt_fs_get_srcpath() and mnt_fs_get_tag().
 */
const char *mnt_fs_get_source(mnt_fs *fs)
{
	return fs ? fs->source : NULL;
}

/* Used by parser mnt_file ONLY (@source has to be allocated) */
int __mnt_fs_set_source(mnt_fs *fs, char *source)
{
	assert(fs);

	if (!source)
		return -1;

	if (strchr(source, '=')) {
		char *name, *val;

		if (blkid_parse_tag_string(source, &name, &val) != 0)
			return -1;

		fs->tagval = val;
		fs->tagname = name;
	}

	fs->source = source;
	return 0;
}

/**
 * mnt_fs_set_source:
 * @fs: fstab/mtab/mountinfo entry
 * @source: new source
 *
 * This function creates a private copy (strdup()) of @source.
 *
 * Returns: 0 on success or -1 in case of error.
 */
int mnt_fs_set_source(mnt_fs *fs, const char *source)
{
	char *p;

	if (!fs && !source)
		return -1;

	p = strdup(source);
	if (!p)
		return -1;

	free(fs->tagval);
	free(fs->tagname);
	free(fs->source);
	fs->tagval = fs->tagname = fs->source = NULL;

	return __mnt_fs_set_source(fs, p);
}

/**
 * mnt_fs_get_tag:
 * @fs: fs
 * @name: returns pointer to NAME string
 * @value: returns pointer to VALUE string
 *
 * "TAG" is NAME=VALUE (e.g. LABEL=foo)
 *
 * The TAG is the first column in the fstab file. The TAG or "srcpath" has to
 * be always set for all entries.
 *
 * See also mnt_fs_get_source().
 *
 * <informalexample>
 *   <programlisting>
 *	char *src;
 *	mnt_fs *fs = mnt_tab_find_target(tb, "/home", MNT_ITER_FORWARD);
 *
 *	if (!fs)
 *		goto err;
 *
 *	src = mnt_fs_get_srcpath(fs);
 *	if (!src) {
 *		char *tag, *val;
 *		if (mnt_fs_get_tag(fs, &tag, &val) == 0)
 *			printf("%s: %s\n", tag, val);	// LABEL or UUID
 *	} else
 *		printf("device: %s\n", src);		// device or bind path
 *   </programlisting>
 * </informalexample>
 *
 * Returns: 0 on success or -1 in case that a TAG is not defined.
 */
int mnt_fs_get_tag(mnt_fs *fs, const char **name, const char **value)
{
	if (fs == NULL || !fs->tagname)
		return -1;
	if (name)
		*name = fs->tagname;
	if (value)
		*value = fs->tagval;
	return 0;
}

/**
 * mnt_fs_get_target:
 * @fs: fstab/mtab/mountinfo entry pointer
 *
 * Returns: pointer to mountpoint path or NULL
 */
const char *mnt_fs_get_target(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->target : NULL;
}

/**
 * mnt_fs_set_target:
 * @fs: fstab/mtab/mountinfo entry
 * @target: mountpoint
 *
 * This function creates a private copy (strdup()) of @target.
 *
 * Returns: 0 on success or -1 in case of error.
 */
int mnt_fs_set_target(mnt_fs *fs, const char *target)
{
	char *p;

	assert(fs);

	if (!fs || !target)
		return -1;

	p = strdup(target);
	if (!p)
		return -1;
	free(fs->target);
	fs->target = p;

	return 0;
}

/**
 * mnt_fs_get_fstype:
 * @fs: fstab/mtab/mountinfo entry pointer
 *
 * Returns: pointer to filesystem type.
 */
const char *mnt_fs_get_fstype(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->fstype : NULL;
}

/* Used by mnt_file parser only */
int __mnt_fs_set_fstype(mnt_fs *fs, char *fstype)
{
	assert(fs);

	if (!fstype)
		return -1;

	fs->fstype = fstype;
	fs->flags &= ~MNT_FS_PSEUDO;
	fs->flags &= ~MNT_FS_NET;

	/* save info about pseudo filesystems */
	if (mnt_fstype_is_pseudofs(fs->fstype))
		fs->flags |= MNT_FS_PSEUDO;
	else if (mnt_fstype_is_netfs(fs->fstype))
		fs->flags |= MNT_FS_NET;

	return 0;
}

/**
 * mnt_fs_set_fstype:
 * @fs: fstab/mtab/mountinfo entry
 * @fstype: filesystem type
 *
 * This function creates a private copy (strdup()) of @fstype.
 *
 * Returns: 0 on success or -1 in case of error.
 */
int mnt_fs_set_fstype(mnt_fs *fs, const char *fstype)
{
	char *p;

	if (!fs || !fstype)
		return -1;

	p = strdup(fstype);
	if (!p)
		return -1;
	free(fs->fstype);

	return __mnt_fs_set_fstype(fs, p);
}

/**
 * mnt_fs_get_optstr:
 * @fs: fstab/mtab/mountinfo entry pointer
 *
 * Returns: pointer to mount option string with all options (FS and VFS)
 */
const char *mnt_fs_get_optstr(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->optstr : NULL;
}

/**
 * mnt_fs_set_optstr:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: options string
 *
 * This function creates a private copy (strdup()) of @optstr.
 *
 * Returns: 0 on success or -1 in case of error.
 */
int mnt_fs_set_optstr(mnt_fs *fs, const char *optstr)
{
	char *p;

	assert(fs);

	if (!fs || !optstr)
		return -1;
	p = strdup(optstr);
	if (!p)
		return -1;

	free(fs->optstr);
	free(fs->fs_optstr);
	free(fs->vfs_optstr);
	fs->fs_optstr = fs->vfs_optstr = NULL;

	/* TODO: it would be possible to use built-in maps of options
	 * and differentiate between VFS and FS options, then we can
	 * set fs_optstr and vfs_optstr */

	fs->optstr = p;

	return 0;
}

/**
 * mnt_fs_get_fs_optstr:
 * @fs: fstab/mtab/mountinfo entry pointer
 *
 * This function works for "mountinfo" files only.
 *
 * Returns: pointer to superblock (fs-depend) mount option string or NULL.
 */
const char *mnt_fs_get_fs_optstr(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->fs_optstr : NULL;
}

/**
 * mnt_fs_get_vfs_optstr:
 * @fs: fstab/mtab entry pointer
 *
 * This function works for "mountinfo" files only.
 *
 * Returns: pointer to fs-independent (VFS) mount option string or NULL.
 */
const char *mnt_fs_get_vfs_optstr(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->vfs_optstr : NULL;
}


/**
 * mnt_fs_get_freq:
 * @fs: fstab/mtab/mountinfo entry pointer
 *
 * Returns: dump frequency in days.
 */
int mnt_fs_get_freq(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->freq : 0;
}

/**
 * mnt_fs_set_freq:
 * @fs: fstab/mtab entry pointer
 * @freq: dump frequency in days
 *
 * Returns: 0 on success or -1 in case of error.
 */
int mnt_fs_set_freq(mnt_fs *fs, int freq)
{
	assert(fs);
	if (!fs)
		return -1;
	fs->freq = freq;
	return 0;
}

/**
 * mnt_fs_get_passno:
 * @fs: fstab/mtab entry pointer
 *
 * Returns: "pass number on parallel fsck".
 */
int mnt_fs_get_passno(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->passno: 0;
}

/**
 * mnt_fs_set_passno:
 * @fs: fstab/mtab entry pointer
 * @passno: pass number
 *
 * Returns: 0 on success or -1 in case of error.
 */
int mnt_fs_set_passno(mnt_fs *fs, int passno)
{
	assert(fs);
	if (!fs)
		return -1;
	fs->passno = passno;
	return 0;
}

/**
 * mnt_fs_get_id:
 * @fs: /proc/self/mountinfo entry
 *
 * Returns: mount ID (unique identifier of the mount) or -1 if ID undefined
 * (for example if @fs is not mountinfo entry).
 */
int mnt_fs_get_id(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->id : -1;
}

/**
 * mnt_fs_get_parent_id:
 * @fs: /proc/self/mountinfo entry
 *
 * Returns: parent mount ID or -1 if ID undefined (for example if @fs is not
 * mountinfo entry).
 */
int mnt_fs_get_parent_id(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->parent : -1;
}

/**
 * mnt_fs_get_devno:
 * @fs: /proc/self/mountinfo
 *
 * Returns: value of st_dev for files on filesystem or 0 in case of error.
 */
dev_t mnt_fs_get_devno(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->devno : 0;
}

/**
 * mnt_fs_get_option:
 * @fs: fstab/mtab/mountinfo entry pointer
 * @name: option name
 * @value: returns pointer to the begin of the value (e.g. name=VALUE) or NULL
 * @valsz: returns size of options value or 0
 *
 * Returns: 0 on success, 1 when not found the @name or -1 in case of error.
 */
int mnt_fs_get_option(mnt_fs *fs, const char *name,
		char **value, size_t *valsz)
{
	char *optstr = (char *) mnt_fs_get_optstr(fs);
	return optstr ? mnt_optstr_get_option(optstr, name, value, valsz) : 1;
}

/**
 * mnt_fs_match_target:
 * @fs: filesystem
 * @target: mountpoint path
 * @cache: tags/paths cache or NULL
 *
 * Possible are three attempts:
 *	1) compare @target with @fs->target
 *	2) realpath(@target) with @fs->target
 *	3) realpath(@target) with realpath(@fs->target).
 *
 * The 2nd and 3rd attempts are not performed when @cache is NULL.
 *
 * Returns: 1 if @fs target is equal to @target else 0.
 */
int mnt_fs_match_target(mnt_fs *fs, const char *target, mnt_cache *cache)
{
	int rc = 0;

	if (!fs || !target || !fs->target)
		return 0;

	/* 1) native paths */
	rc = !strcmp(target, fs->target);

	if (!rc && cache) {
		/* 2) - canonicalized and non-canonicalized */
		char *cn = mnt_resolve_path(target, cache);
		rc = (cn && strcmp(cn, fs->target) == 0);

		/* 3) - canonicalized and canonicalized */
		if (!rc && cn) {
			char *tcn = mnt_resolve_path(fs->target, cache);
			rc = (tcn && strcmp(cn, tcn) == 0);
		}
	}

	return rc;
}

/**
 * mnt_fs_match_source:
 * @fs: filesystem
 * @source: tag or path (device or so)
 * @cache: tags/paths cache or NULL
 *
 * Possible are four attempts:
 *	1) compare @source with @fs->source
 *	2) compare realpath(@source) with @fs->source
 *	3) compare realpath(@source) with realpath(@fs->source)
 *	4) compare realpath(@source) with evaluated tag from @fs->source
 *
 * The 2nd, 3rd and 4th attempts are not performed when @cache is NULL. The
 * 2nd and 3rd attempts are not performed if @fs->source is tag.
 *
 * Returns: 1 if @fs source is equal to @source else 0.
 */
int mnt_fs_match_source(mnt_fs *fs, const char *source, mnt_cache *cache)
{
	char *cn;
	const char *src, *t, *v;

	if (!fs || !source || !fs->source)
		return 0;

	/* 1) native paths/tags */
	if (!strcmp(source, fs->source))
		return 1;

	if (!cache)
		return 0;
	if (fs->flags & (MNT_FS_NET | MNT_FS_PSEUDO))
		return 0;

	cn = mnt_resolve_spec(source, cache);
	if (!cn)
		return 0;

	/* 2) canonicalized and native */
	src = mnt_fs_get_srcpath(fs);
	if (src && !strcmp(cn, src))
		return 1;

	/* 3) canonicalized and canonicalized */
	if (src) {
		src = mnt_resolve_path(src, cache);
		if (src && !strcmp(cn, src))
			return 1;
	}
	if (src || mnt_fs_get_tag(fs, &t, &v))
		/* src path does not match and tag is not defined */
		return 0;

	/* read @source's tags to the cache */
	if (mnt_cache_read_tags(cache, cn) < 0) {
		if (errno == EACCES) {
			/* we don't have permissions to read TAGs from
			 * @source, but can translate @fs tag to devname.
			 *
			 * (because libblkid uses udev symlinks and this is
			 * accessible for non-root uses)
			 */
			char *x = mnt_resolve_tag(t, v, cache);
			if (x && !strcmp(x, cn))
				return 1;
		}
		return 0;
	}

	/* 4) has the @source a tag that matches with tag from @fs ? */
	if (mnt_cache_device_has_tag(cache, cn, t, v))
		return 1;

	return 0;
}

/**
 * mnt_fs_match_fstype:
 * @fs: filesystem
 * @types: filesystem name or comma delimited list of filesystems
 *
 * For more details see mnt_match_fstype().
 *
 * Returns: 1 if @fs type is matching to @types else 0. The function returns
 * 0 when types is NULL.
 */
int mnt_fs_match_fstype(mnt_fs *fs, const char *types)
{
	return mnt_match_fstype(fs->fstype, types);
}

/**
 * mnt_fs_match_options:
 * @fs: filesystem
 * @options: comma delimited list of options (and nooptions)
 *
 * For more details see mnt_match_options().
 *
 * Returns: 1 if @fs type is matching to @options else 0. The function returns
 * 0 when types is NULL.
 */
int mnt_fs_match_options(mnt_fs *fs, const char *options)
{
	return mnt_match_options(fs->optstr, options);
}

/* Unfortunately the classical Unix /etc/mtab and /etc/fstab
   do not handle directory names containing spaces.
   Here we mangle them, replacing a space by \040.
   What do other Unices do? */

static unsigned char need_escaping[] = { ' ', '\t', '\n', '\\' };

static char *mangle(const char *s)
{
	char *ss, *sp;
	int n;

	n = strlen(s);
	ss = sp = malloc(4*n+1);
	if (!sp)
		return NULL;
	while(1) {
		for (n = 0; n < sizeof(need_escaping); n++) {
			if (*s == need_escaping[n]) {
				*sp++ = '\\';
				*sp++ = '0' + ((*s & 0300) >> 6);
				*sp++ = '0' + ((*s & 070) >> 3);
				*sp++ = '0' + (*s & 07);
				goto next;
			}
		}
		*sp++ = *s;
		if (*s == 0)
			break;
	next:
		s++;
	}
	return ss;
}

/**
 * mnt_fprintf_line:
 * @f: FILE
 * @fmt: printf-like format string (see MNT_TAB_PRINTFMT)
 * @source: (spec) device name or tag=value
 * @target: mountpoint
 * @fstype: filesystem type
 * @options: mount options
 * @freq: dump frequency in days
 * @passno: pass number on parallel fsck
 *
 * It's recommended to use this function rather than directly call fprintf() to
 * write an entry to mtab/fstab. All data in these files has to be properly
 * formatted (for example space within paths/tags has to be escaped, see
 * fstab(5) for more details).
 *
 * Returns: return value from fprintf().
 */
int mnt_fprintf_line(	FILE *f,
			const char *fmt,
			const char *source,
			const char *target,
			const char *fstype,
			const char *options,
			int freq,
			int passno)
{
	char *m1 = NULL, *m2 = NULL, *m3 = NULL, *m4 = NULL;
	int rc = -1;

	if (!f || !fmt || !source || !target || !fstype || !options)
		return -1;

	m1 = mangle(source);
	m2 = mangle(target);
	m3 = mangle(fstype);
	m4 = mangle(options);

	if (!m1 || !m2 || !m3 || !m4)
		goto done;

	rc = fprintf(f, fmt, m1, m2, m3, m4, freq, passno);
done:
	free(m1);
	free(m2);
	free(m3);
	free(m4);

	return rc;
}

/**
 * mnt_fs_fprintf:
 * @fs: fstab/mtab/mountinfo entry
 * @f: FILE
 * @fmt: printf-like format string (see MNT_TAB_PRINTFMT)
 *
 * Returns: return value from fprintf().
 */
int mnt_fs_fprintf(mnt_fs *fs, FILE *f, const char *fmt)
{
	assert(fs);
	assert(f);
	assert(fmt);

	if (!fs || !f)
		return -1;

	return mnt_fprintf_line(f, fmt,
			mnt_fs_get_source(fs),
			mnt_fs_get_target(fs),
			mnt_fs_get_fstype(fs),
			mnt_fs_get_optstr(fs),
			mnt_fs_get_freq(fs),
			mnt_fs_get_passno(fs));
}

/**
 * mnt_fs_print_debug
 * @fs: fstab/mtab/mountinfo entry
 * @file: output
 *
 * Returns: 0 on success or -1 in case of error.
 */
int mnt_fs_print_debug(mnt_fs *fs, FILE *file)
{
	if (!fs)
		return -1;
	fprintf(file, "------ fs: %p\n", fs);
	fprintf(file, "source: %s\n", mnt_fs_get_source(fs));
	fprintf(file, "target: %s\n", mnt_fs_get_target(fs));
	fprintf(file, "fstype: %s\n", mnt_fs_get_fstype(fs));
	fprintf(file, "optstr: %s\n", mnt_fs_get_optstr(fs));
	fprintf(file, "freq:   %d\n", mnt_fs_get_freq(fs));
	fprintf(file, "pass:   %d\n", mnt_fs_get_passno(fs));
	fprintf(file, "id:     %d\n", mnt_fs_get_id(fs));
	fprintf(file, "parent: %d\n", mnt_fs_get_parent_id(fs));
	fprintf(file, "devno:  %d:%d\n", major(mnt_fs_get_devno(fs)),
					 minor(mnt_fs_get_devno(fs)));


	return 0;
}
