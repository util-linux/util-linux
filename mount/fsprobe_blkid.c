#include <stdio.h>
#include <blkid/blkid.h>
#include "fsprobe.h"

#define BLKID_EMPTY_CACHE	"/dev/null"
static blkid_cache blkid;

void
fsprobe_init(void)
{
	blkid = NULL;
}

void
fsprobe_exit(void)
{
	if (blkid)
		blkid_put_cache(blkid);
}

const char *
fsprobe_get_label_by_devname(const char *devname)
{
	if (!blkid)
		blkid_get_cache(&blkid, NULL);

	return blkid_get_tag_value(blkid, "LABEL", devname);
}

const char *
fsprobe_get_uuid_by_devname(const char *devname)
{
	if (!blkid)
		blkid_get_cache(&blkid, NULL);

	return blkid_get_tag_value(blkid, "UUID", devname);
}

const char *
fsprobe_get_devname_by_uuid(const char *uuid)
{
	if (!blkid)
		blkid_get_cache(&blkid, NULL);

	return blkid_get_devname(blkid, "UUID", uuid);
}

const char *
fsprobe_get_devname_by_label(const char *label)
{
	if (!blkid)
		blkid_get_cache(&blkid, NULL);

	return blkid_get_devname(blkid, "LABEL", label);
}

int
fsprobe_known_fstype(const char *fstype)
{
	return blkid_known_fstype(fstype);
}

const char *
fsprobe_get_fstype_by_devname(const char *devname)
{
	blkid_cache c;
	const char *tp;

	if (blkid)
		return blkid_get_tag_value(blkid, "TYPE", devname);

	/* The cache is not initialized yet. Use empty cache rather than waste
	 * time with /etc/blkid.tab. It seems that probe FS is faster than
	 * parse the cache file.  -- kzak (17-May-2007)
	 */
	blkid_get_cache(&c, BLKID_EMPTY_CACHE);
	tp = blkid_get_tag_value(c, "TYPE", devname);
	blkid_put_cache(c);

	return tp;
}

