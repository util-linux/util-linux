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

int fdisk_debug_mask;

static unsigned long __get_sector_size(int fd)
{
	int sect_sz;

	if (!blkdev_get_sector_size(fd, &sect_sz))
		return (unsigned long) sect_sz;
	return DEFAULT_SECTOR_SIZE;
}

static int __discover_geometry(struct fdisk_context *cxt)
{
	sector_t nsects;

	/* get number of 512-byte sectors, and convert it the real sectors */
	if (!blkdev_get_sectors(cxt->dev_fd, &nsects))
		cxt->total_sectors = (nsects / (cxt->sector_size >> 9));
	return 0;
}

static int __discover_topology(struct fdisk_context *cxt)
{
#ifdef HAVE_LIBBLKID
	blkid_probe pr;

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

	return 0;
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

	__discover_topology(cxt);
	__discover_geometry(cxt);

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
	free(cxt);
}
