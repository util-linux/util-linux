#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>

#ifdef HAVE_BLKID_BLKID_H
#include <blkid/blkid.h>
#else
#include <blkid.h>
#endif

#include "blkdev.h"
#include "canonicalize.h"
#include "pathnames.h"
#include "fsprobe.h"

static blkid_cache blcache;

#ifdef HAVE_LIBBLKID_INTERNAL
/* ask kernel developers why we need such ugly open() method... */
static int
open_device(const char *devname)
{
	int retries = 0;

	do {
		int fd = open(devname, O_RDONLY);
		if (fd >= 0)
			return fd;
		if (errno != ENOMEDIUM)
			break;
		if (retries >= CRDOM_NOMEDIUM_RETRIES)
			break;
		++retries;
		sleep(3);
	} while(1);

	return -1;
}
#endif

/*
 * Parses NAME=value, returns -1 on parse error, 0 success. The success is also
 * when the 'spec' doesn't contain name=value pair (because the spec could be
 * a devname too). In particular case the pointer 'name' is set to NULL.
 */
int
fsprobe_parse_spec(const char *spec, char **name, char **value)
{
	*name = NULL;
	*value = NULL;

	if (strchr(spec, '='))
		return blkid_parse_tag_string(spec, name, value);

	return 0;
}

char *
fsprobe_get_devname_by_spec(const char *spec)
{
	char *name, *value;

	if (!spec)
		return NULL;
	if (fsprobe_parse_spec(spec, &name, &value) != 0)
		return NULL;				/* parse error */
	if (name) {
		char *nspec = NULL;

		if (!strcmp(name,"LABEL"))
			nspec = fsprobe_get_devname_by_label(value);
		else if (!strcmp(name,"UUID"))
			nspec = fsprobe_get_devname_by_uuid(value);

		free(name);
		free(value);
		return nspec;
	}

	return canonicalize_path(spec);
}

void
fsprobe_init(void)
{
	blcache = NULL;
}

int
fsprobe_known_fstype(const char *fstype)
{
	return blkid_known_fstype(fstype);
}

#ifdef HAVE_LIBBLKID_INTERNAL
/*
 * libblkid from util-linux-ng
 * -- recommended
 */
static blkid_probe blprobe;

void
fsprobe_exit(void)
{
	if (blprobe)
		blkid_free_probe(blprobe);
	if (blcache)
		blkid_put_cache(blcache);
}

/* returns device LABEL, UUID, FSTYPE, ... by low-level
 * probing interface
 */
static char *
fsprobe_get_value(const char *name, const char *devname)
{
	int fd;
	const char *data = NULL;

	if (!devname || !name)
		return NULL;
	fd = open_device(devname);
	if (fd < 0)
		return NULL;
	if (!blprobe)
		blprobe = blkid_new_probe();
	if (!blprobe)
		goto done;
	if (blkid_probe_set_device(blprobe, fd, 0, 0))
		goto done;

	blkid_probe_enable_superblocks(blprobe, 1);

	blkid_probe_set_superblocks_flags(blprobe,
		BLKID_SUBLKS_LABEL | BLKID_SUBLKS_UUID | BLKID_SUBLKS_TYPE);

	if (blkid_do_safeprobe(blprobe))
		goto done;
	if (blkid_probe_lookup_value(blprobe, name, &data, NULL))
		goto done;
done:
	close(fd);
	return data ? strdup((char *) data) : NULL;
}

char *
fsprobe_get_label_by_devname(const char *devname)
{
	return fsprobe_get_value("LABEL", devname);
}

char *
fsprobe_get_uuid_by_devname(const char *devname)
{
	return fsprobe_get_value("UUID", devname);
}

char *
fsprobe_get_fstype_by_devname(const char *devname)
{
	return fsprobe_get_value("TYPE", devname);
}

char *
fsprobe_get_devname_by_uuid(const char *uuid)
{
	return blkid_evaluate_tag("UUID", uuid, &blcache);
}

char *
fsprobe_get_devname_by_label(const char *label)
{
	return blkid_evaluate_tag("LABEL", label, &blcache);
}

#else /* !HAVE_LIBBLKID_INTERNAL */

/*
 * Classic libblkid (from e2fsprogs) without blkid_evaluate_tag()
 * -- deprecated
 */
#define BLKID_EMPTY_CACHE	"/dev/null"

void
fsprobe_exit(void)
{
	if (blcache)
		blkid_put_cache(blcache);
}

char *
fsprobe_get_devname_by_uuid(const char *uuid)
{
	if (!blcache)
		blkid_get_cache(&blcache, NULL);

	return blkid_get_devname(blcache, "UUID", uuid);
}

char *
fsprobe_get_devname_by_label(const char *label)
{
	if (!blcache)
		blkid_get_cache(&blcache, NULL);

	return blkid_get_devname(blcache, "LABEL", label);
}

char *
fsprobe_get_fstype_by_devname(const char *devname)
{
	blkid_cache c;
	char *tp;

	if (blcache)
		return blkid_get_tag_value(blcache, "TYPE", devname);

	/* The cache is not initialized yet. Use empty cache rather than waste
	 * time with /etc/blkid.tab. It seems that probe FS is faster than
	 * parse the cache file.  -- kzak (17-May-2007)
	 */
	blkid_get_cache(&c, BLKID_EMPTY_CACHE);
	tp = blkid_get_tag_value(c, "TYPE", devname);
	blkid_put_cache(c);

	return tp;
}

char *
fsprobe_get_label_by_devname(const char *devname)
{
	if (!blcache)
		blkid_get_cache(&blcache, NULL);

	return blkid_get_tag_value(blcache, "LABEL", devname);
}

char *
fsprobe_get_uuid_by_devname(const char *devname)
{
	if (!blcache)
		blkid_get_cache(&blcache, NULL);

	return blkid_get_tag_value(blcache, "UUID", devname);
}

#endif /* !HAVE_LIBBLKID_INTERNAL */
