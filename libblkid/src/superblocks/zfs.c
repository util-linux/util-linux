/*
 * Copyright (C) 2009-2010 by Andreas Dilger <adilger@sun.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>

#include "superblocks.h"

#define VDEV_LABEL_NVPAIR	( 16 * 1024ULL)
#define VDEV_LABEL_SIZE		(256 * 1024ULL)
#define	VDEV_PHYS_SIZE		(112 * 1024ULL)
#define	VDEV_LABELS		4
#define ZFS_MINDEVSIZE		(64ULL << 20)
#define DATA_TYPE_UNKNOWN	0
#define DATA_TYPE_UINT64	8
#define DATA_TYPE_STRING 	9
#define DATA_TYPE_DIRECTORY 	19

typedef enum pool_state {
	POOL_STATE_ACTIVE = 0,		/* In active use		*/
	POOL_STATE_EXPORTED,		/* Explicitly exported		*/
	POOL_STATE_DESTROYED,		/* Explicitly destroyed		*/
	POOL_STATE_SPARE,		/* Reserved for hot spare use	*/
	POOL_STATE_L2CACHE,		/* Level 2 ARC device		*/
	POOL_STATE_UNINITIALIZED,	/* Internal spa_t state		*/
	POOL_STATE_UNAVAIL,		/* Internal libzfs state	*/
	POOL_STATE_POTENTIALLY_ACTIVE	/* Internal libzfs state	*/
} pool_state_t;

struct nvs_header_t {
	char	  nvh_encoding;		/* encoding method */
	char	  nvh_endian;		/* endianess */
	char	  nvh_reserved1;
	char	  nvh_reserved2;
	uint32_t  nvh_reserved3;
	uint32_t  nvh_reserved4;
	uint32_t  nvh_first_size;	/* first nvpair encode size */
};

struct nvpair {
	uint32_t	nvp_size;
	uint32_t	nvp_unkown;
	uint32_t	nvp_namelen;
	char		nvp_name[0]; /* aligned to 4 bytes */
	/* aligned ptr array for string arrays */
	/* aligned array of data for value */
};

struct nvstring {
	uint32_t	nvs_type;
	uint32_t	nvs_elem;
	uint32_t	nvs_strlen;
	unsigned char	nvs_string[0];
};

struct nvuint64 {
	uint32_t	nvu_type;
	uint32_t	nvu_elem;
	uint64_t	nvu_value;
} __attribute__((packed));

struct nvdirectory {
	uint32_t	nvd_type;
	uint32_t	nvd_unknown[3];
};

struct nvlist {
	uint32_t	nvl_unknown[3];
	struct nvpair	nvl_nvpair;
};

/*
 * Return the offset of the given label.
 */
static uint64_t
label_offset(uint64_t size, int l)
{
	loff_t blk_align = (size % VDEV_LABEL_SIZE);
	return (l * VDEV_LABEL_SIZE + (l < VDEV_LABELS / 2 ?
	    0 : size - VDEV_LABELS * VDEV_LABEL_SIZE - blk_align));
}

static bool zfs_process_value(blkid_probe pr, const char *name, size_t namelen,
    const void *value, size_t max_value_size, unsigned directory_level, int *found)
{
	uint32_t type = be32_to_cpu(*(uint32_t *)value);
	if (strncmp(name, "name", namelen) == 0 &&
	    type == DATA_TYPE_STRING && !directory_level) {
		const struct nvstring *nvs = value;
		if (max_value_size < sizeof(struct nvstring))
			return (false);
		uint32_t nvs_strlen = be32_to_cpu(nvs->nvs_strlen);
		if ((uint64_t)nvs_strlen + sizeof(*nvs) > max_value_size)
			return (false);

		DBG(LOWPROBE, ul_debug("nvstring: type %u string %*s",
				       type, nvs_strlen, nvs->nvs_string));

		blkid_probe_set_label(pr, nvs->nvs_string, nvs_strlen);
		(*found)++;
	} else if (strncmp(name, "guid", namelen) == 0 &&
		   type == DATA_TYPE_UINT64 && !directory_level) {
		const struct nvuint64 *nvu = value;
		uint64_t nvu_value;

		if (max_value_size < sizeof(struct nvuint64))
			return (false);

		memcpy(&nvu_value, &nvu->nvu_value, sizeof(nvu_value));
		nvu_value = be64_to_cpu(nvu_value);

		DBG(LOWPROBE, ul_debug("nvuint64: type %u value %"PRIu64,
				       type, nvu_value));

		blkid_probe_sprintf_value(pr, "UUID_SUB",
					  "%"PRIu64, nvu_value);
		(*found)++;
	} else if (strncmp(name, "pool_guid", namelen) == 0 &&
		   type == DATA_TYPE_UINT64 && !directory_level) {
		const struct nvuint64 *nvu = value;
		uint64_t nvu_value;

		if (max_value_size < sizeof(struct nvuint64))
			return (false);

		memcpy(&nvu_value, &nvu->nvu_value, sizeof(nvu_value));
		nvu_value = be64_to_cpu(nvu_value);

		DBG(LOWPROBE, ul_debug("nvuint64: type %u value %"PRIu64,
				       type, nvu_value));

		blkid_probe_sprintf_uuid(pr, (unsigned char *) &nvu_value,
					 sizeof(nvu_value),
					 "%"PRIu64, nvu_value);
		(*found)++;
	} else if (strncmp(name, "ashift", namelen) == 0 &&
		   type == DATA_TYPE_UINT64) {
		const struct nvuint64 *nvu = value;
		uint64_t nvu_value;

		if (max_value_size < sizeof(struct nvuint64))
			return (false);

		memcpy(&nvu_value, &nvu->nvu_value, sizeof(nvu_value));
		nvu_value = be64_to_cpu(nvu_value);

		if (nvu_value < 32){
			blkid_probe_set_fsblocksize(pr, 1U << nvu_value);
			blkid_probe_set_block_size(pr, 1U << nvu_value);
		}
		(*found)++;
	} else if (strncmp(name, "version", namelen) == 0 &&
		   type == DATA_TYPE_UINT64 && !directory_level) {
		const struct nvuint64 *nvu = value;
		uint64_t nvu_value;
		if (max_value_size < sizeof(struct nvuint64))
			return (false);
		memcpy(&nvu_value, &nvu->nvu_value, sizeof(nvu_value));
		nvu_value = be64_to_cpu(nvu_value);
		DBG(LOWPROBE, ul_debug("nvuint64: type %u value %"PRIu64,
					   type, nvu_value));
		blkid_probe_sprintf_version(pr, "%" PRIu64, nvu_value);
		(*found)++;
	}
	return (true);
}

static bool zfs_extract_guid_name(blkid_probe pr, void *buf, size_t size, bool find_label)
{
	const struct nvlist *nvl;
	const struct nvpair *nvp;
	unsigned directory_level = 0;
	uint64_t state = -1, guid = 0, txg = 0;
	nvl = (const struct nvlist *)buf;
	nvp = &nvl->nvl_nvpair;
	int found = 0;

	 /* Already used up 12 bytes */
	size -= (const unsigned char *)nvp - (const unsigned char *)buf;

	while (size > sizeof(*nvp)) {
		uint32_t nvp_size = be32_to_cpu(nvp->nvp_size);
		uint32_t nvp_namelen = be32_to_cpu(nvp->nvp_namelen);
		uint64_t namesize = ((uint64_t)nvp_namelen + 3) & ~3;
		size_t max_value_size;
		const void *value;
		uint32_t type;

		if (!nvp_size) {
			if (!directory_level)
				/*
				 * End of nvlist!
				 */
				break;
			directory_level--;
			nvp_size = 8;
			goto cont;
		}

		DBG(LOWPROBE, ul_debug("left %zd nvp_size %u",
				       size, nvp_size));

		/* nvpair fits in buffer and name fits in nvpair? */
		if (nvp_size > size || namesize + sizeof(*nvp) > nvp_size)
			return (false);

		DBG(LOWPROBE,
		    ul_debug("nvlist: size %u, namelen %u, name %*s",
			     nvp_size, nvp_namelen, nvp_namelen,
			     nvp->nvp_name));

		max_value_size = nvp_size - (namesize + sizeof(*nvp));
		value = nvp->nvp_name + namesize;
		type = be32_to_cpu(*(uint32_t *)value);

		if (type == DATA_TYPE_UNKNOWN)
			return (false);

		if (type == DATA_TYPE_DIRECTORY) {
			if (max_value_size < sizeof(struct nvdirectory))
				return (false);
			const struct nvdirectory *nvu = value;
			nvp_size = sizeof(*nvp) + namesize + sizeof(*nvu);
			directory_level++;
			goto cont;
		}

		if (find_label) {
			/*
			 * We don't need to parse any tree to find a label
			 */
			if (directory_level)
				goto cont;
			const struct nvuint64 *nvu = value;
			if (!strncmp(nvp->nvp_name, "guid", nvp_namelen) &&
			    type == DATA_TYPE_UINT64) {
				if (max_value_size < sizeof(struct nvuint64))
					return (false);
				memcpy(&guid, &nvu->nvu_value, sizeof(nvu->nvu_value));
				guid = be64_to_cpu(guid);
			} else if (!strncmp(nvp->nvp_name, "state", nvp_namelen) &&
				   type == DATA_TYPE_UINT64) {
				if (max_value_size < sizeof(struct nvuint64))
					return (false);
				memcpy(&state, &nvu->nvu_value, sizeof(nvu->nvu_value));
				state = be64_to_cpu(state);
			} else if (!strncmp(nvp->nvp_name, "txg", nvp_namelen) &&
				   type == DATA_TYPE_UINT64) {
				if (max_value_size < sizeof(struct nvuint64))
					return (false);
				memcpy(&txg, &nvu->nvu_value, sizeof(nvu->nvu_value));
				txg = be64_to_cpu(txg);
			}
		} else {
			if (zfs_process_value(pr, nvp->nvp_name, nvp_namelen, value,
			    max_value_size,directory_level, &found) == false || found >= 5)
				return (false);
		}

cont:
		if (nvp_size > size)
			return (false);
		size -= nvp_size;

		nvp = (struct nvpair *)((char *)nvp + nvp_size);
	}
	if (find_label && guid && state <= POOL_STATE_POTENTIALLY_ACTIVE && (state ==
	    POOL_STATE_L2CACHE || state == POOL_STATE_SPARE || txg > 0))
		return (true);
	return (false);
}

/* ZFS has 128x1kB host-endian root blocks, stored in 2 areas at the start
 * of the disk, and 2 areas at the end of the disk.  Check only some of them...
 * #4 (@ 132kB) is the first one written on a new filesystem. */
static int probe_zfs(blkid_probe pr,
	const struct blkid_idmag *mag  __attribute__((__unused__)))
{
#if BYTE_ORDER == LITTLE_ENDIAN
	int host_endian = 1;
#else
	int host_endian = 0;
#endif
	int swab_endian = 0;
	loff_t offset = 0;
	int label_no;
	struct nvs_header_t *label = NULL;
	bool found_label = false;

	DBG(PROBE, ul_debug("probe_zfs"));

	if (pr->size < ZFS_MINDEVSIZE)
		return (1);

	/* Look for at least one valid label to ensure a positive match */
	for (label_no = 0; label_no < 4; label_no++) {
		offset = label_offset(pr->size, label_no) + VDEV_LABEL_NVPAIR;

		if ((S_ISREG(pr->mode) || blkid_probe_is_wholedisk(pr)) &&
		    blkid_probe_is_covered_by_pt(pr,  offset, VDEV_PHYS_SIZE))
			/* ignore this area, it's within any partition and
			 * we are working with whole-disk now */
			continue;

		label = (struct nvs_header_t *) blkid_probe_get_buffer(pr, offset, VDEV_PHYS_SIZE);

		/*
		 * Label supports XDR encoding, reject for any other unsupported format. Also
		 * endianess can be 0 or 1, reject garbage value. Moreover, check if first
		 * nvpair encode size is non-zero.
		 */
		if (!label || label->nvh_encoding != 0x1 || !be32_to_cpu(label->nvh_first_size) ||
		    (unsigned char) label->nvh_endian > 0x1)
			continue;

		if (host_endian != label->nvh_endian)
			swab_endian = 1;

		if (zfs_extract_guid_name(pr, label, VDEV_PHYS_SIZE, true)) {
			found_label = true;
			break;
		}
	}

	if (!label || !found_label)
		return (1);

	(void) zfs_extract_guid_name(pr, label, VDEV_PHYS_SIZE, false);

	/*
	 * Zero out whole nvlist header including fisrt nvpair size
	 */
	if (blkid_probe_set_magic(pr, offset, sizeof(struct nvs_header_t),
	    (unsigned char *) label))
		return (1);

	blkid_probe_set_fsendianness(pr, !swab_endian ?
			BLKID_ENDIANNESS_NATIVE : BLKID_ENDIANNESS_OTHER);

	return (0);
}

const struct blkid_idinfo zfs_idinfo =
{
	.name		= "zfs_member",
	.usage		= BLKID_USAGE_FILESYSTEM,
	.probefunc	= probe_zfs,
	.minsz		= 64 * 1024 * 1024,
	.magics		= BLKID_NONE_MAGIC
};
