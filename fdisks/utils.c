/*
 *  Copyright (C) 2012 Davidlohr Bueso <dave@gnu.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#ifdef HAVE_LIBBLKID
#include <blkid.h>
#endif

#include "nls.h"
#include "blkdev.h"
#include "common.h"
#include "fdisk.h"

#include "fdiskdoslabel.h"
#include "fdisksunlabel.h"

int fdisk_debug_mask;

/*
 * Label probing functions.
 */
static const struct fdisk_label *labels[] =
{
	&gpt_label,
	&dos_label,
	&sun_label,
	&sgi_label,
	&aix_label,
	&bsd_label,
	&mac_label,
};


static int __probe_labels(struct fdisk_context *cxt)
{
	size_t i;

	cxt->disklabel = FDISK_DISKLABEL_ANY;

	for (i = 0; i < ARRAY_SIZE(labels); i++) {
		if (!labels[i]->probe || labels[i]->probe(cxt) != 1)
			continue;

		cxt->label = labels[i];

		DBG(LABEL, dbgprint("detected a %s label", cxt->label->name));
		return 0;
	}

	return 1; /* not found */
}


/**
 * fdisk_reset_alignment:
 * @cxt: fdisk context
 *
 * Resets alignment setting to the default or label specific values.
 *
 * Returns: 0 on success, < 0 in case of error.
 */
int fdisk_reset_alignment(struct fdisk_context *cxt)
{
	int rc = 0;

	if (!cxt)
		return -EINVAL;

	/* default */
	cxt->grain = fdisk_topology_get_grain(cxt);
	cxt->first_lba = fdisk_topology_get_first_lba(cxt);

	/* overwrite default by label stuff */
	if (cxt->label && cxt->label->reset_alignment)
		rc = cxt->label->reset_alignment(cxt);

	DBG(LABEL, dbgprint("%s alignment reseted to: "
			    "first LBA=%ju, grain=%lu [rc=%d]",
			    cxt->label ? cxt->label->name : NULL,
			    (uintmax_t) cxt->first_lba,
			    cxt->grain,	rc));
	return rc;
}


/**
 * fdisk_create_disklabel:
 * @cxt: fdisk context
 * @name: label name
 *
 * Creates a new disk label of type @name. If @name is NULL, then it
 * will create a default system label type, either SUN or DOS.
 *
 * Returns 0 on success, otherwise, a corresponding error.
 */
int fdisk_create_disklabel(struct fdisk_context *cxt, const char *name)
{
	if (!cxt)
		return -EINVAL;

	cxt->label = NULL;

	if (!name) { /* use default label creation */
#ifdef __sparc__
		cxt->label = &sun_label;
#else
		cxt->label = &dos_label;
#endif
	} else {
		size_t i;

		for (i = 0; i < ARRAY_SIZE(labels); i++) {
			if (strcmp(name, labels[i]->name) != 0)
				continue;

			cxt->label = labels[i];
			DBG(LABEL, dbgprint("changing to %s label\n", cxt->label->name));
			break;
		}
	}

	if (!cxt->label)
		return -EINVAL;
	if (!cxt->label->create)
		return -ENOSYS;

	fdisk_reset_alignment(cxt);

	return cxt->label->create(cxt);
}

/**
 * fdisk_new_context:
 * @fname: path to the device to be handled
 * @readonly: how to open the device
 *
 * If the @readonly flag is set to false, fdisk will attempt to open
 * the device with read-write mode and will fallback to read-only if
 * unsuccessful.
 *
 * Returns: newly allocated fdisk context or NULL upon failure.
 */
struct fdisk_context *fdisk_new_context_from_filename(const char *fname, int readonly)
{
	int fd, errsv = 0;
	struct fdisk_context *cxt = NULL;

	DBG(CONTEXT, dbgprint("initializing context for %s", fname));

	if (readonly == 1 || (fd = open(fname, O_RDWR)) < 0) {
		if ((fd = open(fname, O_RDONLY)) < 0)
			return NULL;
		readonly = 1;
	}

	cxt = calloc(1, sizeof(*cxt));
	if (!cxt)
		goto fail;

	cxt->dev_fd = fd;
	cxt->dev_path = strdup(fname);
	if (!cxt->dev_path)
		goto fail;

	fdisk_discover_topology(cxt);
	fdisk_discover_geometry(cxt);

	if (fdisk_read_firstsector(cxt) < 0)
		goto fail;

	/* detect labels and apply labes specific stuff (e.g geomery)
	 * to the context */
	__probe_labels(cxt);

	fdisk_reset_alignment(cxt);

	DBG(CONTEXT, dbgprint("context %p initialized for %s [%s]",
			      cxt, fname,
			      readonly ? "READ-ONLY" : "READ-WRITE"));
	return cxt;
fail:
	errsv = errno;
	fdisk_free_context(cxt);
	errno = errsv;

	DBG(CONTEXT, dbgprint("failed to initialize context for %s: %m", fname));
	return NULL;
}

/**
 * fdisk_free_context:
 * @cxt: fdisk context
 *
 * Deallocates context struct.
 */
void fdisk_free_context(struct fdisk_context *cxt)
{
	if (!cxt)
		return;

	DBG(CONTEXT, dbgprint("freeing context %p for %s", cxt, cxt->dev_path));
	close(cxt->dev_fd);
	free(cxt->dev_path);
	free(cxt->firstsector);
	free(cxt);
}



