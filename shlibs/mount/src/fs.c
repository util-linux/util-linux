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
#include <stddef.h>

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
	free(fs->bindsrc);
	free(fs->tagname);
	free(fs->tagval);
	free(fs->root);
	free(fs->target);
	free(fs->fstype);
	free(fs->optstr);
	free(fs->vfs_optstr);
	free(fs->fs_optstr);

	free(fs);
}

static inline int cpy_str_item(void *new, const void *old, size_t offset)
{
	char **o = (char **) (old + offset);
	char **n = (char **) (new + offset);

	if (!*o)
		return 0;	/* source (old) is empty */

	*n = strdup(*o);
	if (!*n)
		return -ENOMEM;
	return 0;
}

/**
 * mnt_copy_fs:
 * @fs: source FS
 *
 * This function does not copy userdata (se mnt_fs_set_userdata()). A new copy is
 * not linked with any existing mnt_tab.
 *
 * Returns: copy of @fs
 */
mnt_fs *mnt_copy_fs(const mnt_fs *fs)
{
	mnt_fs *n = mnt_new_fs();

	if (!n)
		return NULL;

	n->id         = fs->id;
	n->parent     = fs->parent;
	n->devno      = fs->devno;

	if (cpy_str_item(n, fs, offsetof(struct _mnt_fs, source)))
		goto err;
	if (cpy_str_item(n, fs, offsetof(struct _mnt_fs, tagname)))
		goto err;
	if (cpy_str_item(n, fs, offsetof(struct _mnt_fs, tagval)))
		goto err;
	if (cpy_str_item(n, fs, offsetof(struct _mnt_fs, root)))
		goto err;
	if (cpy_str_item(n, fs, offsetof(struct _mnt_fs, target)))
		goto err;
	if (cpy_str_item(n, fs, offsetof(struct _mnt_fs, fstype)))
		goto err;
	if (cpy_str_item(n, fs, offsetof(struct _mnt_fs, optstr)))
		goto err;
	if (cpy_str_item(n, fs, offsetof(struct _mnt_fs, vfs_optstr)))
		goto err;
	if (cpy_str_item(n, fs, offsetof(struct _mnt_fs, fs_optstr)))
		goto err;
	n->freq       = fs->freq;
	n->passno     = fs->passno;
	n->flags      = fs->flags;

	return n;
err:
	mnt_free_fs(n);
	return NULL;
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
 * Returns: 0 or negative number in case of error (if @fs is NULL).
 */
int mnt_fs_set_userdata(mnt_fs *fs, void *data)
{
	if (!fs)
		return -EINVAL;
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
int __mnt_fs_set_source_ptr(mnt_fs *fs, char *source)
{
	char *t = NULL, *v = NULL;

	assert(fs);

	if (source && !strcmp(source, "none"))
		source = NULL;

	if (source && strchr(source, '=')) {
		if (blkid_parse_tag_string(source, &t, &v) != 0)
			return -1;
	}

	if (fs->source != source)
		free(fs->source);

	free(fs->tagname);
	free(fs->tagval);

	fs->source = source;
	fs->tagname = t;
	fs->tagval = v;
	return 0;
}

/**
 * mnt_fs_set_source:
 * @fs: fstab/mtab/mountinfo entry
 * @source: new source
 *
 * This function creates a private copy (strdup()) of @source.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_source(mnt_fs *fs, const char *source)
{
	char *p;
	int rc;

	if (!fs && !source)
		return -EINVAL;
	p = strdup(source);
	if (!p)
		return -ENOMEM;

	rc = __mnt_fs_set_source_ptr(fs, p);
	if (rc)
		free(p);
	return rc;
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
 * Returns: 0 on success or negative number in case that a TAG is not defined.
 */
int mnt_fs_get_tag(mnt_fs *fs, const char **name, const char **value)
{
	if (fs == NULL || !fs->tagname)
		return -EINVAL;
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
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_target(mnt_fs *fs, const char *target)
{
	char *p;

	assert(fs);

	if (!fs || !target)
		return -EINVAL;
	p = strdup(target);
	if (!p)
		return -ENOMEM;
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
int __mnt_fs_set_fstype_ptr(mnt_fs *fs, char *fstype)
{
	assert(fs);

	if (fstype != fs->fstype)
		free(fs->fstype);

	fs->fstype = fstype;
	fs->flags &= ~MNT_FS_PSEUDO;
	fs->flags &= ~MNT_FS_NET;

	/* save info about pseudo filesystems */
	if (fs->fstype) {
		if (mnt_fstype_is_pseudofs(fs->fstype))
			fs->flags |= MNT_FS_PSEUDO;
		else if (mnt_fstype_is_netfs(fs->fstype))
			fs->flags |= MNT_FS_NET;
		else if (!strcmp(fs->fstype, "swap"))
			fs->flags |= MNT_FS_SWAP;
	}
	return 0;
}

/**
 * mnt_fs_set_fstype:
 * @fs: fstab/mtab/mountinfo entry
 * @fstype: filesystem type
 *
 * This function creates a private copy (strdup()) of @fstype.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_fstype(mnt_fs *fs, const char *fstype)
{
	char *p = NULL;
	int rc;

	if (!fs)
		return -EINVAL;
	if (fstype) {
		p = strdup(fstype);
		if (!p)
			return -ENOMEM;
	}
	rc =  __mnt_fs_set_fstype_ptr(fs, p);
	if (rc)
		free(p);
	return rc;
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

int __mnt_fs_set_optstr_ptr(mnt_fs *fs, char *ptr, int split)
{
	char *v = NULL, *f = NULL;

	assert(fs);

	if (!fs)
		return -EINVAL;
	if (ptr) {
		int rc = 0;

		if (split)
			rc = mnt_split_optstr((char *) ptr, NULL, &v, &f, 0, 0);
		if (rc)
			return rc;
	}

	if (ptr != fs->optstr)
		free(fs->optstr);

	free(fs->fs_optstr);
	free(fs->vfs_optstr);

	fs->optstr = ptr;
	fs->fs_optstr = f;
	fs->vfs_optstr = v;
	return 0;
}

int __mnt_fs_set_optstr(mnt_fs *fs, const char *optstr, int split)
{
	char *p;
	int rc;

	assert(fs);

	p = strdup(optstr);
	if (!p)
		return -ENOMEM;
	rc = __mnt_fs_set_optstr_ptr(fs, p, split);
	if (rc)
		free(p);		/* error, deallocate */
	return rc;
}

/**
 * mnt_fs_set_optstr:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: options string
 *
 * This function creates a private copy of @optstr. The function also updates
 * VFS and FS mount options.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_optstr(mnt_fs *fs, const char *optstr)
{
	return __mnt_fs_set_optstr(fs, optstr, TRUE);
}

/**
 * mnt_fs_append_userspace_optstr:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: options string
 *
 * This function appends @optstr to the current list of the mount options. The VFS and
 * FS mount options are not modified.
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_append_userspace_optstr(mnt_fs *fs, const char *optstr)
{
	assert(fs);
	if (!fs || !optstr)
		return -EINVAL;
	return mnt_optstr_append_option(&fs->optstr, optstr, NULL);
}

/**
 * mnt_fs_append_optstr:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: mount options
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_append_optstr(mnt_fs *fs, const char *optstr)
{
	char *v = NULL, *f = NULL;
	int rc;

	assert(fs);

	if (!fs)
		return -EINVAL;
	if (!optstr)
		return 0;

	rc = mnt_split_optstr((char *) optstr, NULL, &v, &f, 0, 0);
	if (!rc)
		rc = mnt_optstr_append_option(&fs->optstr, optstr, NULL);
	if (!rc && v)
		rc = mnt_optstr_append_option(&fs->vfs_optstr, v, NULL);
	if (!rc && f)
	       rc = mnt_optstr_append_option(&fs->fs_optstr, f, NULL);

	return rc;
}

/**
 * mnt_fs_prepend_optstr:
 * @fs: fstab/mtab/mountinfo entry
 * @optstr: mount options
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_prepend_optstr(mnt_fs *fs, const char *optstr)
{
	char *v = NULL, *f = NULL;
	int rc;

	assert(fs);

	if (!fs)
		return -EINVAL;
	if (!optstr)
		return 0;

	rc = mnt_split_optstr((char *) optstr, NULL, &v, &f, 0, 0);
	if (!rc)
		rc = mnt_optstr_prepend_option(&fs->optstr, optstr, NULL);
	if (!rc && v)
		rc = mnt_optstr_prepend_option(&fs->vfs_optstr, v, NULL);
	if (!rc && f)
		rc = mnt_optstr_prepend_option(&fs->fs_optstr, f, NULL);

	return rc;
}

/**
 * mnt_fs_get_fs_optstr:
 * @fs: fstab/mtab/mountinfo entry pointer
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
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_freq(mnt_fs *fs, int freq)
{
	assert(fs);
	if (!fs)
		return -EINVAL;
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
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_passno(mnt_fs *fs, int passno)
{
	assert(fs);
	if (!fs)
		return -EINVAL;
	fs->passno = passno;
	return 0;
}

/**
 * mnt_fs_get_root:
 * @fs: /proc/self/mountinfo entry
 *
 * Returns: root of the mount within the filesystem or NULL
 */
const char *mnt_fs_get_root(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->root : NULL;
}

/**
 * mnt_fs_set_root:
 * @fs: mountinfo entry
 * @root: path
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_root(mnt_fs *fs, const char *root)
{
	char *p = NULL;

	assert(fs);
	if (!fs)
		return -EINVAL;
	if (root) {
		p = strdup(root);
		if (!p)
			return -ENOMEM;
	}
	free(fs->root);
	fs->root = p;
	return 0;
}

/**
 * mnt_fs_get_bindsrc:
 * @fs: /dev/.mount/utab entry
 *
 * Returns: full path that was used for mount(2) on MS_BIND
 */
const char *mnt_fs_get_bindsrc(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->bindsrc : NULL;
}

/**
 * mnt_fs_set_bindsrc:
 * @fs: filesystem
 * @src: path
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_set_bindsrc(mnt_fs *fs, const char *src)
{
	char *p = NULL;

	assert(fs);
	if (!fs)
		return -EINVAL;
	if (src) {
		p = strdup(src);
		if (!p)
			return -ENOMEM;
	}
	free(fs->bindsrc);
	fs->bindsrc = p;
	return 0;
}

/**
 * mnt_fs_get_id:
 * @fs: /proc/self/mountinfo entry
 *
 * Returns: mount ID (unique identifier of the mount) or negative number in case of error.
 */
int mnt_fs_get_id(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->id : -EINVAL;
}

/**
 * mnt_fs_get_parent_id:
 * @fs: /proc/self/mountinfo entry
 *
 * Returns: parent mount ID or negative number in case of error.
 */
int mnt_fs_get_parent_id(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->parent : -EINVAL;
}

/**
 * mnt_fs_get_devno:
 * @fs: /proc/self/mountinfo entry
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
 * Returns: 0 on success, 1 when not found the @name or negative number in case of error.
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

/**
 * mnt_fs_print_debug
 * @fs: fstab/mtab/mountinfo entry
 * @file: output
 *
 * Returns: 0 on success or negative number in case of error.
 */
int mnt_fs_print_debug(mnt_fs *fs, FILE *file)
{
	if (!fs)
		return -EINVAL;
	fprintf(file, "------ fs: %p\n", fs);
	fprintf(file, "source: %s\n", mnt_fs_get_source(fs));
	fprintf(file, "target: %s\n", mnt_fs_get_target(fs));
	fprintf(file, "fstype: %s\n", mnt_fs_get_fstype(fs));
	fprintf(file, "optstr: %s\n", mnt_fs_get_optstr(fs));

	if (mnt_fs_get_root(fs))
		fprintf(file, "root:   %s\n", mnt_fs_get_root(fs));
	if (mnt_fs_get_bindsrc(fs))
		fprintf(file, "bindsrc: %s\n", mnt_fs_get_bindsrc(fs));
	if (mnt_fs_get_freq(fs))
		fprintf(file, "freq:   %d\n", mnt_fs_get_freq(fs));
	if (mnt_fs_get_passno(fs))
		fprintf(file, "pass:   %d\n", mnt_fs_get_passno(fs));
	if (mnt_fs_get_id(fs))
		fprintf(file, "id:     %d\n", mnt_fs_get_id(fs));
	if (mnt_fs_get_parent_id(fs))
		fprintf(file, "parent: %d\n", mnt_fs_get_parent_id(fs));
	if (mnt_fs_get_devno(fs))
		fprintf(file, "devno:  %d:%d\n", major(mnt_fs_get_devno(fs)),
						 minor(mnt_fs_get_devno(fs)));
	return 0;
}
