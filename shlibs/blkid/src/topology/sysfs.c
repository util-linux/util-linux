/*
 * sysfs based topology -- gathers topology information from Linux sysfs
 *
 * Copyright (C) 2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 *
 * For more information see Linux kernel Documentation/ABI/testing/sysfs-block.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "topology.h"

/*
 * Sysfs topology values (since 2.6.31, May 2009).
 */
static struct topology_val {

	/* /sys/dev/block/<maj>:<min>/<ATTR> */
	const char *attr;

	/* functions to set probing resut */
	int (*set_ulong)(blkid_probe, unsigned long);
	int (*set_int)(blkid_probe, int);

} topology_vals[] = {
	{ "alignment_offset", NULL, blkid_topology_set_alignment_offset },
	{ "queue/minimum_io_size", blkid_topology_set_minimum_io_size },
	{ "queue/optimal_io_size", blkid_topology_set_optimal_io_size },
	{ "queue/physical_block_size", blkid_topology_set_physical_sector_size },
};

static int probe_sysfs_tp(blkid_probe pr, const struct blkid_idmag *mag)
{
	dev_t dev, pri_dev = 0;
	int i, count = 0;

	dev = blkid_probe_get_devno(pr);
	if (!dev)
		goto nothing;		/* probably not a block device */

	for (i = 0; i < ARRAY_SIZE(topology_vals); i++) {
		struct topology_val *val = &topology_vals[i];
		dev_t attr_dev = dev;
		int rc = 1;

		if (!blkid_devno_has_attribute(dev, val->attr)) {
			/* get attribute from partition's primary device */
			if (!pri_dev &&
			    blkid_devno_to_wholedisk(dev, NULL, 0, &pri_dev))
				continue;
			attr_dev = pri_dev;
		}

		if (val->set_ulong) {
			uint64_t data = 0;

			if (blkid_devno_get_u64_attribute(attr_dev,
							val->attr, &data))
				continue;
			rc = val->set_ulong(pr, (unsigned long) data);

		} else if (val->set_int) {
			int64_t data = 0;

			if (blkid_devno_get_s64_attribute(attr_dev,
							val->attr, &data))
				continue;
			rc = val->set_int(pr, (int) data);
		}

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

const struct blkid_idinfo sysfs_tp_idinfo =
{
	.name		= "sysfs",
	.probefunc	= probe_sysfs_tp,
	.magics		= BLKID_NONE_MAGIC
};

