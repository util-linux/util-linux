/*
 * Copyright (C) 2008-2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 *
 * The mnt_fs is representation of one line in a fstab / mtab / mountinfo.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <blkid/blkid.h>

#include "nls.h"
#include "mountP.h"

/**
 * mnt_new_fs:
 *
 * Returns newly allocated mnt_file fs.
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
 * mnt_fs_get_srcpath:
 * @fs: mnt_file (fstab/mtab/mountinfo) fs
 *
 * The mount "source path" is:
 *	- a directory for 'bind' mounts (in fstab or mtab only)
 *	- a device name for standard mounts
 *	- NULL when path is not set (for example when TAG
 *	  (LABEL/UUID) is defined)
 *
 * See also mnt_fs_get_tag() and mnt_fs_get_source().
 *
 * Returns mount "source" path or NULL in case of error or when the path
 * is not defined.
 *
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
 * @fs: mnt_file (fstab/mtab/mountinfo) fs
 *
 * Returns mount "source". Note that the source could be unparsed TAG
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
 * Returns 0 on success or -1 in case of error.
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
 * The TAG is the first column in the fstab file. The TAG
 * or "srcpath" has to be always set for all entries.
 *
 * See also mnt_fs_get_source().
 *
 * Example:
 *	char *src;
 *	mnt_fs *fs = mnt_file_find_target(mf, "/home");
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
 *
 * Returns 0 on success or -1 in case that a TAG is not defined.
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
 * Returns pointer to mountpoint path or NULL
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
 * Returns 0 on success or -1 in case of error.
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
 * Returns pointer to filesystem type.
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
 * Returns 0 on success or -1 in case of error.
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
 * Returns pointer to mount option string with all options (FS and VFS)
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
 * Returns 0 on success or -1 in case of error.
 */
int mnt_fs_set_optstr(mnt_fs *fs, const char *optstr)
{
	assert(fs);

	if (!fs || !optstr)
		return -1;
	free(fs->optstr);
	free(fs->fs_optstr);
	free(fs->vfs_optstr);
	fs->fs_optstr = fs->vfs_optstr = NULL;

	fs->optstr = strdup(optstr);

	return fs->optstr ? 0 : -1;
}

/**
 * mnt_fs_get_fs_optstr:
 * @fs: fstab/mtab/mountinfo entry pointer
 *
 * This function works for "mountinfo" files only.
 *
 * Returns pointer to superblock (fs-depend) mount option string or NULL.
 */
const char *mnt_fs_get_fs_optstr(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->fs_optstr : NULL;
}

/**
 * mnt_fs_get_vfs_optstr:
 * @fs: fstab/mtab/mountinfo entry pointer
 *
 * This function works for "mountinfo" files only.
 *
 * Returns pointer to fs-independent (VFS) mount option string or NULL.
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
 * Returns "dump frequency in days".
 */
int mnt_fs_get_freq(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->freq : 0;
}

/**
 * mnt_fs_set_freq:
 * @fs: fstab/mtab/mountinfo entry pointer
 * @freq: dump frequency in days
 *
 * Returns 0 on success or -1 in case of error.
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
 * @fs: fstab/mtab/mountinfo entry pointer
 *
 * Returns "pass number on parallel fsck".
 */
int mnt_fs_get_passno(mnt_fs *fs)
{
	assert(fs);
	return fs ? fs->passno: 0;
}

/**
 * mnt_fs_set_passno:
 * @fs: fstab/mtab/mountinfo entry pointer
 * @passno: pass number
 *
 * Returns 0 on success or -1 in case of error.
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
 * mnt_fs_get_option:
 * @fs: fstab/mtab/mountinfo entry pointer
 * @name: option name
 * @value: returns pointer to the begin of the value (e.g. name=VALUE) or NULL
 * @valsz: returns size of options value or 0
 *
 * Returns 0 on success, 1 when not found the @name or -1 in case of error.
 */
int mnt_fs_get_option(mnt_fs *fs, const char *name,
		char **value, size_t *valsz)
{
	char *optstr = (char *) mnt_fs_get_optstr(fs);
	return optstr ? mnt_optstr_get_option(optstr, name, value, valsz) : 1;
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
 * @fmt: printf-like format string (see MNT_MFILE_PRINTFMT)
 * @source: (spec) device name or tag=value
 * @target: mountpoint
 * @fstype: filesystem type
 * @options: mount options
 * @freq: dump frequency in days
 * @passno: pass number on parallel fsck
 *
 * Returns return value from fprintf().
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
 * @fmt: printf-like format string (see MNT_MFILE_PRINTFMT)
 *
 * Returns return value from fprintf().
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
 * Returns 0 on success or -1 in case of error.
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

	return 0;
}
