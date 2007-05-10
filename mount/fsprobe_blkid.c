#include <stdio.h>
#include <blkid/blkid.h>
#include "fsprobe.h"

static blkid_cache blkid;

void
fsprobe_init(void) {
	blkid_get_cache(&blkid, NULL);
}

void
fsprobe_exit(void) {
	blkid_put_cache(blkid);
}

const char *
fsprobe_get_label_by_devname(const char *devname) {
	return blkid_get_tag_value(blkid, "LABEL", devname);
}

const char *
fsprobe_get_uuid_by_devname(const char *devname) {
	return blkid_get_tag_value(blkid, "UUID", devname);
}

const char *
fsprobe_get_devname(const char *spec) {
	return blkid_get_devname(blkid, spec, 0);
}

const char *
fsprobe_get_devname_by_uuid(const char *uuid) {
	return blkid_get_devname(blkid, "UUID", uuid);
}

const char *
fsprobe_get_devname_by_label(const char *label) {
	return blkid_get_devname(blkid, "LABEL", label);
}

/* Also when no UUID= or LABEL= occur? No verbose? No warnings? */
const char *
fsprobe_get_devname_for_mounting(const char *spec) {
	return blkid_get_devname(blkid, spec, 0);
}

int
fsprobe_known_fstype(const char *fstype)
{
	return blkid_known_fstype(fstype);
}

const char *
fsprobe_get_fstype_by_devname(const char *devname) {
	return blkid_get_tag_value(blkid, "TYPE", devname);
}

