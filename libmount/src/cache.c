/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*
 * This file is part of libmount from util-linux project.
 *
 * Copyright (C) 2009-2018 Karel Zak <kzak@redhat.com>
 *
 * libmount is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 */

/**
 * SECTION: cache
 * @title: Cache
 * @short_description: paths and tags (UUID/LABEL) caching
 *
 * The cache is a very simple API for working with tags (LABEL, UUID, ...) and
 * paths. The cache uses libblkid as a backend for TAGs resolution.
 *
 * All returned paths are always canonicalized.
 */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <blkid.h>

/* sd-device is a replacement for libudev */
#ifdef USE_LIBMOUNT_UDEV_SUPPORT
# include <systemd/sd-device.h>
#endif

#include "canonicalize.h"
#include "mountP.h"
#include "loopdev.h"
#include "strutils.h"
#include "mangle.h"

/*
 * Canonicalized (resolved) paths & tags cache
 */
#define MNT_CACHE_CHUNKSZ	128

#define MNT_CACHE_ISTAG		(1 << 1) /* entry is TAG */
#define MNT_CACHE_ISPATH	(1 << 2) /* entry is path */
#define MNT_CACHE_TAGREAD	(1 << 3) /* tag read by mnt_cache_read_tags() */

/* path cache entry */
struct mnt_cache_entry {
	char			*key;	/* search key (e.g. uncanonicalized path) */
	char			*value;	/* value (e.g. canonicalized path) */
	int			flag;
};

struct libmnt_cache {
	struct mnt_cache_entry	*ents;
	size_t			nents;
	size_t			nallocs;
	int			refcount;
	int			probe_sb_extra;	/* extra BLKID_SUBLKS_* flags */

	/* blkid_evaluate_tag() works in two ways:
	 *
	 * 1/ all tags are evaluated by udev /dev/disk/by-* symlinks,
	 *    then the blkid_cache is NULL.
	 *
	 * 2/ all tags are read from blkid.tab and verified by /dev
	 *    scanning, then the blkid_cache is not NULL and then it's
	 *    better to reuse the blkid_cache.
	 */
	blkid_cache		bc;

	struct libmnt_table	*mountinfo;
};


struct libmnt_cachetag {
	const char *mnt_name;	/* tag name used by libmount */
	const char *blk_name;	/* tag name used by libblkid */
	const char *udev_name;	/* tag name used by udev db */
};

static const struct libmnt_cachetag mnttags[] =
{
	/* mount	blkid			udev */
	{ "LABEL",	"LABEL",		"ID_FS_LABEL_ENC" },
	{ "UUID",	"UUID",			"ID_FS_UUID_ENC" },
	{ "TYPE",	"TYPE",			"ID_FS_TYPE" },
	{ "PARTUUID",	"PART_ENTRY_UUID",	"ID_PART_ENTRY_UUID" },
	{ "PARTLABEL",	"PART_ENTRY_NAME",	"ID_PART_ENTRY_NAME" },

	{ NULL, NULL }
};

/**
 * mnt_new_cache:
 *
 * Returns: new struct libmnt_cache instance or NULL in case of ENOMEM error.
 */
struct libmnt_cache *mnt_new_cache(void)
{
	struct libmnt_cache *cache = calloc(1, sizeof(*cache));
	if (!cache)
		return NULL;
	DBG(CACHE, ul_debugobj(cache, "alloc"));
	cache->refcount = 1;
	return cache;
}

/**
 * mnt_free_cache:
 * @cache: pointer to struct libmnt_cache instance
 *
 * Deallocates the cache. This function does not care about reference count. Don't
 * use this function directly -- it's better to use mnt_unref_cache().
 */
void mnt_free_cache(struct libmnt_cache *cache)
{
	size_t i;

	if (!cache)
		return;

	DBG(CACHE, ul_debugobj(cache, "free [refcount=%d]", cache->refcount));

	for (i = 0; i < cache->nents; i++) {
		struct mnt_cache_entry *e = &cache->ents[i];
		if (e->value != e->key)
			free(e->value);
		free(e->key);
	}
	free(cache->ents);
	if (cache->bc)
		blkid_put_cache(cache->bc);
	free(cache);
}

/**
 * mnt_ref_cache:
 * @cache: cache pointer
 *
 * Increments reference counter.
 */
void mnt_ref_cache(struct libmnt_cache *cache)
{
	if (cache) {
		cache->refcount++;
		/*DBG(CACHE, ul_debugobj(cache, "ref=%d", cache->refcount));*/
	}
}

/**
 * mnt_unref_cache:
 * @cache: cache pointer
 *
 * De-increments reference counter, on zero the cache is automatically
 * deallocated by mnt_free_cache().
 */
void mnt_unref_cache(struct libmnt_cache *cache)
{
	if (cache) {
		cache->refcount--;
		/*DBG(CACHE, ul_debugobj(cache, "unref=%d", cache->refcount));*/
		if (cache->refcount <= 0) {
			mnt_unref_table(cache->mountinfo);

			mnt_free_cache(cache);
		}
	}
}

/**
 * mnt_cache_set_targets:
 * @cache: cache pointer
 * @mountinfo: table with already canonicalized mountpoints
 *
 * Add to @cache reference to @mountinfo. This can be used to avoid unnecessary paths
 * canonicalization in mnt_resolve_target().
 *
 * Returns: negative number in case of error, or 0 o success.
 */
int mnt_cache_set_targets(struct libmnt_cache *cache,
				struct libmnt_table *mountinfo)
{
	if (!cache)
		return -EINVAL;

	mnt_ref_table(mountinfo);
	mnt_unref_table(cache->mountinfo);
	cache->mountinfo = mountinfo;
	return 0;
}

/**
 * mnt_cache_set_sbprobe:
 * @cache: cache pointer
 * @flags: BLKID_SUBLKS_* flags
 *
 * Add extra flags to the libblkid prober. Don't use if not sure.
 *
 * Returns: negative number in case of error, or 0 o success.
 */
int mnt_cache_set_sbprobe(struct libmnt_cache *cache, int flags)
{
	if (!cache)
		return -EINVAL;

	cache->probe_sb_extra = flags;
	return 0;
}

/* note that the @key could be the same pointer as @value */
static int cache_add_entry(struct libmnt_cache *cache, char *key,
					char *value, int flag)
{
	struct mnt_cache_entry *e;

	assert(cache);
	assert(value);
	assert(key);

	if (cache->nents == cache->nallocs) {
		size_t sz = cache->nallocs + MNT_CACHE_CHUNKSZ;

		e = reallocarray(cache->ents, sz, sizeof(struct mnt_cache_entry));
		if (!e)
			return -ENOMEM;
		cache->ents = e;
		cache->nallocs = sz;
	}

	e = &cache->ents[cache->nents];
	e->key = key;
	e->value = value;
	e->flag = flag;
	cache->nents++;

	DBG(CACHE, ul_debugobj(cache, "add entry [%2zd] (%s): %s: %s",
			cache->nents,
			(flag & MNT_CACHE_ISPATH) ? "path" : "tag",
			value, key));
	return 0;
}

/* add tag to the cache, @devname has to be an allocated string */
static int cache_add_tag(struct libmnt_cache *cache, const char *tagname,
				const char *tagval, char *devname, int flag)
{
	size_t tksz, vlsz;
	char *key;
	int rc;

	assert(cache);
	assert(devname);
	assert(tagname);
	assert(tagval);

	/* add into cache -- cache format for TAGs is
	 *	key    = "TAG_NAME\0TAG_VALUE\0"
	 *	value  = "/dev/foo"
	 */
	tksz = strlen(tagname);
	vlsz = strlen(tagval);

	key = malloc(tksz + vlsz + 2);
	if (!key)
		return -ENOMEM;

	memcpy(key, tagname, tksz + 1);	   /* include '\0' */
	memcpy(key + tksz + 1, tagval, vlsz + 1);

	rc = cache_add_entry(cache, key, devname, flag | MNT_CACHE_ISTAG);
	if (!rc)
		return 0;

	free(key);
	return rc;
}


/*
 * Returns cached canonicalized path or NULL.
 */
static const char *cache_find_path(struct libmnt_cache *cache, const char *path)
{
	size_t i;

	if (!cache || !path)
		return NULL;

	for (i = 0; i < cache->nents; i++) {
		struct mnt_cache_entry *e = &cache->ents[i];
		if (!(e->flag & MNT_CACHE_ISPATH))
			continue;
		if (streq_paths(path, e->key))
			return e->value;
	}
	return NULL;
}

/*
 * Returns cached path or NULL.
 */
static const char *cache_find_tag(struct libmnt_cache *cache,
			const char *token, const char *value)
{
	size_t i;
	size_t tksz;

	if (!cache || !token || !value)
		return NULL;

	tksz = strlen(token);

	for (i = 0; i < cache->nents; i++) {
		struct mnt_cache_entry *e = &cache->ents[i];
		if (!(e->flag & MNT_CACHE_ISTAG))
			continue;
		if (strcmp(token, e->key) == 0 &&
		    strcmp(value, e->key + tksz + 1) == 0)
			return e->value;
	}
	return NULL;
}

static char *cache_find_tag_value(struct libmnt_cache *cache,
			const char *devname, const char *token)
{
	size_t i;

	assert(cache);
	assert(devname);
	assert(token);

	for (i = 0; i < cache->nents; i++) {
		struct mnt_cache_entry *e = &cache->ents[i];
		if (!(e->flag & MNT_CACHE_ISTAG))
			continue;
		if (strcmp(e->value, devname) == 0 &&	/* dev name */
		    strcmp(token, e->key) == 0)	/* tag name */
			return e->key + strlen(token) + 1;	/* tag value */
	}

	return NULL;
}

static bool is_device_cached(struct libmnt_cache *cache, const char *devname)
{
	size_t i;

	for (i = 0; i < cache->nents; i++) {
		struct mnt_cache_entry *e = &cache->ents[i];
		if (!(e->flag & MNT_CACHE_TAGREAD))
			continue;
		if (strcmp(e->value, devname) == 0)
			return 1; /* already in cache */
	}

	return 0;
}

/*
 * read data from libblkid into local cache
 * returns: <0 on error; 0 success; 1 nothing
*/
static int read_from_blkid(struct libmnt_cache *cache, const char *devname)
{
	blkid_probe pr;
	const struct libmnt_cachetag *t;
	size_t ntags = 0;
	int rc;
	char *cacheval = NULL;

	assert(cache);
	assert(devname);

	DBG(CACHE, ul_debugobj(cache, "%s: reading from blkid", devname));

	pr =  blkid_new_probe_from_filename(devname);
	if (!pr)
		return -EINVAL;

	blkid_probe_enable_superblocks(pr, 1);
	blkid_probe_set_superblocks_flags(pr,
			BLKID_SUBLKS_LABEL | BLKID_SUBLKS_UUID |
			BLKID_SUBLKS_TYPE | cache->probe_sb_extra);

	blkid_probe_enable_partitions(pr, 1);
	blkid_probe_set_partitions_flags(pr, BLKID_PARTS_ENTRY_DETAILS);

	rc = blkid_do_safeprobe(pr);
	if (rc)
		goto done;

	for (t = mnttags; t && t->mnt_name; t++) {
		const char *data;

		if (cache_find_tag_value(cache, devname, t->mnt_name))
			continue;
		if (blkid_probe_lookup_value(pr, t->blk_name, &data, NULL))
			continue;

		cacheval = strdup(devname);
		rc = !cacheval ? -ENOMEM :
			cache_add_tag(cache, t->mnt_name, data, cacheval, MNT_CACHE_TAGREAD);
		if (rc)
			break;
		ntags++;
		cacheval = NULL;	/* stored into cache */
	}

done:
	DBG(CACHE, ul_debugobj(cache, "\tread %zd tags [rc=%d]", ntags, rc));
	blkid_free_probe(pr);
	free(cacheval);

	return rc ? rc : ntags ? 0 : 1;
}

#ifdef USE_LIBMOUNT_UDEV_SUPPORT
/*
 * read data from udev into local cache
 * returns: <0 on error; 0 success; 1 nothing
 */
static int read_from_udev(struct libmnt_cache *cache, const char *devname)
{
	sd_device *sd = NULL;
	const struct libmnt_cachetag *t;
	size_t ntags = 0;
	char *tagval = NULL, *cacheval = NULL;
	int rc;

	assert(cache);
	assert(devname);

	rc = sd_device_new_from_devname(&sd, devname);
	if (rc < 0)
		return rc;

	DBG(CACHE, ul_debugobj(cache, "%s: reading from udev", devname));

	for (t = mnttags; t && t->mnt_name; t++) {
		const char *data;

		if (cache_find_tag_value(cache, devname, t->mnt_name))
			continue;
		if (sd_device_get_property_value(sd, t->udev_name, &data) < 0)
			continue;

		tagval = strdup(data); /* temporary for unhexmangle() */
		cacheval = strdup(devname);

		if (tagval && cacheval) {
			unhexmangle_string(tagval);
			rc = cache_add_tag(cache, t->mnt_name,
					tagval, cacheval, MNT_CACHE_TAGREAD);
		} else
			rc = -ENOMEM;
		if (rc)
			break;
		ntags++;
		cacheval = NULL; /* stored into cache */
		free(tagval), tagval = NULL;
	}

	DBG(CACHE, ul_debugobj(cache, "\tread %zd tags [rc=%d]", ntags, rc));
	sd_device_unref(sd);
	free(cacheval);
	free(tagval);

	return rc ? rc : ntags ? 0 : 1;
}
#endif /* USE_LIBMOUNT_UDEV_SUPPORT */


/**
 * mnt_cache_read_tags
 * @cache: pointer to struct libmnt_cache instance
 * @devname: path device
 *
 * Reads @devname information into the @cache.
 *
 * Returns: 0 if at least one tag was added, 1 if no tag was added or
 *          negative number in case of error.
 */
int mnt_cache_read_tags(struct libmnt_cache *cache, const char *devname)
{
	if (!cache || !devname)
		return -EINVAL;

	DBG(CACHE, ul_debugobj(cache, "tags for %s requested", devname));

	if (is_device_cached(cache, devname))
		return 0;

#ifdef USE_LIBMOUNT_UDEV_SUPPORT
	if (read_from_udev(cache, devname) == 0)
		return 0;
#endif
	return read_from_blkid(cache, devname);
}

/**
 * mnt_cache_device_has_tag:
 * @cache: paths cache
 * @devname: path to the device
 * @token: tag name (e.g "LABEL")
 * @value: tag value
 *
 * Look up @cache to check if @tag+@value are associated with @devname.
 *
 * Returns: 1 on success or 0.
 */
int mnt_cache_device_has_tag(struct libmnt_cache *cache, const char *devname,
				const char *token, const char *value)
{
	const char *path = cache_find_tag(cache, token, value);

	if (path && devname && strcmp(path, devname) == 0)
		return 1;
	return 0;
}

static int __mnt_cache_find_tag_value(struct libmnt_cache *cache,
		const char *devname, const char *token, char **data)
{
	int rc = 0;

	if (!cache || !devname || !token || !data)
		return -EINVAL;

	rc = mnt_cache_read_tags(cache, devname);
	if (rc)
		return rc;

	*data = cache_find_tag_value(cache, devname, token);
	return *data ? 0 : -1;
}

/**
 * mnt_cache_find_tag_value:
 * @cache: cache for results
 * @devname: device name
 * @token: tag name ("LABEL", "UUID", ...)
 *
 * Returns: LABEL or UUID for the @devname or NULL in case of error.
 */
char *mnt_cache_find_tag_value(struct libmnt_cache *cache,
		const char *devname, const char *token)
{
	char *data = NULL;

	if (__mnt_cache_find_tag_value(cache, devname, token, &data) == 0)
		return data;
	return NULL;
}

static char *fstype_from_cache(const char *devname, struct libmnt_cache *cache)
{
	char *val = NULL;

	assert(cache);

	if (__mnt_cache_find_tag_value(cache, devname, "TYPE", &val) != 0)
		return NULL;
	return val;
}

static char *fstype_from_blkid(const char *devname, int *ambi)
{
	blkid_probe pr;
	const char *data;
	char *type = NULL;
	int rc;

	pr =  blkid_new_probe_from_filename(devname);
	if (!pr)
		return NULL;

	blkid_probe_enable_superblocks(pr, 1);
	blkid_probe_set_superblocks_flags(pr, BLKID_SUBLKS_TYPE);

	rc = blkid_do_safeprobe(pr);

	if (!rc && !blkid_probe_lookup_value(pr, "TYPE", &data, NULL))
		type = strdup(data);
	if (ambi)
		*ambi = rc == -2 ? TRUE : FALSE;

	blkid_free_probe(pr);
	return type;
}

/**
 * mnt_get_fstype:
 * @devname: device name
 * @ambi: returns TRUE if probing result is ambivalent (optional argument)
 * @cache: cache for results or NULL
 *
 * Note: If the cache is not specified, it reads the file system type from the
 * device, and in this case, there is no optimization like udev db, etc. *
 *
 * Returns: filesystem type or NULL in case of error. The result has to be
 * deallocated by free() if @cache is NULL.
 */
char *mnt_get_fstype(const char *devname, int *ambi, struct libmnt_cache *cache)
{
	DBG(CACHE, ul_debugobj(cache, "get %s FS type", devname));

	if (cache)
		return fstype_from_cache(devname, cache);

	return fstype_from_blkid(devname, ambi);
}

static char *canonicalize_path_and_cache(const char *path,
						struct libmnt_cache *cache)
{
	char *p;
	char *key;
	char *value;

	DBG(CACHE, ul_debugobj(cache, "canonicalize path %s", path));
	p = ul_canonicalize_path(path);

	if (p && cache) {
		value = p;
		key = strcmp(path, p) == 0 ? value : strdup(path);

		if (!key || !value)
			goto error;

		if (cache_add_entry(cache, key, value,
				MNT_CACHE_ISPATH))
			goto error;
	}

	return p;
error:
	if (value != key)
		free(value);
	free(key);
	return NULL;
}

/**
 * mnt_resolve_path:
 * @path: "native" path
 * @cache: cache for results or NULL
 *
 * Converts path:
 *	- to the absolute path
 *	- /dev/dm-N to /dev/mapper/name
 *
 * Returns: absolute path or NULL in case of error. The result has to be
 * deallocated by free() if @cache is NULL.
 */
char *mnt_resolve_path(const char *path, struct libmnt_cache *cache)
{
	char *p = NULL;

	/*DBG(CACHE, ul_debugobj(cache, "resolving path %s", path));*/

	if (!path)
		return NULL;
	if (cache)
		p = (char *) cache_find_path(cache, path);
	if (!p)
		p = canonicalize_path_and_cache(path, cache);

	return p;
}

/**
 * mnt_resolve_target:
 * @path: "native" path, a potential mount point
 * @cache: cache for results or NULL.
 *
 * Like mnt_resolve_path(), unless @cache is not NULL and
 * mnt_cache_set_targets(cache, mountinfo) was called: if @path is found in the
 * cached @mountinfo and the matching entry was provided by the kernel, assume that
 * @path is already canonicalized. By avoiding a call to realpath(2) on
 * known mount points, there is a lower risk of stepping on a stale mount
 * point, which can result in an application freeze. This is also faster in
 * general, as stat(2) on a mount point is slower than on a regular file.
 *
 * Returns: absolute path or NULL in case of error. The result has to be
 * deallocated by free() if @cache is NULL.
 */
char *mnt_resolve_target(const char *path, struct libmnt_cache *cache)
{
	char *p = NULL;

	if (!path)
		return NULL;

	/*DBG(CACHE, ul_debugobj(cache, "resolving target %s", path));*/

	if (!cache || !cache->mountinfo)
		return mnt_resolve_path(path, cache);

	p = (char *) cache_find_path(cache, path);
	if (p)
		return p;

	{
		struct libmnt_iter itr;
		struct libmnt_fs *fs = NULL;

		mnt_reset_iter(&itr, MNT_ITER_BACKWARD);
		while (mnt_table_next_fs(cache->mountinfo, &itr, &fs) == 0) {

			if (!mnt_fs_is_kernel(fs)
			     || mnt_fs_is_swaparea(fs)
			     || !mnt_fs_streq_target(fs, path))
				continue;

			p = strdup(path);
			if (!p)
				return NULL;	/* ENOMEM */

			if (cache_add_entry(cache, p, p, MNT_CACHE_ISPATH)) {
				free(p);
				return NULL;	/* ENOMEM */
			}
			break;
		}
	}

	if (!p)
		p = canonicalize_path_and_cache(path, cache);
	return p;
}

/**
 * mnt_pretty_path:
 * @path: any path
 * @cache: NULL or pointer to the cache
 *
 * Converts path:
 *	- to the absolute path
 *	- /dev/dm-N to /dev/mapper/name
 *	- /dev/loopN to the loop backing filename
 *	- empty path (NULL) to 'none'
 *
 * Returns: newly allocated string with path, result always has to be deallocated
 *          by free().
 */
char *mnt_pretty_path(const char *path, struct libmnt_cache *cache)
{
	char *pretty = mnt_resolve_path(path, cache);

	if (!pretty)
		return strdup("none");

#ifdef __linux__
	/* users assume backing file name rather than /dev/loopN in
	 * output if the device has been initialized by mount(8).
	 */
	if (strncmp(pretty, "/dev/loop", 9) == 0) {
		struct loopdev_cxt lc;

		if (loopcxt_init(&lc, 0) || loopcxt_set_device(&lc, pretty))
			goto done;

		if (loopcxt_is_autoclear(&lc)) {
			char *tmp = loopcxt_get_backing_file(&lc);
			if (tmp) {
				loopcxt_deinit(&lc);
				if (!cache)
					free(pretty);	/* not cached, deallocate */
				return tmp;		/* return backing file */
			}
		}
		loopcxt_deinit(&lc);

	}
#endif

done:
	/* don't return pointer to the cache, allocate a new string */
	return cache ? strdup(pretty) : pretty;
}

/**
 * mnt_resolve_tag:
 * @token: tag name
 * @value: tag value
 * @cache: for results or NULL
 *
 * Returns: device name or NULL in case of error. The result has to be
 * deallocated by free() if @cache is NULL.
 */
char *mnt_resolve_tag(const char *token, const char *value,
		      struct libmnt_cache *cache)
{
	char *p = NULL;

	/*DBG(CACHE, ul_debugobj(cache, "resolving tag token=%s value=%s",
				token, value));*/

	if (!token || !value)
		return NULL;

	if (cache)
		p = (char *) cache_find_tag(cache, token, value);

	if (!p) {
		/* returns newly allocated string */
		p = blkid_evaluate_tag(token, value, cache ? &cache->bc : NULL);

		if (p && cache &&
		    cache_add_tag(cache, token, value, p, 0))
				goto error;
	}

	return p;
error:
	free(p);
	return NULL;
}



/**
 * mnt_resolve_spec:
 * @spec: path or tag
 * @cache: paths cache
 *
 * Returns: canonicalized path or NULL. The result has to be
 * deallocated by free() if @cache is NULL.
 */
char *mnt_resolve_spec(const char *spec, struct libmnt_cache *cache)
{
	char *cn = NULL;
	char *t = NULL, *v = NULL;

	if (!spec)
		return NULL;

	if (blkid_parse_tag_string(spec, &t, &v) == 0 && mnt_valid_tagname(t))
		cn = mnt_resolve_tag(t, v, cache);
	else
		cn = mnt_resolve_path(spec, cache);

	free(t);
	free(v);
	return cn;
}


#ifdef TEST_PROGRAM

static int test_resolve_path(struct libmnt_test *ts __attribute__((unused)),
			     int argc __attribute__((unused)),
			     char *argv[] __attribute__((unused)))
{
	char line[BUFSIZ];
	struct libmnt_cache *cache;

	cache = mnt_new_cache();
	if (!cache)
		return -ENOMEM;

	while(fgets(line, sizeof(line), stdin)) {
		size_t sz = strlen(line);
		char *p;

		if (sz > 0 && line[sz - 1] == '\n')
			line[sz - 1] = '\0';

		p = mnt_resolve_path(line, cache);
		printf("%s : %s\n", line, p);
	}
	mnt_unref_cache(cache);
	return 0;
}

static int test_resolve_spec(struct libmnt_test *ts __attribute__((unused)),
			     int argc __attribute__((unused)),
			     char *argv[] __attribute__((unused)))
{
	char line[BUFSIZ];
	struct libmnt_cache *cache;

	cache = mnt_new_cache();
	if (!cache)
		return -ENOMEM;

	while(fgets(line, sizeof(line), stdin)) {
		size_t sz = strlen(line);
		char *p;

		if (sz > 0 && line[sz - 1] == '\n')
			line[sz - 1] = '\0';

		p = mnt_resolve_spec(line, cache);
		printf("%s : %s\n", line, p);
	}
	mnt_unref_cache(cache);
	return 0;
}

static int test_read_tags(struct libmnt_test *ts __attribute__((unused)),
			  int argc __attribute__((unused)),
			  char *argv[] __attribute__((unused)))
{
	char line[BUFSIZ];
	struct libmnt_cache *cache;
	size_t i;

	cache = mnt_new_cache();
	if (!cache)
		return -ENOMEM;

	while(fgets(line, sizeof(line), stdin)) {
		size_t sz = strlen(line);
		char *t = NULL, *v = NULL;

		if (sz > 0 && line[sz - 1] == '\n')
			line[sz - 1] = '\0';

		if (!strcmp(line, "quit"))
			break;

		if (*line == '/') {
			if (mnt_cache_read_tags(cache, line) < 0)
				fprintf(stderr, "%s: read tags failed\n", line);

		} else if (blkid_parse_tag_string(line, &t, &v) == 0) {
			const char *cn = NULL;

			if (mnt_valid_tagname(t))
				cn = cache_find_tag(cache, t, v);
			free(t);
			free(v);

			if (cn)
				printf("%s: %s\n", line, cn);
			else
				printf("%s: not cached\n", line);
		}
	}

	for (i = 0; i < cache->nents; i++) {
		struct mnt_cache_entry *e = &cache->ents[i];
		if (!(e->flag & MNT_CACHE_ISTAG))
			continue;

		printf("%15s : %5s : %s\n", e->value, e->key,
				e->key + strlen(e->key) + 1);
	}

	mnt_unref_cache(cache);
	return 0;

}

int main(int argc, char *argv[])
{
	struct libmnt_test ts[] = {
		{ "--resolve-path", test_resolve_path, "  resolve paths from stdin" },
		{ "--resolve-spec", test_resolve_spec, "  evaluate specs from stdin" },
		{ "--read-tags", test_read_tags,       "  read devname or TAG from stdin (\"quit\" to exit)" },
		{ NULL }
	};

	return mnt_run_test(ts, argc, argv);
}
#endif
