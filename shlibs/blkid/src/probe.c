/*
 * probe.c - reads tags (LABEL, UUID, FS type, ..) from a block device
 *
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
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

#include "superblocks/superblocks.h"

static const struct blkid_idinfo *idinfos[] =
{
	/* RAIDs */
	&linuxraid_idinfo,
	&ddfraid_idinfo,
	&iswraid_idinfo,
	&lsiraid_idinfo,
	&viaraid_idinfo,
	&silraid_idinfo,
	&nvraid_idinfo,
	&pdcraid_idinfo,
	&highpoint45x_idinfo,
	&highpoint37x_idinfo,
	&adraid_idinfo,
	&jmraid_idinfo,
	&lvm2_idinfo,
	&lvm1_idinfo,
	&snapcow_idinfo,
	&luks_idinfo,

	/* Filesystems */
	&vfat_idinfo,
	&swsuspend_idinfo,
	&swap_idinfo,
	&xfs_idinfo,
	&ext4dev_idinfo,
	&ext4_idinfo,
	&ext3_idinfo,
	&ext2_idinfo,
	&jbd_idinfo,
	&reiser_idinfo,
	&reiser4_idinfo,
	&jfs_idinfo,
	&udf_idinfo,
	&iso9660_idinfo,
	&zfs_idinfo,
	&hfsplus_idinfo,
	&hfs_idinfo,
	&ufs_idinfo,
	&hpfs_idinfo,
	&sysv_idinfo,
        &xenix_idinfo,
	&ntfs_idinfo,
	&cramfs_idinfo,
	&romfs_idinfo,
	&minix_idinfo,
	&gfs_idinfo,
	&gfs2_idinfo,
	&ocfs_idinfo,
	&ocfs2_idinfo,
	&oracleasm_idinfo,
	&vxfs_idinfo,
	&squashfs_idinfo,
	&netware_idinfo,
	&btrfs_idinfo
};

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#define BLKID_FLTR_ITEMS	ARRAY_SIZE(idinfos)
#define BLKID_FLTR_SIZE		blkid_bmp_nwords(BLKID_FLTR_ITEMS)

static int blkid_probe_set_usage(blkid_probe pr, int usage);

/*
 * Returns a pointer to the newly allocated probe struct
 */
blkid_probe blkid_new_probe(void)
{
	blkid_init_debug(0);
	return calloc(1, sizeof(struct blkid_struct_probe));
}

/*
 * Deallocates probe struct, buffers and all allocated
 * data that are associated with this probing control struct.
 */
void blkid_free_probe(blkid_probe pr)
{
	if (!pr)
		return;
	free(pr->fltr);
	free(pr->buf);
	free(pr->sbbuf);
	free(pr);
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

static void blkid_probe_reset_idx(blkid_probe pr)
{
	pr->idx = -1;
}

void blkid_reset_probe(blkid_probe pr)
{
	if (!pr)
		return;
	DBG(DEBUG_LOWPROBE, printf("reseting blkid_probe\n"));
	if (pr->buf)
		memset(pr->buf, 0, pr->buf_max);
	pr->buf_off = 0;
	pr->buf_len = 0;
	if (pr->sbbuf)
		memset(pr->sbbuf, 0, BLKID_SB_BUFSIZ);
	pr->sbbuf_len = 0;
	blkid_probe_reset_vals(pr);
	blkid_probe_reset_idx(pr);
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

/*
 * Assignes the device to probe control struct, resets internal buffers and
 * reads 512 bytes from device to the buffers.
 *
 * Returns -1 in case of failure, or 0 on success.
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

	if (size)
		pr->size = size;
	else {
		struct stat sb;

		if (fstat(fd, &sb))
			return -1;

		if (S_ISBLK(sb.st_mode))
			blkdev_get_size(fd, (unsigned long long *) &pr->size);
		else
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

/*
 * The blkid_do_probe() calls the probe functions. This routine could be used
 * in a loop when you need to probe for all possible filesystems/raids.
 *
 * 1/ basic case -- use the first result:
 *
 *	if (blkid_do_probe(pr) == 0) {
 *		int nvals = blkid_probe_numof_values(pr);
 *		for (n = 0; n < nvals; n++) {
 *			if (blkid_probe_get_value(pr, n, &name, &data, &len) == 0)
 *				printf("%s = %s\n", name, data);
 *		}
 *	}
 *
 * 2/ advanced case -- probe for all signatures (don't forget that some
 *                     filesystems can co-exist on one volume (e.g. CD-ROM).
 *
 *	while (blkid_do_probe(pr) == 0) {
 *		int nvals = blkid_probe_numof_values(pr);
 *		...
 *	}
 *
 *    The internal probing index (pointer to the last probing function) is
 *    always reseted when you touch probing filter or set a new device. It
 *    means you cannot use:
 *
 *      blkid_probe_invert_filter()
 *      blkid_probe_filter_usage()
 *      blkid_probe_filter_types()
 *      blkid_probe_reset_filter()
 *      blkid_probe_set_device()
 *
 *    in the loop (e.g while()) when you iterate on all signatures.
 */
int blkid_do_probe(blkid_probe pr)
{
	int i = 0;

	if (!pr || pr->idx < -1)
		return -1;

	blkid_probe_reset_vals(pr);

	DBG(DEBUG_LOWPROBE,
		printf("--> starting probing loop [idx=%d]\n",
		pr->idx));

	i = pr->idx + 1;

	for ( ; i < ARRAY_SIZE(idinfos); i++) {
		const struct blkid_idinfo *id;
		const struct blkid_idmag *mag;
		int hasmag = 0;

		pr->idx = i;

		if (pr->fltr && blkid_bmp_get_item(pr->fltr, i))
			continue;

		id = idinfos[i];
		mag = id->magics ? &id->magics[0] : NULL;

		/* try to detect by magic string */
		while(mag && mag->magic) {
			int idx;
			unsigned char *buf;

			idx = mag->kboff + (mag->sboff >> 10);
			buf = blkid_probe_get_buffer(pr, idx << 10, 1024);

			if (buf && !memcmp(mag->magic,
					buf + (mag->sboff & 0x3ff), mag->len)) {
				DBG(DEBUG_LOWPROBE, printf(
					"%s: magic sboff=%u, kboff=%ld\n",
					id->name, mag->sboff, mag->kboff));
				hasmag = 1;
				break;
			}
			mag++;
		}

		if (hasmag == 0 && id->magics && id->magics[0].magic)
			/* magic string(s) defined, but not found */
			continue;

		/* final check by probing function */
		if (id->probefunc) {
			DBG(DEBUG_LOWPROBE, printf(
				"%s: call probefunc()\n", id->name));
			if (id->probefunc(pr, mag) != 0)
				continue;
		}

		/* all cheks passed */
		if (pr->probreq & BLKID_PROBREQ_TYPE)
			blkid_probe_set_value(pr, "TYPE",
				(unsigned char *) id->name,
				strlen(id->name) + 1);
		if (pr->probreq & BLKID_PROBREQ_USAGE)
			blkid_probe_set_usage(pr, id->usage);

		DBG(DEBUG_LOWPROBE,
			printf("<-- leaving probing loop (type=%s) [idx=%d]\n",
			id->name, pr->idx));
		return 0;
	}
	DBG(DEBUG_LOWPROBE,
		printf("<-- leaving probing loop (failed) [idx=%d]\n",
		pr->idx));
	return 1;
}

/*
 * This is the same function as blkid_do_probe(), but returns only one result
 * (cannot be used in while()) and checks for ambivalen results (more
 * filesystems on the device) -- in such case returns -2.
 *
 * The function does not check for filesystems when a RAID signature is
 * detected.  The function also does not check for collision between RAIDs. The
 * first detected RAID is returned.
 */
int blkid_do_safeprobe(blkid_probe pr)
{
	struct blkid_struct_probe first;
	int count = 0;
	int intol = 0;
	int rc;

	while ((rc = blkid_do_probe(pr)) == 0) {
		if (!count) {
			/* store the fist result */
			memcpy(first.vals, pr->vals, sizeof(first.vals));
			first.nvals = pr->nvals;
			first.idx = pr->idx;
		}
		count++;

		if (idinfos[pr->idx]->usage & BLKID_USAGE_RAID)
			break;
		if (!(idinfos[pr->idx]->flags & BLKID_IDINFO_TOLERANT))
			intol++;
	}
	if (rc < 0)
		return rc;		/* error */
	if (count > 1 && intol) {
		DBG(DEBUG_LOWPROBE,
			printf("ERROR: ambivalent result detected (%d filesystems)!\n",
			count));
		return -2;		/* error, ambivalent result (more FS) */
	}
	if (!count)
		return 1;		/* nothing detected */

	/* restore the first result */
	memcpy(pr->vals, first.vals, sizeof(first.vals));
	pr->nvals = first.nvals;
	pr->idx = first.idx;

	return 0;
}

int blkid_probe_numof_values(blkid_probe pr)
{
	if (!pr)
		return -1;
	return pr->nvals;
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

static int blkid_probe_set_usage(blkid_probe pr, int usage)
{
	char *u = NULL;

	if (usage & BLKID_USAGE_FILESYSTEM)
		u = "filesystem";
	else if (usage & BLKID_USAGE_RAID)
		u = "raid";
	else if (usage & BLKID_USAGE_CRYPTO)
		u = "crypto";
	else if (usage & BLKID_USAGE_OTHER)
		u = "other";
	else
		u = "unknown";

	return blkid_probe_set_value(pr, "USAGE", (unsigned char *) u, strlen(u) + 1);
}

int blkid_probe_get_value(blkid_probe pr, int num, const char **name,
			const char **data, size_t *len)
{
	struct blkid_prval *v;

	if (pr == NULL || num < 0 || num >= pr->nvals)
		return -1;

	v = &pr->vals[num];
	if (name)
		*name = v->name;
	if (data)
		*data = (char *) v->data;
	if (len)
		*len = v->len;

	DBG(DEBUG_LOWPROBE, printf("returning %s value\n", v->name));
	return 0;
}

int blkid_probe_lookup_value(blkid_probe pr, const char *name,
			const char **data, size_t *len)
{
	int i;

	if (pr == NULL || pr->nvals == 0 || name == NULL)
		return -1;

	for (i = 0; i < pr->nvals; i++) {
		struct blkid_prval *v = &pr->vals[i];

		if (v->name && strcmp(name, v->name) == 0) {
			if (data)
				*data = (char *) v->data;
			if (len)
				*len = v->len;
			DBG(DEBUG_LOWPROBE, printf("returning %s value\n", v->name));
			return 0;
		}
	}
	return -1;
}

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

