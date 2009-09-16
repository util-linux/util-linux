/*
 * topology - gathers information about device topology
 *
 * Copyright 2009 Red Hat, Inc.  All rights reserved.
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 *
 * NOTE: this chain supports one probing method only, so it's implemented
 *       without array of probing functions (idinfos array).
 *
 *       This chain does not support probing functions filtering. The chain
 *       could be enable or disabled only.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "blkdev.h"
#include "blkidP.h"

/**
 * SECTION:topology
 * @title: Topology information
 * @short_description: block device tolology information.
 *
 * The tolology chain provides details about Linux block devices, for more
 * information see:
 *
 *      Linux kernel Documentation/ABI/testing/sysfs-block
 *
 * NAME=value (tags) interface is enabled by blkid_probe_enable_topology(),
 * and provides:
 *
 * @MINIMUM_IO_SIZE: minimum size which is the device's preferred unit of I/O
 *
 * @OPTIMAL_IO_SIZE: usually the stripe width for RAID or zero
 *
 * @ALIGNMENT_OFFSET: indicates how many bytes the beginning o the device is
 *                    offset from the disk's natural alignment.
 *
 * Binary interface:
 *
 * blkid_probe_get_tolology()
 *
 * blkid_topology_get_'VALUENAME'()
 */
static int topology_probe(blkid_probe pr, struct blkid_chain *chn);
static void topology_free(blkid_probe pr, void *data);

/*
 * Driver definition
 */
const struct blkid_chaindrv topology_drv = {
	.id           = BLKID_CHAIN_TOPLGY,
	.name         = "topology",
	.dflt_enabled = FALSE,
	.probe        = topology_probe,
	.safeprobe    = topology_probe,
	.free_data    = topology_free
};

/*
 * Binary interface
 */
struct blkid_struct_topology {
	unsigned long	alignment_offset;
	unsigned long	minimum_io_size;
	unsigned long	optimal_io_size;
};

/**
 * blkid_probe_enable_topology:
 * @pr: probe
 * @enable: TRUE/FALSE
 *
 * Enables/disables the topology probing for non-binary interface.
 *
 * Returns: 0 on success, or -1 in case of error.
 */
int blkid_probe_enable_topology(blkid_probe pr, int enable)
{
	if (!pr)
		return -1;
	pr->chains[BLKID_CHAIN_TOPLGY].enabled = enable;
	return 0;
}

/**
 * blkid_probe_get_topology:
 * @pr: probe
 *
 * This is a binary interface for topology values. See also blkid_topology_*
 * functions.
 *
 * This function is independent on blkid_do_[safe,full]probe() and
 * blkid_probe_enable_topology() calls.
 *
 * Returns: blkid_tolopogy, or NULL in case of error.
 */
blkid_topology blkid_probe_get_topology(blkid_probe pr)
{
	return (blkid_topology) blkid_probe_get_binary_data(pr,
			&pr->chains[BLKID_CHAIN_TOPLGY]);
}

static unsigned long
dev_topology_attribute(const char *attribute, dev_t dev, dev_t *primary)
{
	const char *sysfs_fmt_str = "/sys/dev/block/%d:%d/%s";
	char path[PATH_MAX];
	int len;
	FILE *fp = NULL;
	struct stat info;
	unsigned long result = 0UL;

	len = snprintf(path, sizeof(path), sysfs_fmt_str,
			major(dev), minor(dev), attribute);
	if (len < 0 || len + 1 > sizeof(path))
		goto err;

	/*
	 * check if the desired sysfs attribute exists
	 * - if not: either the kernel doesn't have topology support or the
	 *   device could be a partition
	 */
	if (stat(path, &info) < 0) {
		if (!*primary &&
		    blkid_devno_to_wholedisk(dev, NULL, 0, primary))
			goto err;

		/* get attribute from partition's primary device */
		len = snprintf(path, sizeof(path), sysfs_fmt_str,
				major(*primary), minor(*primary), attribute);
		if (len < 0 || len + 1 > sizeof(path))
			goto err;
	}

	fp = fopen(path, "r");
	if (!fp) {
		DBG(DEBUG_LOWPROBE, printf(
			"topology: %s: fopen failed, errno=%d\n", path, errno));
		goto err;
	}

	if (fscanf(fp, "%lu", &result) != 1) {
		DBG(DEBUG_LOWPROBE, printf(
			"topology: %s: unexpected file format\n", path));
		goto err;
	}

	fclose(fp);

	DBG(DEBUG_LOWPROBE,
		printf("topology: attribute %s = %lu (sectors)\n", attribute, result));

	return result * DEFAULT_SECTOR_SIZE;
err:
	if (fp)
		fclose(fp);
	DBG(DEBUG_LOWPROBE,
		printf("topology: failed to read %s attribute\n", attribute));
	return 0;
}

/*
 * Topology values
 */
static struct topology_val {
	const char *val_name;		/* NAME=value */
	const char *sysfs_name;		/* /sys/dev/block/<maj>:<min>/NAME */
	const size_t bin_offset;	/* blkid_struct_topology member */
} topology_vals[] = {
	{ "ALIGNMENT_OFFSET", "alignment_offset",
		offsetof(struct blkid_struct_topology, alignment_offset) },
	{ "MINIMUM_IO_SIZE", "queue/minimum_io_size",
		offsetof(struct blkid_struct_topology, minimum_io_size) },
	{"OPTIMAL_IO_SIZE", "queue/optimal_io_size",
		offsetof(struct blkid_struct_topology, optimal_io_size) }
};

static int topology_set_value(blkid_probe pr, struct blkid_chain *chn,
				struct topology_val *val, unsigned long data)
{
	if (chn->binary) {
		unsigned long *v =
			(unsigned long *) (chn->data + val->bin_offset);
		*v = data;
		return 0;
	}
	return blkid_probe_sprintf_value(pr, val->val_name, "%llu", data);
}

/*
 * The blkid_do_probe() backend.
 */
static int topology_probe(blkid_probe pr, struct blkid_chain *chn)
{
	dev_t dev, pri_dev = 0;
	int i, rc = 0, count = 0;

	if (!pr)
		return -1;

	blkid_probe_chain_reset_vals(pr, chn);

	dev = blkid_probe_get_devno(pr);
	if (!dev)
		return 1;	/* no result */

	if (chn->binary) {
		DBG(DEBUG_LOWPROBE, printf("initialize topology binary data\n"));

		if (chn->data)
			/* reset binary data */
			memset(chn->data, 0,
					sizeof(struct blkid_struct_topology));
		else {
			chn->data = calloc(1,
					sizeof(struct blkid_struct_topology));
			if (!chn->data)
				return -1;
		}
	}

	DBG(DEBUG_LOWPROBE, printf("--> starting probing loop [TOPOLOGY]\n"));

	for (i = 0; i < ARRAY_SIZE(topology_vals); i++) {
		struct topology_val *val = &topology_vals[i];
		unsigned long data;

		/*
		 * Don't bother reporting any of the topology information
		 * if it's zero.
		 */
		data = dev_topology_attribute(val->sysfs_name, dev, &pri_dev);
		if (!data)
			continue;

		rc = topology_set_value(pr, chn, val, data);
		if (rc)
			break;	/* error */
		count++;
	}


	if (rc == 0 && count == 0)
		rc = 1;			/* no result */

	DBG(DEBUG_LOWPROBE,
		printf("<-- leaving probing loop [TOPOLOGY, rc=%d]\n", rc));
	return rc;
}

static void topology_free(blkid_probe pr, void *data)
{
	free(data);
}

/**
 * blkid_topology_get_alignment_offset:
 * @tp: topology
 *
 * Returns: alignment offset in bytes or 0.
 */
unsigned long blkid_topology_get_alignment_offset(blkid_topology tp)
{
	return tp ? tp->alignment_offset : 0;
}

/**
 * blkid_topology_get_minimum_io_size:
 * @tp: topology
 *
 * Returns: minimum io size in bytes or 0.
 */
unsigned long blkid_topology_get_minimum_io_size(blkid_topology tp)
{
	return tp ? tp->minimum_io_size : 0;
}

/**
 * blkid_topology_get_optimal_io_size
 * @tp: topology
 *
 * Returns: optimal io size in bytes or 0.
 */
unsigned long blkid_topology_get_optimal_io_size(blkid_topology tp)
{
	return tp ? tp->optimal_io_size : 0;
}

