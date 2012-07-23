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
 * label probing functions
 */
static const struct fdisk_label *labels[] =
{
	&bsd_label,
	&dos_label,
	&sgi_label,
	&sun_label,
	&aix_label,
	&mac_label,
};

static int __probe_labels(struct fdisk_context *cxt)
{
	int i, rc = 0;

	disklabel = ANY_LABEL;
	update_units(cxt);

	for (i = 0; i < ARRAY_SIZE(labels); i++) {
		rc = labels[i]->probe(cxt);
		if (rc) {
			DBG(LABEL, dbgprint("detected a %s label\n",
					    labels[i]->name));
			goto done;
		}
	}

done:
	return rc;
}

static int __init_mbr_buffer(struct fdisk_context *cxt)
{
	DBG(TOPOLOGY, dbgprint("initialize MBR buffer"));

	cxt->mbr = calloc(1, MAX_SECTOR_SIZE);
	if (!cxt->mbr)
		goto fail;

	/* read MBR */
	if (512 != read(cxt->dev_fd, cxt->mbr, 512)) {
		if (errno == 0)
			errno = EINVAL;	/* probably too small file/device */
		goto fail;
	}

	return 0;
fail:
	return -1;
}

static unsigned long __get_sector_size(int fd)
{
	int sect_sz;

	if (!blkdev_get_sector_size(fd, &sect_sz))
		return (unsigned long) sect_sz;
	return DEFAULT_SECTOR_SIZE;
}

/**
 * fdisk_context_force_sector_size:
 * @cxt: fdisk context
 * @s: required sector size
 *
 * Overwrites logical and physical sector size. Note that the default sector
 * size is discovered by fdisk_new_context_from_device() from device topology.
 *
 * Don't use this function, rely on the default behavioer is more safe.
 *
 * Returns: 0 on success, < 0 on error.
 */
int fdisk_context_force_sector_size(struct fdisk_context *cxt, sector_t s)
{
	if (!cxt)
		return -EINVAL;

	cxt->phy_sector_size = cxt->sector_size = s;
	return 0;
}

static void recount_geometry(struct fdisk_context *cxt)
{
	cxt->geom.cylinders = cxt->total_sectors /
		(cxt->geom.heads * cxt->geom.sectors);
}

/**
 * fdisk_context_set_user_geometry:
 * @cxt: fdisk context
 * @cylinders: user specified cylinders
 * @heads: user specified heads
 * @sectors: user specified sectors
 *
 * Overrides autodiscovery and apply user specified geometry.
 *
 * Returns: 0 on success, < 0 on error.
 */
int fdisk_context_set_user_geometry(struct fdisk_context *cxt,
				    unsigned int cylinders,
				    unsigned int heads,
				    unsigned int sectors)
{
	if (!cxt)
		return -EINVAL;
	if (heads)
		cxt->geom.heads = heads;
	if (sectors)
		cxt->geom.sectors = sectors;

	recount_geometry(cxt);

	if (!cxt->geom.cylinders)
		/* use the user defined cylinders only as fillback */
		cxt->geom.cylinders = cylinders;

	return 0;
}


static int __discover_geometry(struct fdisk_context *cxt)
{
	sector_t nsects;
	unsigned int h = 0, s = 0;

	/* get number of 512-byte sectors, and convert it the real sectors */
	if (!blkdev_get_sectors(cxt->dev_fd, &nsects))
		cxt->total_sectors = (nsects / (cxt->sector_size >> 9));

	get_partition_table_geometry(cxt, &h, &s);
	if (h && s)
		goto hs_ok;

	/* what the kernel/bios thinks the geometry is */
	blkdev_get_geometry(cxt->dev_fd, &h, &s);
	if (h && s)
		goto hs_ok;

	/* unable to discover geometry, use default values */
	s = 63;
	h = 255;

hs_ok: /* obtained heads and sectors */
	cxt->geom.heads = h;
	cxt->geom.sectors = s;
	recount_geometry(cxt);

	update_sector_offset(cxt);

	DBG(GEOMETRY, dbgprint("geometry discovered for %s: C/H/S: %lld/%d/%lld",
			       cxt->dev_path, cxt->geom.cylinders,
			       cxt->geom.heads, cxt->geom.sectors));
	return 0;
}

static int __discover_topology(struct fdisk_context *cxt)
{
#ifdef HAVE_LIBBLKID
	blkid_probe pr;

	DBG(TOPOLOGY, dbgprint("initialize libblkid prober"));

	pr = blkid_new_probe();
	if (pr && blkid_probe_set_device(pr, cxt->dev_fd, 0, 0) == 0) {
		blkid_topology tp = blkid_probe_get_topology(pr);

		if (tp) {
			cxt->min_io_size = blkid_topology_get_minimum_io_size(tp);
			cxt->optimal_io_size = blkid_topology_get_optimal_io_size(tp);
			cxt->phy_sector_size = blkid_topology_get_physical_sector_size(tp);
			cxt->alignment_offset = blkid_topology_get_alignment_offset(tp);

			/* I/O size used by fdisk */
			cxt->io_size = cxt->optimal_io_size;
			if (!cxt->io_size)
				/* optimal IO is optional, default to minimum IO */
				cxt->io_size = cxt->min_io_size;
		}
	}
	blkid_free_probe(pr);
#endif

	/* no blkid or error, use default values */
	if (!cxt->min_io_size)
		cxt->min_io_size = DEFAULT_SECTOR_SIZE;
	if (!cxt->io_size)
		cxt->io_size = DEFAULT_SECTOR_SIZE;

	cxt->sector_size = __get_sector_size(cxt->dev_fd);
	if (!cxt->phy_sector_size) /* could not discover physical size */
		cxt->phy_sector_size = cxt->sector_size;

	DBG(TOPOLOGY, dbgprint("topology discovered for %s:\n"
			       "\tlogical/physical sector sizes: %ld/%ld\n"
			       "\tfdisk/minimal/optimal io sizes: %ld/%ld/%ld\n",
			       cxt->dev_path, cxt->sector_size, cxt->phy_sector_size,
			       cxt->io_size, cxt->optimal_io_size, cxt->min_io_size));
	return 0;
}

/**
 * fdisk_mbr_zeroize:
 * @cxt: fdisk context
 *
 * Zero's MBR buffer
 */
void fdisk_mbr_zeroize(struct fdisk_context *cxt)
{
	if (cxt->mbr) {
		DBG(CONTEXT, dbgprint("zeroize in-memory MBR"));
		memset(cxt->mbr, 0, MAX_SECTOR_SIZE);
	}
}

/**
 * fdisk_dev_sectsz_is_default:
 * @cxt: fdisk context
 *
 * Returns 1 if the device's sector size is the default value, otherwise 0.
 */
int fdisk_dev_sectsz_is_default(struct fdisk_context *cxt)
{
	return cxt->sector_size == DEFAULT_SECTOR_SIZE;
}

/**
 * fdisk_dev_has_topology:
 * @cxt: fdisk context
 *
 * Returns 1 if the device provides topology information, otherwise 0.
 */
int fdisk_dev_has_topology(struct fdisk_context *cxt)
{
	/*
	 * Assume that the device provides topology info if
	 * optimal_io_size is set or alignment_offset is set or
	 * minimum_io_size is not power of 2.
	 */
	if (cxt->optimal_io_size || cxt->alignment_offset ||
	    !is_power_of_2(cxt->min_io_size))
		return 1;
	return 0;
}

/**
 * fdisk_dev_has_disklabel:
 * @cxt: fdisk context
 *
 * Returns: return 1 if there is label on the device.
 */
int fdisk_dev_has_disklabel(struct fdisk_context *cxt)
{
	return cxt && disklabel != ANY_LABEL;
}

/**
 * fdisk_create_default_disklabel:
 * @cxt: fdisk context
 *
 * Creates (in memory) disk label which is usual default for the system. For
 * example sun label on sparcs, gpt on UEFI machines (TODO), DOS on another
 * machines, ...etc.
 *
 * Returns: 0 on sucess, < 0 on error.
 */
int fdisk_create_default_disklabel(struct fdisk_context *cxt)
{
	if (!cxt)
		return -EINVAL;
#ifdef __sparc__
	return create_sunlabel(cxt);
#else
	return create_doslabel(cxt);
#endif
}

/**
 * fdisk_init_debug:
 * @mask: debug mask (0xffff to enable full debuging)
 *
 * If the @mask is not specified then this function reads
 * FDISK_DEBUG environment variable to get the mask.
 *
 * Already initialized debugging stuff cannot be changed. It does not
 * have effect to call this function twice.
 */
void fdisk_init_debug(int mask)
{
	if (fdisk_debug_mask & FDISK_DEBUG_INIT)
		return;
	if (!mask) {
		char *str = getenv("FDISK_DEBUG");
		if (str)
			fdisk_debug_mask = strtoul(str, 0, 0);
	} else
		fdisk_debug_mask = mask;

	if (fdisk_debug_mask)
		fprintf(stderr, "fdisk: debug mask set to 0x%04x.\n",
		       fdisk_debug_mask);
	fdisk_debug_mask |= FDISK_DEBUG_INIT;
}

/**
 * fdisk_new_context:
 * @filename: path to the device to be handled
 * @readonly: how to open the device
 *
 * If the @readonly flag is set to false, fdisk will attempt to open
 * the device with read-write mode and will fallback to read-only if
 * unsuccessful.
 *
 * Returns: newly allocated fdisk context
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

	if (__init_mbr_buffer(cxt) < 0)
		goto fail;

	__discover_topology(cxt);
	__discover_geometry(cxt);

	__probe_labels(cxt);

	DBG(CONTEXT, dbgprint("context initialized for %s [%s]",
			      fname, readonly ? "READ-ONLY" : "READ-WRITE"));
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

	DBG(CONTEXT, dbgprint("freeing context for %s", cxt->dev_path));
	close(cxt->dev_fd);
	free(cxt->dev_path);
	free(cxt->mbr);
	free(cxt);
}
