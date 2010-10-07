/*
 * Lifted from kpartx's mac.c
 *
 * Integrated to partx (utils-linux-ng)
 *       Davidlohr Bueso <dave@gnu.org>
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "bitops.h"
#include "partx.h"

#define MAC_PARTITION_MAGIC     0x504d
#define MAC_DRIVER_MAGIC        0x4552

struct mac_partition {
        uint16_t  signature;      /* expected to be MAC_PARTITION_MAGIC */
        uint16_t  res1;
        uint32_t  map_count;      /* # blocks in partition map */
        uint32_t  start_block;    /* absolute starting block # of partition */
        uint32_t  block_count;    /* number of blocks in partition */
        /* there is more stuff after this that we don't need */
};

/* Driver descriptor structure, in block 0 */
struct mac_driver_desc {
        uint16_t  signature;      /* expected to be MAC_DRIVER_MAGIC */
        uint16_t  block_size;
        uint32_t  block_count;
};

int
read_mac_pt(int fd, struct slice all, struct slice *sp, int ns) {
	struct mac_driver_desc *md;
        struct mac_partition *part;
	unsigned secsize;
	unsigned char *data;
	int blk, blocks_in_map;
        int n = 0;

	md = (struct mac_driver_desc *) getblock(fd, 0);
	if (md == NULL)
		return -1;

	if (be16_to_cpu(md->signature) != MAC_DRIVER_MAGIC)
		return -1;

	secsize = be16_to_cpu(md->block_size);
	data = getblock(fd, secsize/512);
	if (!data)
		return -1;
	part = (struct mac_partition *) (data + secsize%512);

	if (be16_to_cpu(part->signature) != MAC_PARTITION_MAGIC)
		return -1;

	blocks_in_map = be32_to_cpu(part->map_count);
	for (blk = 1; blk <= blocks_in_map && blk <= ns; ++blk, ++n) {
		int pos = blk * secsize;
		data = getblock(fd, pos/512);
		if (!data)
			return -1;

		part = (struct mac_partition *) (data + pos%512);
		if (be16_to_cpu(part->signature) != MAC_PARTITION_MAGIC)
			break;

		sp[n].start = be32_to_cpu(part->start_block) * (secsize/512);
		sp[n].size = be32_to_cpu(part->block_count) * (secsize/512);
	}
	return n;
}
