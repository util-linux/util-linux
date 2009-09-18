#ifndef BLKID_TOPOLOGY_H
#define BLKID_TOPOLOGY_H

#include "blkidP.h"

/*
 * Binary interface
 */
struct blkid_struct_topology {
	unsigned long	alignment_offset;
	unsigned long	minimum_io_size;
	unsigned long	optimal_io_size;
};

/*
 * topology probers
 */
extern const struct blkid_idinfo sysfs_tp_idinfo;

#endif /* BLKID_TOPOLOGY_H */

