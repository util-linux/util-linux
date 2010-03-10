/*
 * ioctl based topology -- gathers topology information
 *
 * Copyright (C) 2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "blkdev.h"		/* top-level lib/ */
#include "topology.h"

/*
 * ioctl topology values
 */
static struct topology_val {

	long  ioc;

	/* function to set probing resut */
	int (*set_result)(blkid_probe, unsigned long);

} topology_vals[] = {
	{ BLKALIGNOFF, blkid_topology_set_alignment_offset },
	{ BLKIOMIN, blkid_topology_set_minimum_io_size },
	{ BLKIOOPT, blkid_topology_set_optimal_io_size },
	{ BLKPBSZGET, blkid_topology_set_physical_sector_size }
	/* we read BLKSSZGET in topology.c */
};

static int probe_ioctl_tp(blkid_probe pr, const struct blkid_idmag *mag)
{
	int i;
	int count = 0;

	for (i = 0; i < ARRAY_SIZE(topology_vals); i++) {
		struct topology_val *val = &topology_vals[i];
		unsigned int data = 0;
		int rc;

		if (val->ioc == BLKALIGNOFF) {
			int sdata = 0;
			if (ioctl(pr->fd, val->ioc, &sdata) == -1)
				goto nothing;
			data = sdata < 0 ? 0 : sdata;

		} else if (ioctl(pr->fd, val->ioc, &data) == -1)
			goto nothing;

		rc = val->set_result(pr, (unsigned long) data);
		if (rc)
			goto err;
		count++;
	}

	if (count)
		return 0;
nothing:
	return 1;
err:
	return -1;
}

const struct blkid_idinfo ioctl_tp_idinfo =
{
	.name		= "ioctl",
	.probefunc	= probe_ioctl_tp,
	.magics		= BLKID_NONE_MAGIC
};

