/*
 * Low-level libblkid probing API
 *
 * Copyright (C) 2008-2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

/**
 * SECTION: lowprobe
 * @title: Low-level probing
 * @short_description: low-level prober initialization
 *
 * The low-level probing routines always and directly read information from
 * the selected (see blkid_probe_set_device()) device.
 *
 * The probing routines are grouped together into separate chains. Currently,
 * the librray provides superblocks, partitions and topology chains.
 *
 * The probing routines is possible to filter (enable/disable) by type (e.g.
 * fstype "vfat" or partype "gpt") or by usage flags (e.g. BLKID_USAGE_RAID).
 * These filters are per-chain. Note that always when you touch the chain
 * filter the current probing position is reseted and probing starts from
 * scratch.  It means that the chain filter should not be modified during
 * probing, for example in loop where you call blkid_do_probe().
 *
 * For more details see the chain specific documentation.
 *
 * The low-level API provides two ways how access to probing results.
 *
 *   1. The NAME=value (tag) interface. This interface is older and returns all data
 *      as strings. This interface is generic for all chains.
 *
 *   2. The binary interfaces. These interfaces return data in the native formats.
 *      The interface is always specific to the probing chain.
 *
 */

/**
 * SECTION: lowprobe-tags
 * @title: Low-level tags
 * @short_description: generic NAME=value interface.
 *
 * The probing routines inside the chain are mutually exclusive by default --
 * only few probing routines are marked as "tolerant". The "tolerant" probing
 * routines are used for filesystem which can share the same device with any
 * other filesystem. The blkid_do_safeprobe() checks for the "tolerant" flag.
 *
 * The SUPERBLOCKS chain is enabled by default. The all others chains is
 * necessary to enable by blkid_probe_enable_'CHAINNAME'(). See chains specific
 * documentation.
 *
 * The blkid_do_probe() function returns a result from only one probing
 * routine, and the next call from the next probing routine. It means you need
 * to call the function in loop, for example:
 *
 * <informalexample>
 *   <programlisting>
 *	while((blkid_do_probe(pr) == 0)
 *		... use result ...
 *   </programlisting>
 * </informalexample>
 *
 * The blkid_do_safeprobe() is the same as blkid_do_probe(), but returns only
 * first probing result for every enabled chain. This function checks for
 * ambivalent results (e.g. more "intolerant" filesystems superblocks on the
 * device).
 *
 * The probing result is set of NAME=value pairs (the NAME is always unique).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_MKDEV_H
#include <sys/mkdev.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <stdint.h>
#include <stdarg.h>

#ifdef HAVE_LIBUUID
# ifdef HAVE_UUID_UUID_H
#  include <uuid/uuid.h>
# else
#  include <uuid.h>
# endif
#endif

#include "blkdev.h"
#include "blkidP.h"

/* chains */
extern const struct blkid_chaindrv superblocks_drv;
extern const struct blkid_chaindrv topology_drv;
extern const struct blkid_chaindrv partitions_drv;

/*
 * All supported chains
 */
static const struct blkid_chaindrv *chains_drvs[] = {
	[BLKID_CHAIN_SUBLKS] = &superblocks_drv,
	[BLKID_CHAIN_TOPLGY] = &topology_drv,
	[BLKID_CHAIN_PARTS] = &partitions_drv
};

static void blkid_probe_reset_vals(blkid_probe pr);

/**
 * blkid_new_probe:
 *
 * Returns: a pointer to the newly allocated probe struct.
 */
blkid_probe blkid_new_probe(void)
{
	int i;
	blkid_probe pr;

	blkid_init_debug(0);
	pr = calloc(1, sizeof(struct blkid_struct_probe));
	if (!pr)
		return NULL;

	/* initialize chains */
	for (i = 0; i < BLKID_NCHAINS; i++) {
		pr->chains[i].driver = chains_drvs[i];
		pr->chains[i].flags = chains_drvs[i]->dflt_flags;
		pr->chains[i].enabled = chains_drvs[i]->dflt_enabled;
	}
	return pr;
}

/**
 * blkid_free_probe:
 * @pr: probe
 *
 * Deallocates the probe struct, buffers and all allocated
 * data that are associated with this probing control struct.
 */
void blkid_free_probe(blkid_probe pr)
{
	int i;

	if (!pr)
		return;

	for (i = 0; i < BLKID_NCHAINS; i++) {
		struct blkid_chain *ch = &pr->chains[i];

		if (ch->driver->free_data)
			ch->driver->free_data(pr, ch->data);
		free(ch->fltr);
	}
	free(pr->buf);
	free(pr->sbbuf);
	free(pr);
}

static void blkid_probe_reset_buffer(blkid_probe pr)
{
	DBG(DEBUG_LOWPROBE, printf("reseting blkid probe buffer\n"));
	if (pr->buf)
		memset(pr->buf, 0, pr->buf_max);
	pr->buf_off = 0;
	pr->buf_len = 0;
	if (pr->sbbuf)
		memset(pr->sbbuf, 0, BLKID_SB_BUFSIZ);
	pr->sbbuf_len = 0;
}


/*
 * Removes chain values from probing result.
 */
void blkid_probe_chain_reset_vals(blkid_probe pr, struct blkid_chain *chn)
{
	int nvals = pr->nvals;
	int i, x;

	for (x = 0, i = 0; i < pr->nvals; i++) {
		struct blkid_prval *v = &pr->vals[i];

		if (v->chain != chn && x == i) {
			x++;
			continue;
		}
		if (v->chain == chn) {
			--nvals;
			continue;
		}
		memcpy(&pr->vals[x++], v, sizeof(struct blkid_prval));
	}
	pr->nvals = nvals;
}

/*
 * Copies chain values from probing result to @vals, the max size of @vals is
 * @nvals and returns real number of values.
 */
int blkid_probe_chain_copy_vals(blkid_probe pr, struct blkid_chain *chn,
		struct blkid_prval *vals, int nvals)
{
	int i, x;

	for (x = 0, i = 0; i < pr->nvals && x < nvals; i++) {
		struct blkid_prval *v = &pr->vals[i];

		if (v->chain != chn)
			continue;
		memcpy(&vals[x++], v, sizeof(struct blkid_prval));
	}
	return x;
}

/*
 * Appends values from @vals to the probing result
 */
void blkid_probe_append_vals(blkid_probe pr, struct blkid_prval *vals, int nvals)
{
	int i = 0;

	while (i < nvals && pr->nvals < BLKID_NVALS) {
		memcpy(&pr->vals[pr->nvals++], &vals[i++],
				sizeof(struct blkid_prval));
	}
}

static void blkid_probe_reset_vals(blkid_probe pr)
{
	memset(pr->vals, 0, sizeof(pr->vals));
	pr->nvals = 0;
}

struct blkid_chain *blkid_probe_get_chain(blkid_probe pr)
{
	return pr->cur_chain;
}

void *blkid_probe_get_binary_data(blkid_probe pr, struct blkid_chain *chn)
{
	int rc;

	if (!pr && !chn)
		return NULL;

	pr->cur_chain = chn;
	chn->idx = -1;			/* start probing from scratch */
	chn->binary = TRUE;

	rc = chn->driver->probe(pr, chn);

	chn->binary = FALSE;
	pr->cur_chain = NULL;

	if (rc < 0)
		return NULL;

	DBG(DEBUG_LOWPROBE,
		printf("returning %s binary data\n", chn->driver->name));
	return chn->data;
}


/**
 * blkid_reset_probe:
 * @pr: probe
 *
 * Cleanup probing result. This function does not touch probing filters
 * and keeps assigned device.
 */
void blkid_reset_probe(blkid_probe pr)
{
	int i;

	if (!pr)
		return;

	blkid_probe_reset_buffer(pr);
	blkid_probe_reset_vals(pr);

	for (i = 0; i < BLKID_NCHAINS; i++)
		pr->chains[i].idx = -1;
}

/***
static int blkid_probe_dump_filter(blkid_probe pr, int chain)
{
	struct blkid_chain *chn;
	int i;

	if (!pr || chain < 0 || chain >= BLKID_NCHAINS)
		return -1;

	chn = &pr->chains[chain];

	if (!chn->fltr)
		return -1;

	for (i = 0; i < chn->driver->nidinfos; i++) {
		const struct blkid_idinfo *id = chn->driver->idinfos[i];

		DBG(DEBUG_LOWPROBE, printf("%d: %s: %s\n",
			i,
			id->name,
			blkid_bmp_get_item(chn->fltr, i)
				? "disabled" : "enabled <--"));
	}
	return 0;
}
***/

/*
 * Returns properly initialized chain filter
 */
unsigned long *blkid_probe_get_filter(blkid_probe pr, int chain, int create)
{
	struct blkid_chain *chn;

	if (!pr || chain < 0 || chain >= BLKID_NCHAINS)
		return NULL;

	chn = &pr->chains[chain];

	/* always when you touch the chain filter all indexes are reseted and
	 * probing starts from scratch
	 */
	chn->idx = -1;
	pr->cur_chain = NULL;

	if (!chn->driver->has_fltr || (!chn->fltr && !create))
		return NULL;

	if (!chn->fltr)
		chn->fltr = calloc(1, blkid_bmp_nbytes(chn->driver->nidinfos));
	else
		memset(chn->fltr, 0, blkid_bmp_nbytes(chn->driver->nidinfos));

	/* blkid_probe_dump_filter(pr, chain); */
	return chn->fltr;
}

/*
 * Generic private functions for filter setting
 */
int __blkid_probe_invert_filter(blkid_probe pr, int chain)
{
	int i;
	struct blkid_chain *chn;
	unsigned long *fltr;

	fltr = blkid_probe_get_filter(pr, chain, FALSE);
	if (!fltr)
		return -1;

	chn = &pr->chains[chain];

	for (i = 0; i < blkid_bmp_nwords(chn->driver->nidinfos); i++)
		fltr[i] = ~fltr[i];

	DBG(DEBUG_LOWPROBE, printf("probing filter inverted\n"));
	/* blkid_probe_dump_filter(pr, chain); */
	return 0;
}

int __blkid_probe_reset_filter(blkid_probe pr, int chain)
{
	return blkid_probe_get_filter(pr, chain, FALSE) ? 0 : -1;
}

int __blkid_probe_filter_types(blkid_probe pr, int chain, int flag, char *names[])
{
	unsigned long *fltr;
	struct blkid_chain *chn;
	int i;

	fltr = blkid_probe_get_filter(pr, chain, TRUE);
	if (!fltr)
		return -1;

	chn = &pr->chains[chain];

	for (i = 0; i < chn->driver->nidinfos; i++) {
		int has = 0;
		const struct blkid_idinfo *id = chn->driver->idinfos[i];
		char **n;

		for (n = names; *n; n++) {
			if (!strcmp(id->name, *n)) {
				has = 1;
				break;
			}
		}
		if (flag & BLKID_FLTR_ONLYIN) {
		       if (!has)
				blkid_bmp_set_item(fltr, i);
		} else if (flag & BLKID_FLTR_NOTIN) {
			if (has)
				blkid_bmp_set_item(fltr, i);
		}
	}

	DBG(DEBUG_LOWPROBE,
		printf("%s: a new probing type-filter initialized\n",
		chn->driver->name));
	/* blkid_probe_dump_filter(pr, chain); */
	return 0;
}

/*
 * Note that we have two offsets:
 *
 *	1/ general device offset (pr->off), that's useful for example when we
 *	   probe a partition from whole disk image:
 *	               blkid-low --offset  <partition_position> disk.img
 *
 *	2/ buffer offset (the 'off' argument), that useful for offsets in
 *	   superbloks, ...
 *
 *	That means never use lseek(fd, 0, SEEK_SET), the zero position is always
 *	pr->off, so lseek(fd, pr->off, SEEK_SET).
 *
 */
unsigned char *blkid_probe_get_buffer(blkid_probe pr,
				blkid_loff_t off, blkid_loff_t len)
{
	ssize_t ret_read = 0;

	if (off < 0 || len < 0) {
		DBG(DEBUG_LOWPROBE,
			printf("unexpected offset or length of buffer requested\n"));
		return NULL;
	}
	if (off + len <= BLKID_SB_BUFSIZ) {
		if (!pr->sbbuf) {
			pr->sbbuf = malloc(BLKID_SB_BUFSIZ);
			if (!pr->sbbuf)
				return NULL;
		}
		if (!pr->sbbuf_len) {
			if (lseek(pr->fd, pr->off, SEEK_SET) < 0)
				return NULL;
			ret_read = read(pr->fd, pr->sbbuf, BLKID_SB_BUFSIZ);
			if (ret_read < 0)
				ret_read = 0;
			pr->sbbuf_len = ret_read;
		}
		if (off + len > pr->sbbuf_len)
			return NULL;
		return pr->sbbuf + off;
	} else {
		unsigned char *newbuf = NULL;

		if (len > pr->buf_max) {
			newbuf = realloc(pr->buf, len);
			if (!newbuf)
				return NULL;
			pr->buf = newbuf;
			pr->buf_max = len;
			pr->buf_off = 0;
			pr->buf_len = 0;
		}
		if (newbuf || off < pr->buf_off ||
		    off + len > pr->buf_off + pr->buf_len) {

			if (blkid_llseek(pr->fd, pr->off + off, SEEK_SET) < 0)
				return NULL;

			ret_read = read(pr->fd, pr->buf, len);
			if (ret_read != (ssize_t) len)
				return NULL;
			pr->buf_off = off;
			pr->buf_len = len;
		}
		return off ? pr->buf + (off - pr->buf_off) : pr->buf;
	}
}

/**
 * blkid_probe_set_device:
 * @pr: probe
 * @fd: device file descriptor
 * @off: begin of probing area
 * @size: size of probing area
 *
 * Assigns the device to probe control struct, resets internal buffers and
 * reads 512 bytes from device to the buffers.
 *
 * Returns: -1 in case of failure, or 0 on success.
 */
int blkid_probe_set_device(blkid_probe pr, int fd,
		blkid_loff_t off, blkid_loff_t size)
{
	if (!pr)
		return -1;

	blkid_reset_probe(pr);

	pr->fd = fd;
	pr->off = off;
	pr->size = 0;
	pr->devno = 0;
	pr->mode = 0;
	pr->blkssz = 0;

	if (size)
		pr->size = size;
	else {
		struct stat sb;

		if (fstat(fd, &sb))
			return -1;

		pr->mode = sb.st_mode;

		if (S_ISBLK(sb.st_mode)) {
			blkdev_get_size(fd, (unsigned long long *) &pr->size);
			pr->devno = sb.st_rdev;
		} else
			pr->size = sb.st_size;
	}
	if (!pr->size)
		return -1;

	/* read SB to test if the device is readable */
	if (!blkid_probe_get_buffer(pr, 0, 0x200)) {
		DBG(DEBUG_LOWPROBE,
			printf("failed to prepare a device for low-probing\n"));
		return -1;
	}

	DBG(DEBUG_LOWPROBE, printf("ready for low-probing, offset=%zd, size=%zd\n",
				pr->off, pr->size));
	return 0;
}

int blkid_probe_get_dimension(blkid_probe pr,
		blkid_loff_t *off, blkid_loff_t *size)
{
	if (!pr)
		return -1;

	*off = pr->off;
	*size = pr->size;
	return 0;
}

int blkid_probe_set_dimension(blkid_probe pr,
		blkid_loff_t off, blkid_loff_t size)
{
	if (!pr)
		return -1;

	DBG(DEBUG_LOWPROBE, printf(
		"changing probing area: size=%llu, off=%llu "
		"-to-> size=%llu, off=%llu\n",
		(unsigned long long) pr->size,
		(unsigned long long) pr->off,
		(unsigned long long) size,
		(unsigned long long) off));

	pr->off = off;
	pr->size = size;

	blkid_probe_reset_buffer(pr);

	return 0;
}

/**
 * blkid_do_probe:
 * @pr: prober
 *
 * Calls probing functions in all enabled chains. The superblocks chain is
 * enabled by default. The blkid_do_probe() stores result from only one
 * probing function. It's necessary to call this routine in a loop to get
 * resuluts from all probing functions in all chains.
 *
 * This is string-based NAME=value interface only.
 *
 * <example>
 *   <title>basic case - use the first result only</title>
 *   <programlisting>
 *
 *	if (blkid_do_probe(pr) == 0) {
 *		int nvals = blkid_probe_numof_values(pr);
 *		for (n = 0; n < nvals; n++) {
 *			if (blkid_probe_get_value(pr, n, &name, &data, &len) == 0)
 *				printf("%s = %s\n", name, data);
 *		}
 *	}
 *  </programlisting>
 * </example>
 *
 * <example>
 *   <title>advanced case - probe for all signatures</title>
 *   <programlisting>
 *
 *	while (blkid_do_probe(pr) == 0) {
 *		int nvals = blkid_probe_numof_values(pr);
 *		...
 *	}
 *  </programlisting>
 * </example>
 *
 * See also blkid_reset_probe().
 *
 * Returns: 0 on success, 1 when probing is done and -1 in case of error.
 */
int blkid_do_probe(blkid_probe pr)
{
	int rc = 1;

	if (!pr)
		return -1;

	do {
		struct blkid_chain *chn;

		if (!pr->cur_chain)
			pr->cur_chain = &pr->chains[0];
		else if (pr->cur_chain < &pr->chains[BLKID_NCHAINS - 1])
			pr->cur_chain += sizeof(struct blkid_chain);
		else
			return 1;	/* all chains already probed */

		chn = pr->cur_chain;
		chn->binary = FALSE;		/* for sure... */

		DBG(DEBUG_LOWPROBE, printf("chain probe %s %s\n",
				chn->driver->name,
				chn->enabled? "ENABLED" : "DISABLED"));

		if (!chn->enabled)
			continue;

		/* rc: -1 = error, 0 = success, 1 = no result */
		rc = chn->driver->probe(pr, chn);

	} while (rc == 1);

	return rc;
}

/**
 * blkid_do_safeprobe:
 * @pr: prober
 *
 * This function gathers probing results from all enabled chains and checks
 * for ambivalent results (e.g. more filesystems on the device).
 *
 * This is string-based NAME=value interface only.
 *
 * Note about suberblocks chain -- the function does not check for filesystems
 * when a RAID signature is detected.  The function also does not check for
 * collision between RAIDs. The first detected RAID is returned.
 *
 * Returns: 0 on success, 1 if nothing is detected, -2 if ambivalen result is
 * detected and -1 on case of error.
 */
int blkid_do_safeprobe(blkid_probe pr)
{
	int i, count = 0, rc = 0;

	if (!pr)
		return -1;

	for (i = 0; i < BLKID_NCHAINS; i++) {
		struct blkid_chain *chn;

		chn = pr->cur_chain = &pr->chains[i];
		chn->binary = FALSE;		/* for sure... */

		DBG(DEBUG_LOWPROBE, printf("chain safeprobe %s %s\n",
				chn->driver->name,
				chn->enabled? "ENABLED" : "DISABLED"));

		if (!chn->enabled)
			continue;

		chn->idx = - 1;

		/* rc: -2 ambivalent, -1 = error, 0 = success, 1 = no result */
		rc = chn->driver->safeprobe(pr, chn);
		if (rc < 0)
			goto done;	/* error */
		if (rc == 0)
			count++;	/* success */
	}

done:
	pr->cur_chain = NULL;
	if (rc < 0)
		return rc;
	return count ? 0 : 1;
}

/**
 * blkid_do_fullprobe:
 * @pr: prober
 *
 * This function gathers probing results from all enabled chains. Same as
 * blkid_so_safeprobe() but does not check for collision between probing
 * result.
 *
 * This is string-based NAME=value interface only.
 *
 * Returns: 0 on success, 1 if nothing is detected or -1 on case of error.
 */
int blkid_do_fullprobe(blkid_probe pr)
{
	int i, count = 0, rc = 0;

	if (!pr)
		return -1;

	for (i = 0; i < BLKID_NCHAINS; i++) {
		int rc;
		struct blkid_chain *chn;

		chn = pr->cur_chain = &pr->chains[i];
		chn->binary = FALSE;		/* for sure... */

		DBG(DEBUG_LOWPROBE, printf("chain fullprobe %s: %s\n",
				chn->driver->name,
				chn->enabled? "ENABLED" : "DISABLED"));

		if (!chn->enabled)
			continue;

		chn->idx = - 1;

		/* rc: -1 = error, 0 = success, 1 = no result */
		rc = chn->driver->probe(pr, chn);
		if (rc < 0)
			goto done;	/* error */
		if (rc == 0)
			count++;	/* success */
	}

done:
	pr->cur_chain = NULL;
	if (rc < 0)
		return rc;
	return count ? 0 : 1;
}

/* same sa blkid_probe_get_buffer() but works with 512-sectors */
unsigned char *blkid_probe_get_sector(blkid_probe pr, unsigned int sector)
{
	return pr ? blkid_probe_get_buffer(pr,
			((blkid_loff_t) sector) << 9, 0x200) : NULL;
}

struct blkid_prval *blkid_probe_assign_value(
			blkid_probe pr, const char *name)
{
	struct blkid_prval *v;

	if (!name)
		return NULL;
	if (pr->nvals >= BLKID_NVALS)
		return NULL;

	v = &pr->vals[pr->nvals];
	v->name = name;
	v->chain = pr->cur_chain;
	pr->nvals++;

	DBG(DEBUG_LOWPROBE,
		printf("assigning %s [%s]\n", name, v->chain->driver->name));
	return v;
}

int blkid_probe_set_value(blkid_probe pr, const char *name,
		unsigned char *data, size_t len)
{
	struct blkid_prval *v;

	if (len > BLKID_PROBVAL_BUFSIZ)
		len = BLKID_PROBVAL_BUFSIZ;

	v = blkid_probe_assign_value(pr, name);
	if (!v)
		return -1;

	memcpy(v->data, data, len);
	v->len = len;
	return 0;
}

int blkid_probe_vsprintf_value(blkid_probe pr, const char *name,
		const char *fmt, va_list ap)
{
	struct blkid_prval *v;
	size_t len;

	v = blkid_probe_assign_value(pr, name);
	if (!v)
		return -1;

	len = vsnprintf((char *) v->data, sizeof(v->data), fmt, ap);

	if (len <= 0) {
		pr->nvals--; /* reset the latest assigned value */
		return -1;
	}
	v->len = len + 1;
	return 0;
}

int blkid_probe_sprintf_value(blkid_probe pr, const char *name,
		const char *fmt, ...)
{
	int rc;
	va_list ap;

	va_start(ap, fmt);
	rc = blkid_probe_vsprintf_value(pr, name, fmt, ap);
	va_end(ap);

	return rc;
}

/**
 * blkid_probe_get_devno:
 * @pr: probe
 *
 * Returns: block device number, or 0 for regilar files.
 */
dev_t blkid_probe_get_devno(blkid_probe pr)
{
	if (!pr->devno) {
		struct stat sb;

		if (fstat(pr->fd, &sb) == 0 && S_ISBLK(sb.st_mode))
			pr->devno = sb.st_rdev;
	}
	return pr->devno;
}

/**
 * blkid_probe_get_size:
 * @pr: probe
 *
 * Returns: block device (or file) size in bytes or -1 in case of error.
 */
blkid_loff_t blkid_probe_get_size(blkid_probe pr)
{
	return pr ? pr->size : -1;
}

/**
 * blkid_probe_get_sectorsize:
 * @pr: probe
 *
 * Returns: block device hardware sector size (BLKSSZGET ioctl, default 512).
 */
unsigned int blkid_probe_get_sectorsize(blkid_probe pr)
{
	if (!pr)
		return DEFAULT_SECTOR_SIZE;  /*... and good luck! */
	if (pr->blkssz)
		return pr->blkssz;
	if (!pr->mode) {
		struct stat st;

		if (fstat(pr->fd, &st))
			goto fallback;
		pr->mode = st.st_mode;
	}
	if (S_ISBLK(pr->mode)) {
	    if (blkdev_get_sector_size(pr->fd, (int *) &pr->blkssz))
		goto fallback;

	    return pr->blkssz;
	}

fallback:
	pr->blkssz = DEFAULT_SECTOR_SIZE;
	return pr->blkssz;
}

/**
 * blkid_probe_numof_values:
 * @pr: probe
 *
 * Returns: number of values in probing result or -1 in case of error.
 */
int blkid_probe_numof_values(blkid_probe pr)
{
	if (!pr)
		return -1;
	return pr->nvals;
}

/**
 * blkid_probe_get_value:
 * @pr: probe
 * @num: wanted value in range 0..N, where N is blkid_probe_numof_values() - 1
 * @name: pointer to return value name or NULL
 * @data: pointer to return value data or NULL
 * @len: pointer to return value length or NULL
 *
 * Returns: 0 on success, or -1 in case of error.
 */
int blkid_probe_get_value(blkid_probe pr, int num, const char **name,
			const char **data, size_t *len)
{
	struct blkid_prval *v = __blkid_probe_get_value(pr, num);

	if (!v)
		return -1;
	if (name)
		*name = v->name;
	if (data)
		*data = (char *) v->data;
	if (len)
		*len = v->len;

	DBG(DEBUG_LOWPROBE, printf("returning %s value\n", v->name));
	return 0;
}

/**
 * blkid_probe_lookup_value:
 * @pr: probe
 * @name: name of value
 * @data: pointer to return value data or NULL
 * @len: pointer to return value length or NULL
 *
 * Returns: 0 on success, or -1 in case of error.
 */
int blkid_probe_lookup_value(blkid_probe pr, const char *name,
			const char **data, size_t *len)
{
	struct blkid_prval *v = __blkid_probe_lookup_value(pr, name);

	if (!v)
		return -1;
	if (data)
		*data = (char *) v->data;
	if (len)
		*len = v->len;
	DBG(DEBUG_LOWPROBE, printf("returning %s value\n", v->name));
	return 0;
}

/**
 * blkid_probe_has_value:
 * @pr: probe
 * @name: name of value
 *
 * Returns: 1 if value exist in probing result, otherwise 0.
 */
int blkid_probe_has_value(blkid_probe pr, const char *name)
{
	if (blkid_probe_lookup_value(pr, name, NULL, NULL) == 0)
		return 1;
	return 0;
}

struct blkid_prval *__blkid_probe_get_value(blkid_probe pr, int num)
{
	if (pr == NULL || num < 0 || num >= pr->nvals)
		return NULL;

	return &pr->vals[num];
}

struct blkid_prval *__blkid_probe_lookup_value(blkid_probe pr, const char *name)
{
	int i;

	if (pr == NULL || pr->nvals == 0 || name == NULL)
		return NULL;

	for (i = 0; i < pr->nvals; i++) {
		struct blkid_prval *v = &pr->vals[i];

		if (v->name && strcmp(name, v->name) == 0) {
			DBG(DEBUG_LOWPROBE, printf("returning %s value\n", v->name));
			return v;
		}
	}
	return NULL;
}


/* converts DCE UUID (uuid[16]) to human readable string
 * - the @len should be always 37 */
void blkid_unparse_uuid(const unsigned char *uuid, char *str, size_t len)
{
#ifdef HAVE_LIBUUID
	uuid_unparse(uuid, str);
#else
	snprintf(str, len,
		"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		uuid[0], uuid[1], uuid[2], uuid[3],
		uuid[4], uuid[5],
		uuid[6], uuid[7],
		uuid[8], uuid[9],
		uuid[10], uuid[11], uuid[12], uuid[13], uuid[14],uuid[15]);
#endif
}

