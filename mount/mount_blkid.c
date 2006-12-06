#include <stdio.h>
#include "mount_blkid.h"

#ifdef HAVE_BLKID

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

#else
#include <string.h>
#include "sundries.h"
#include "mount_by_label.h"
#include "nls.h"

void
mount_blkid_get_cache(void) {
}

void
mount_blkid_put_cache(void) {
}

const char *
mount_get_volume_label_by_spec(const char *spec) {
	return xstrdup(get_volume_label_by_spec(spec));
}

const char *
mount_get_devname(const char *spec) {
	if (!strncmp(spec, "UUID=", 5))
		return get_spec_by_uuid(spec+5);
	if (!strncmp(spec, "LABEL=", 6))
		return get_spec_by_volume_label(spec+6);
	return spec;
}

const char *
mount_get_devname_by_uuid(const char *uuid) {
	return get_spec_by_uuid(uuid);
}

extern char *progname;

const char *
mount_get_devname_by_label(const char *volumelabel) {
	const char *spec, *spec2;

	spec = get_spec_by_volume_label(volumelabel);
	spec2 = second_occurrence_of_vol_label(volumelabel);
	if (spec2)
		die (EX_FAIL,
		     _("%s: error: the label %s occurs on both %s and %s\n"),
		     progname, volumelabel, spec, spec2);
	return spec;
}

const char *
mount_get_devname_for_mounting(const char *spec) {
	const char *nspec;

	if (!strncmp(spec, "UUID=", 5)) {
		nspec = mount_get_devname_by_uuid(spec+5);
		if (nspec && verbose > 1)
			printf(_("mount: going to mount %s by UUID\n"), spec);
	} else if (!strncmp(spec, "LABEL=", 6)) {
		nspec = mount_get_devname_by_label(spec+6);
		if (nspec && verbose > 1)
			printf(_("mount: going to mount %s by label\n"), spec);
	} else
		nspec = spec;

	return nspec;
}


#endif
