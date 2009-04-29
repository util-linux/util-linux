#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>

#include <blkid.h>

#include "blkdev.h"
#include "canonicalize.h"
#include "pathnames.h"
#include "fsprobe.h"

#if defined(HAVE_BLKID_EVALUATE_TAG) || defined(HAVE_LIBVOLUME_ID)
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

 * The result is a new allocated string (the 'name' pointer).
 */
int
fsprobe_parse_spec(const char *spec, char **name, char **value)
{
	char *vl, *tk, *cp;

	*name = NULL;
	*value = NULL;

	if (!(cp = strchr(spec, '=')))
		return 0;				/* no name= */

	tk = strdup(spec);
	vl = tk + (cp - spec);
	*vl++ = '\0';

	if (*vl == '"' || *vl == '\'') {
		if (!(cp = strrchr(vl+1, *vl))) {
			free(tk);
			return -1;			/* parse error */
		}
		vl++;
		*cp = '\0';
	}

	*name = tk;
	*value = vl;
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

		free((void *) name);
		return nspec;
	}

	return canonicalize_path(spec);
}

#ifdef HAVE_LIBBLKID
static blkid_cache blcache;

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

#ifdef HAVE_BLKID_EVALUATE_TAG
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
	if (blkid_probe_set_request(blprobe, BLKID_PROBREQ_LABEL |
			 BLKID_PROBREQ_UUID | BLKID_PROBREQ_TYPE ))
		goto done;
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

#else /* !HAVE_BLKID_EVALUATE_TAG */

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

#endif /* !HAVE_BLKID_EVALUATE_TAG */
#else  /* !HAVE_LIBBLKID */

/*
 * libvolume_id from udev
 * -- deprecated
 */
#include <libvolume_id.h>

enum probe_type {
	VOLUME_ID_NONE,
	VOLUME_ID_LABEL,
	VOLUME_ID_UUID,
	VOLUME_ID_TYPE,
};

static char
*probe(const char *device, enum probe_type type)
{
	int fd;
	uint64_t size;
	struct volume_id *id;
	const char *val;
	char *value = NULL;
	int retries = 0;

	fd = open_device(devname);
	if (fd < 0)
		return NULL;
	id = volume_id_open_fd(fd);
	if (!id) {
		close(fd);
		return NULL;
	}
	if (blkdev_get_size(fd, &size) != 0)
		size = 0;
	if (volume_id_probe_all(id, 0, size) == 0) {
		switch(type) {
		case VOLUME_ID_LABEL:
			if (volume_id_get_label(id, &val))
				value  = strdup(val);
			break;
		case VOLUME_ID_UUID:
			if (volume_id_get_uuid(id, &val))
				value  = strdup(val);
			break;
		case VOLUME_ID_TYPE:
			if (volume_id_get_type(id, &val))
				value  = strdup(val);
			break;
		default:
			break;
		}
	}
	volume_id_close(id);
	close(fd);
	return value;
}

void
fsprobe_init(void)
{
}

void
fsprobe_exit(void)
{
}

int
fsprobe_known_fstype(const char *fstype)
{
	if (volume_id_get_prober_by_type(fstype) != NULL)
		return 1;
	return 0;
}

char *
fsprobe_get_uuid_by_devname(const char *devname)
{
	return probe(devname, VOLUME_ID_UUID);
}

char *
fsprobe_get_label_by_devname(const char *devname)
{
	return probe(devname, VOLUME_ID_LABEL);
}

char *
fsprobe_get_fstype_by_devname(const char *devname)
{
	return probe(devname, VOLUME_ID_TYPE);
}

char *
fsprobe_get_devname_by_uuid(const char *uuid)
{
	char dev[PATH_MAX];
	size_t len;

	if (!uuid)
		return NULL;

	strcpy(dev, _PATH_DEV_BYUUID "/");
	len = strlen(_PATH_DEV_BYUUID "/");
	if (!volume_id_encode_string(uuid, &dev[len], sizeof(dev) - len))
		return NULL;
	return canonicalize_path(dev);
}

char *
fsprobe_get_devname_by_label(const char *label)
{
	char dev[PATH_MAX];
	size_t len;

	if (!label)
		return NULL;
	strcpy(dev, _PATH_DEV_BYLABEL "/");
	len = strlen(_PATH_DEV_BYLABEL "/");
	if (!volume_id_encode_string(label, &dev[len], sizeof(dev) - len))
		return NULL;
	return canonicalize_path(dev);
}

#endif /* HAVE_LIBVOLUME_ID  */
