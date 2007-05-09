#include <stdio.h>
#include "fsprobe.h"

#ifdef HAVE_LIBBLKID

blkid_cache blkid;

void
mount_blkid_get_cache(void) {
	blkid_get_cache(&blkid, NULL);
}

void
mount_blkid_put_cache(void) {
	blkid_put_cache(blkid);
}

const char *
mount_get_volume_label_by_spec(const char *spec) {
	return blkid_get_tag_value(blkid, "LABEL", spec);
}

const char *
mount_get_devname(const char *spec) {
	return blkid_get_devname(blkid, spec, 0);
}

const char *
mount_get_devname_by_uuid(const char *uuid) {
	return blkid_get_devname(blkid, "UUID", uuid);
}

const char *
mount_get_devname_by_label(const char *label) {
	return blkid_get_devname(blkid, "LABEL", label);
}

/* Also when no UUID= or LABEL= occur? No verbose? No warnings? */
const char *
mount_get_devname_for_mounting(const char *spec) {
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

#endif
