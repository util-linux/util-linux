/*
 * Evms topology
 * -- this is fallback for old systems where the toplogy information are not
 *    exporte dy sysfs
 *
 * Copyright (C) 2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 *
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "topology.h"

#define EVMS_MAJOR		117
#define EVMS_GET_STRIPE_INFO	_IOR(EVMS_MAJOR, 0xF0, struct evms_stripe_info)

struct evms_stripe_info {
	u_int32_t	size;		/* stripe unit 512-byte blocks */
	u_int32_t	width;		/* the number of stripe members or RAID data disks */
} evms_stripe_info;

static int is_evms_device(dev_t devno)
{
	if (major(devno) == EVMS_MAJOR)
		return 1;
	return blkid_driver_has_major("evms", major(devno));
}

static int probe_evms_tp(blkid_probe pr, const struct blkid_idmag *mag)
{
	struct evms_stripe_info evms;
	dev_t devno = blkid_probe_get_devno(pr);

	if (!devno)
		goto nothing;		/* probably not a block device */

	if (!is_evms_device(devno))
		goto nothing;

	memset(&evms, 0, sizeof(evms));

	if (ioctl(pr->fd, EVMS_GET_STRIPE_INFO, &evms))
		goto nothing;

	blkid_topology_set_minimum_io_size(pr, evms.size << 9);
	blkid_topology_set_optimal_io_size(pr, (evms.size * evms.width) << 9);

	return 0;

nothing:
	return 1;
}

const struct blkid_idinfo evms_tp_idinfo =
{
	.name		= "evms",
	.probefunc	= probe_evms_tp,
	.magics		= BLKID_NONE_MAGIC
};

