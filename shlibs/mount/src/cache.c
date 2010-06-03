/*
 * Copyright (C) 2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: cache
 * @title: Cache
 * @short_description: paths and tags (UUID/LABEL) caching
 *
 * The cache is a very simple API for work with tags (LABEL, UUID, ...) and
 * paths. The cache uses libblkid as a backend from TAGs resolution.
 *
 * All returned paths are always canonicalized.
 */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <blkid.h>

#include "canonicalize.h"
#include "mountP.h"

/*
 * Canonicalized (resolved) paths & tags cache
 */
#define MNT_CACHE_CHUNKSZ	128

#define MNT_CACHE_ISTAG		(1 << 1) /* entry is TAG */
#define MNT_CACHE_ISPATH	(1 << 2) /* entry is path */
#define MNT_CACHE_TAGREAD	(1 << 3) /* tag read by mnt_cache_read_tags() */

/* path cache entry */
struct mnt_cache_entry {
	char			*native;	/* the original path */
	char			*real;		/* canonicalized path */
	int			flag;
};

struct _mnt_cache {
	struct mnt_cache_entry	*ents;
	size_t			nents;
	size_t			nallocs;

	/* blkid_evaluate_tag() works in two ways:
	 *
	 * 1/ all tags are evaluated by udev /dev/disk/by-* symlinks,
	 *    then the blkid_cache is NULL.
	 *
	 * 2/ all tags are read from /etc/blkid.tab and verified by /dev
	 *    scanning, then the blkid_cache is not NULL and then it's
	 *    better to reuse the blkid_cache.
	 */
	blkid_cache		bc;
};

/**
 * mnt_new_cache:
 *
 * Returns: new mnt_cache instance or NULL in case of ENOMEM error.
 */
mnt_cache *mnt_new_cache(void)
{
	return calloc(1, sizeof(struct _mnt_cache));
}

/**
 * mnt_free_cache:
 * @cache: pointer to mnt_cache instance
 *
 * Deallocates the cache.
 */
void mnt_free_cache(mnt_cache *cache)
{
	int i;

	if (!cache)
		return;
	for (i = 0; i < cache->nents; i++) {
		struct mnt_cache_entry *e = &cache->ents[i];
		if (e->real != e->native)
			free(e->real);
		free(e->native);
	}
	free(cache->ents);
	if (cache->bc)
		blkid_put_cache(cache->bc);
	free(cache);
}

/* note that the @native could be tha same pointer as @real */
static int mnt_cache_add_entry(mnt_cache *cache, char *native,
					char *real, int flag)
{
	struct mnt_cache_entry *e;

	assert(cache);
	assert(real);
	assert(native);

	if (cache->nents == cache->nallocs) {
		size_t sz = cache->nallocs + MNT_CACHE_CHUNKSZ;

		e = realloc(cache->ents, sz * sizeof(struct mnt_cache_entry));
		if (!e)
			return -1;
		cache->ents = e;
		cache->nallocs = sz;
	}

	e = &cache->ents[cache->nents];
	e->native = native;
	e->real = real;
	e->flag = flag;
	cache->nents++;

	DBG(DEBUG_CACHE,
		printf("cache: add entry[%2zd] (%s): %s: %s\n",
			cache->nents,
			(flag & MNT_CACHE_ISPATH) ? "path" : "tag",
			real, native));
	return 0;
}

/* add tag to the cache, @real has to be allocated string */
static int mnt_cache_add_tag(mnt_cache *cache, const char *token,
				const char *value, char *real, int flag)
{
	size_t tksz, vlsz;
	char *native;

	assert(cache);
	assert(real);
	assert(token);
	assert(value);

	/* add into cache -- cache format for TAGs is
	 *	native = "NAME\0VALUE\0"
	 *	real   = "/dev/foo"
	 */
	tksz = strlen(token);
	vlsz = strlen(value);

	native = malloc(tksz + vlsz + 2);
	if (!native)
		goto error;

	memcpy(native, token, tksz + 1);	   /* include '\0' */
	memcpy(native + tksz + 1, value, vlsz + 1);

	if (mnt_cache_add_entry(cache, native, real, flag | MNT_CACHE_ISTAG))
		goto error;
	return 0;
error:
	free(native);
	return -1;
}


/**
 * mnt_cache_find_path:
 * @cache: pointer to mnt_cache instance
 * @path: "native" (non-canonicalized) path
 *
 * Returns: cached canonicalized path or NULL.
 */
const char *mnt_cache_find_path(mnt_cache *cache, const char *path)
{
	int i;

	assert(cache);
	assert(path);

	if (!cache || !path)
		return NULL;

	for (i = 0; i < cache->nents; i++) {
		struct mnt_cache_entry *e = &cache->ents[i];
		if (!(e->flag & MNT_CACHE_ISPATH))
			continue;
		if (strcmp(path, e->native) == 0)
			return e->real;
	}
	return NULL;
}

/**
 * mnt_cache_find_tag:
 * @cache: pointer to mnt_cache instance
 * @token: tag name
 * @value: tag value
 *
 * Returns: cached path or NULL.
 */
const char *mnt_cache_find_tag(mnt_cache *cache,
			const char *token, const char *value)
{
	int i;
	size_t tksz;

	assert(cache);
	assert(token);
	assert(value);

	if (!cache || !token || !value)
		return NULL;

	tksz = strlen(token);

	for (i = 0; i < cache->nents; i++) {
		struct mnt_cache_entry *e = &cache->ents[i];
		if (!(e->flag & MNT_CACHE_ISTAG))
			continue;
		if (strcmp(token, e->native) == 0 &&
		    strcmp(value, e->native + tksz + 1) == 0)
			return e->real;
	}
	return NULL;
}

/**
 * mnt_cache_read_tags
 * @cache: pointer to mnt_cache instance
 * @devname: path device
 *
 * Reads @devname LABEL and UUID to the @cache.
 *
 * Returns: 0 if at least one tag was added, 1 if no tag was added or
 *          -1 in case of error.
 */
int mnt_cache_read_tags(mnt_cache *cache, const char *devname)
{
	int i, ntags = 0;
	static blkid_probe pr;
	const char *tags[] = { "LABEL", "UUID" };

	assert(cache);
	assert(devname);

	if (!cache || !devname)
		return -1;

	DBG(DEBUG_CACHE, printf("cache: tags for %s requested\n", devname));

	/* check is device is already cached */
	for (i = 0; i < cache->nents; i++) {
		struct mnt_cache_entry *e = &cache->ents[i];
		if (!(e->flag & MNT_CACHE_TAGREAD))
			continue;
		if (strcmp(e->real, devname) == 0)
			/* tags has been already read */
			return 0;
	}

	DBG(DEBUG_CACHE,
		printf("cache: reading tags for: %s\n", devname));

	pr = blkid_new_probe_from_filename(devname);
	if (!pr)
		return -1;

	blkid_probe_enable_superblocks(pr, 1);

	blkid_probe_set_superblocks_flags(pr,
			BLKID_SUBLKS_LABEL | BLKID_SUBLKS_UUID);

	if (blkid_do_safeprobe(pr))
		goto error;

	for (i = 0; i < ARRAY_SIZE(tags); i++) {
		const char *data;
		char *dev;

		if (blkid_probe_lookup_value(pr, tags[i], &data, NULL))
			continue;
		if (mnt_cache_find_tag(cache, tags[i], data))
			continue; /* already cached */

		dev = strdup(devname);
		if (!dev)
			goto error;
		if (mnt_cache_add_tag(cache, tags[i], data, dev,
					MNT_CACHE_TAGREAD)) {
			free(dev);
			goto error;
		}
		ntags++;
	}

	return ntags ? 0 : 1;
error:
	blkid_free_probe(pr);
	return -1;
}

/**
 * mnt_cache_device_has_tag:
 * @cache: paths cache
 * @devname: path to the device
 * @token: tag name (e.g "LABEL")
 * @value: tag value
 *
 * Look up @cache to check it @tag+@value are associated with @devname.
 *
 * Returns: 1 on success or 0.
 */
int mnt_cache_device_has_tag(mnt_cache *cache, const char *devname,
				const char *token, const char *value)
{
	const char *path = mnt_cache_find_tag(cache, token, value);

	if (path && strcmp(path, devname) == 0)
		return 1;
	return 0;
}

/**
 * mnt_cache_find_tag_value:
 * @cache: cache for results
 * @devname: device name
 * @token: tag name ("LABEL" or "UUID")
 *
 * Returns: LABEL or UUID for the @devname or NULL in case of error.
 */
char *mnt_cache_find_tag_value(mnt_cache *cache,
		const char *devname, const char *token)
{
	int i;

	if (!cache || !devname || !token)
		return NULL;

	if (mnt_cache_read_tags(cache, devname) != 0)
		return NULL;

	for (i = 0; i < cache->nents; i++) {
		struct mnt_cache_entry *e = &cache->ents[i];
		if (!(e->flag & MNT_CACHE_ISTAG))
			continue;
		if (strcmp(e->real, devname) == 0 &&	/* dev name */
		    strcmp(token, e->native) == 0)	/* tag name */
			return e->native + strlen(token) + 1;	/* tag value */
	}

	return NULL;
}

/**
 * mnt_resolve_path:
 * @path: "native" path
 * @cache: cache for results or NULL
 *
 * Returns: absolute path or NULL in case of error. The result has to be
 * deallocated by free() if @cache is NULL.
 */
char *mnt_resolve_path(const char *path, mnt_cache *cache)
{
	char *p = NULL;
	char *native = NULL;
	char *real = NULL;

	assert(path);

	if (!path)
		return NULL;
	if (cache)
		p = (char *) mnt_cache_find_path(cache, path);

	if (!p) {
		p = canonicalize_path(path);

		if (p && cache) {
			native = strdup(path);
			real = strcmp(path, p) == 0 ? native : p;

			if (!native || !real)
				goto error;

			if (mnt_cache_add_entry(cache, native, real,
							MNT_CACHE_ISPATH))
				goto error;
		}
	}

	return p;
error:
	if (real != native)
		free(real);
	free(native);
	return NULL;
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
char *mnt_resolve_tag(const char *token, const char *value, mnt_cache *cache)
{
	char *p = NULL;

	assert(token);
	assert(value);

	if (!token || !value)
		return NULL;

	if (cache)
		p = (char *) mnt_cache_find_tag(cache, token, value);

	if (!p) {
		/* returns newly allocated string */
		p = blkid_evaluate_tag(token, value, cache ? &cache->bc : NULL);

		if (p && cache &&
		    mnt_cache_add_tag(cache, token, value, p, 0))
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
char *mnt_resolve_spec(const char *spec, mnt_cache *cache)
{
	char *cn = NULL;

	if (!spec)
		return NULL;

	if (strchr(spec, '=')) {
		char *tag, *val;

		if (!blkid_parse_tag_string(spec, &tag, &val)) {
			cn = mnt_resolve_tag(tag, val, cache);

			free(tag);
			free(val);
		}
	} else
		cn = mnt_resolve_path(spec, cache);

	return cn;
}


#ifdef TEST_PROGRAM

int test_resolve_path(struct mtest *ts, int argc, char *argv[])
{
	char line[BUFSIZ];
	mnt_cache *cache;

	cache = mnt_new_cache();
	if (!cache)
		return -1;

	while(fgets(line, sizeof(line), stdin)) {
		size_t sz = strlen(line);
		char *p;

		if (line[sz - 1] == '\n')
			line[sz - 1] = '\0';

		p = mnt_resolve_path(line, cache);
		printf("%s : %s\n", line, p);
	}
	mnt_free_cache(cache);
	return 0;
}

int test_resolve_spec(struct mtest *ts, int argc, char *argv[])
{
	char line[BUFSIZ];
	mnt_cache *cache;

	cache = mnt_new_cache();
	if (!cache)
		return -1;

	while(fgets(line, sizeof(line), stdin)) {
		size_t sz = strlen(line);
		char *p;

		if (line[sz - 1] == '\n')
			line[sz - 1] = '\0';

		p = mnt_resolve_spec(line, cache);
		printf("%s : %s\n", line, p);
	}
	mnt_free_cache(cache);
	return 0;
}

int test_read_tags(struct mtest *ts, int argc, char *argv[])
{
	char line[BUFSIZ];
	mnt_cache *cache;

	cache = mnt_new_cache();
	if (!cache)
		return -1;

	while(fgets(line, sizeof(line), stdin)) {
		size_t sz = strlen(line);

		if (line[sz - 1] == '\n')
			line[sz - 1] = '\0';

		if (*line == '/') {
			if (mnt_cache_read_tags(cache, line) < 0)
				fprintf(stderr, "%s: read tags faild\n", line);

		} else if (strchr(line, '=')) {
			char *tag, *val;
			const char *cn = NULL;

			if (!blkid_parse_tag_string(line, &tag, &val)) {
				cn = mnt_cache_find_tag(cache, tag, val);

				free(tag);
				free(val);
			}
			if (cn)
				printf("%s: %s\n", line, cn);
			else
				printf("%s: not cached\n", line);
		}
	}
	mnt_free_cache(cache);
	return 0;

}

int main(int argc, char *argv[])
{
	struct mtest ts[] = {
		{ "--resolve-path", test_resolve_path, "  resolve paths from stdin" },
		{ "--resolve-spec", test_resolve_spec, "  evaluate specs from stdin" },
		{ "--read-tags", test_read_tags,       "  read devname or TAG from stdin" },
		{ NULL }
	};

	return mnt_run_test(ts, argc, argv);
}
#endif
