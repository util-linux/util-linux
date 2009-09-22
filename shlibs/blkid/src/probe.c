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
#include "probers/probers.h"

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

/* filter bitmap macros */
#define blkid_bmp_wordsize		(8 * sizeof(unsigned long))
#define blkid_bmp_idx_bit(item)		(1UL << ((item) % blkid_bmp_wordsize))
#define blkid_bmp_idx_byte(item)	((item) / blkid_bmp_wordsize)

#define blkid_bmp_set_item(bmp, item)	\
		((bmp)[ blkid_bmp_idx_byte(item) ] |= blkid_bmp_idx_bit(item))

#define blkid_bmp_unset_item(bmp, item)	\
		((bmp)[ bmp_idx_byte(item) ] &= ~bmp_idx_bit(item))

#define blkid_bmp_get_item(bmp, item)	\
		((bmp)[ blkid_bmp_idx_byte(item) ] & blkid_bmp_idx_bit(item))

#define blkid_bmp_size(max_items) \
		(((max_items) + blkid_bmp_wordsize) / blkid_bmp_wordsize)

#define BLKID_FLTR_ITEMS	ARRAY_SIZE(idinfos)
#define BLKID_FLTR_SIZE		blkid_bmp_size(BLKID_FLTR_ITEMS)


static int blkid_probe_set_usage(blkid_probe pr, int usage);

int blkid_known_fstype(const char *fstype)
{
	int i;

	if (!fstype)
		return 0;

	for (i = 0; i < ARRAY_SIZE(idinfos); i++) {
		const struct blkid_idinfo *id = idinfos[i];
		if (strcmp(id->name, fstype) == 0)
			return 1;
	}
	return 0;
}

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

static void blkid_probe_reset_vals(blkid_probe pr)
{
	memset(pr->vals, 0, sizeof(pr->vals));
	pr->nvals = 0;
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

int blkid_probe_set_request(blkid_probe pr, int flags)
{
	if (!pr)
		return -1;
	pr->probreq = flags;
	return 0;
}

int blkid_probe_reset_filter(blkid_probe pr)
{
	if (!pr)
		return -1;
	if (pr->fltr)
		memset(pr->fltr, 0, BLKID_FLTR_SIZE * sizeof(unsigned long));
	blkid_probe_reset_idx(pr);
	return 0;
}

/*
 * flag:
 *
 *  BLKID_FLTR_NOTIN  - probe all filesystems which are NOT IN names[]
 *
 *  BLKID_FLTR_ONLYIN - probe filesystem which are IN names[]
 */
int blkid_probe_filter_types(blkid_probe pr, int flag, char *names[])
{
	int i;

	if (!pr || !names)
		return -1;
	if (!pr->fltr) {
		pr->fltr = calloc(BLKID_FLTR_SIZE, sizeof(unsigned long));
		blkid_probe_reset_idx(pr);
	} else
		blkid_probe_reset_filter(pr);

	if (!pr->fltr)
		return -1;

	for (i = 0; i < ARRAY_SIZE(idinfos); i++) {
		int has = 0;
		const struct blkid_idinfo *id = idinfos[i];
		char **n;

		for (n = names; *n; n++) {
			if (!strcmp(id->name, *n)) {
				has = 1;
				break;
			}
		}
		/* The default is enable all filesystems,
		 * set relevant bitmap bit means disable the filesystem.
		 */
		if (flag & BLKID_FLTR_ONLYIN) {
		       if (!has)
				blkid_bmp_set_item(pr->fltr, i);
		} else if (flag & BLKID_FLTR_NOTIN) {
			if (has)
				blkid_bmp_set_item(pr->fltr, i);
		}
	}
	DBG(DEBUG_LOWPROBE, printf("a new probing type-filter initialized\n"));
	return 0;
}

/*
 * flag:
 *
 *  BLKID_FLTR_NOTIN  - probe all filesystems which are NOT IN "usage"
 *
 *  BLKID_FLTR_ONLYIN - probe filesystem which are IN "usage"
 *
 * where the "usage" is a set of filesystem according the usage flag (crypto,
 * raid, filesystem, ...)
 */
int blkid_probe_filter_usage(blkid_probe pr, int flag, int usage)
{
	int i;

	if (!pr || !usage)
		return -1;
	if (!pr->fltr) {
		pr->fltr = calloc(BLKID_FLTR_SIZE, sizeof(unsigned long));
		blkid_probe_reset_idx(pr);
	} else
		blkid_probe_reset_filter(pr);

	if (!pr->fltr)
		return -1;

	for (i = 0; i < ARRAY_SIZE(idinfos); i++) {
		const struct blkid_idinfo *id = idinfos[i];

		if (id->usage & usage) {
			if (flag & BLKID_FLTR_NOTIN)
				blkid_bmp_set_item(pr->fltr, i);
		} else if (flag & BLKID_FLTR_ONLYIN)
			blkid_bmp_set_item(pr->fltr, i);
	}
	DBG(DEBUG_LOWPROBE, printf("a new probing usage-filter initialized\n"));
	return 0;
}


int blkid_probe_invert_filter(blkid_probe pr)
{
	int i;

	if (!pr || !pr->fltr)
		return -1;
	for (i = 0; i < BLKID_FLTR_SIZE; i++)
		pr->fltr[i] = ~pr->fltr[i];

	blkid_probe_reset_idx(pr);
	DBG(DEBUG_LOWPROBE, printf("probing filter inverted\n"));
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


static struct blkid_prval *blkid_probe_assign_value(
			blkid_probe pr, const char *name)
{
	struct blkid_prval *v;

	if (!name)
		return NULL;
	if (pr->nvals >= BLKID_PROBVAL_NVALS)
		return NULL;

	v = &pr->vals[pr->nvals];
	v->name = name;
	pr->nvals++;

	DBG(DEBUG_LOWPROBE, printf("assigning %s\n", name));
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

int blkid_probe_set_version(blkid_probe pr, const char *version)
{
	if (pr->probreq & BLKID_PROBREQ_VERSION)
		return blkid_probe_set_value(pr, "VERSION",
			   (unsigned char *) version, strlen(version) + 1);
	return 0;
}

int blkid_probe_sprintf_version(blkid_probe pr, const char *fmt, ...)
{
	int rc = 0;

	if (pr->probreq & BLKID_PROBREQ_VERSION) {
		va_list ap;

		va_start(ap, fmt);
		rc = blkid_probe_vsprintf_value(pr, "VERSION", fmt, ap);
		va_end(ap);
	}
	return rc;
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


/* Removes whitespace from the right-hand side of a string (trailing
 * whitespace).
 *
 * Returns size of the new string (without \0).
 */
static size_t blkid_rtrim_whitespace(unsigned char *str)
{
	size_t i = strlen((char *) str);

	while (i--) {
		if (!isspace(str[i]))
			break;
	}
	str[++i] = '\0';
	return i;
}

int blkid_probe_set_label(blkid_probe pr, unsigned char *label, size_t len)
{
	struct blkid_prval *v;

	if (len > BLKID_PROBVAL_BUFSIZ)
		len = BLKID_PROBVAL_BUFSIZ;

	if ((pr->probreq & BLKID_PROBREQ_LABELRAW) &&
	    blkid_probe_set_value(pr, "LABEL_RAW", label, len) < 0)
		return -1;
	if (!(pr->probreq & BLKID_PROBREQ_LABEL))
		return 0;
	v = blkid_probe_assign_value(pr, "LABEL");
	if (!v)
		return -1;

	memcpy(v->data, label, len);
	v->data[len] = '\0';
	v->len = blkid_rtrim_whitespace(v->data) + 1;
	return 0;
}

static size_t encode_to_utf8(int enc, unsigned char *dest, size_t len,
			unsigned char *src, size_t count)
{
	size_t i, j;
	uint16_t c;

	for (j = i = 0; i + 2 <= count; i += 2) {
		if (enc == BLKID_ENC_UTF16LE)
			c = (src[i+1] << 8) | src[i];
		else /* BLKID_ENC_UTF16BE */
			c = (src[i] << 8) | src[i+1];
		if (c == 0) {
			dest[j] = '\0';
			break;
		} else if (c < 0x80) {
			if (j+1 >= len)
				break;
			dest[j++] = (uint8_t) c;
		} else if (c < 0x800) {
			if (j+2 >= len)
				break;
			dest[j++] = (uint8_t) (0xc0 | (c >> 6));
			dest[j++] = (uint8_t) (0x80 | (c & 0x3f));
		} else {
			if (j+3 >= len)
				break;
			dest[j++] = (uint8_t) (0xe0 | (c >> 12));
			dest[j++] = (uint8_t) (0x80 | ((c >> 6) & 0x3f));
			dest[j++] = (uint8_t) (0x80 | (c & 0x3f));
		}
	}
	dest[j] = '\0';
	return j;
}

int blkid_probe_set_utf8label(blkid_probe pr, unsigned char *label,
				size_t len, int enc)
{
	struct blkid_prval *v;

	if ((pr->probreq & BLKID_PROBREQ_LABELRAW) &&
	    blkid_probe_set_value(pr, "LABEL_RAW", label, len) < 0)
		return -1;
	if (!(pr->probreq & BLKID_PROBREQ_LABEL))
		return 0;
	v = blkid_probe_assign_value(pr, "LABEL");
	if (!v)
		return -1;

	encode_to_utf8(enc, v->data, sizeof(v->data), label, len);
	v->len = blkid_rtrim_whitespace(v->data) + 1;
	return 0;
}

/* like uuid_is_null() from libuuid, but works with arbitrary size of UUID */
static int uuid_is_empty(const unsigned char *buf, size_t len)
{
	int i;

	for (i = 0; i < len; i++)
		if (buf[i])
			return 0;
	return 1;
}

int blkid_probe_sprintf_uuid(blkid_probe pr, unsigned char *uuid,
				size_t len, const char *fmt, ...)
{
	int rc = -1;
	va_list ap;

	if (len > BLKID_PROBVAL_BUFSIZ)
		len = BLKID_PROBVAL_BUFSIZ;

	if (uuid_is_empty(uuid, len))
		return 0;

	if ((pr->probreq & BLKID_PROBREQ_UUIDRAW) &&
	    blkid_probe_set_value(pr, "UUID_RAW", uuid, len) < 0)
		return -1;
	if (!(pr->probreq & BLKID_PROBREQ_UUID))
		return 0;

	va_start(ap, fmt);
	rc = blkid_probe_vsprintf_value(pr, "UUID", fmt, ap);
	va_end(ap);

	/* convert to lower case (..be paranoid) */
	if (!rc) {
		int i;
		struct blkid_prval *v = &pr->vals[pr->nvals];

		for (i = 0; i < v->len; i++)
			if (v->data[i] >= 'A' && v->data[i] <= 'F')
				v->data[i] = (v->data[i] - 'A') + 'a';
	}
	return rc;
}

/* function to set UUIDs that are in suberblocks stored as strings */
int blkid_probe_strncpy_uuid(blkid_probe pr, unsigned char *str, size_t len)
{
	struct blkid_prval *v;

	if (str == NULL || *str == '\0')
		return -1;
	if (!len)
		len = strlen((char *) str);
	if (len > BLKID_PROBVAL_BUFSIZ)
		len = BLKID_PROBVAL_BUFSIZ;

	if ((pr->probreq & BLKID_PROBREQ_UUIDRAW) &&
	    blkid_probe_set_value(pr, "UUID_RAW", str, len) < 0)
		return -1;
	if (!(pr->probreq & BLKID_PROBREQ_UUID))
		return 0;

	v = blkid_probe_assign_value(pr, "UUID");
	if (v) {
		memcpy((char *) v->data, str, len);
		*(v->data + len) = '\0';
		v->len = len;
		return 0;
	}
	return -1;
}

/* default _set_uuid function to set DCE UUIDs */
int blkid_probe_set_uuid_as(blkid_probe pr, unsigned char *uuid, const char *name)
{
	struct blkid_prval *v;

	if (uuid_is_empty(uuid, 16))
		return 0;

	if (!name) {
		if ((pr->probreq & BLKID_PROBREQ_UUIDRAW) &&
		    blkid_probe_set_value(pr, "UUID_RAW", uuid, 16) < 0)
			return -1;
		if (!(pr->probreq & BLKID_PROBREQ_UUID))
			return 0;

		v = blkid_probe_assign_value(pr, "UUID");
	} else
		v = blkid_probe_assign_value(pr, name);

#ifdef HAVE_LIBUUID
	{
		uuid_unparse(uuid, (char *) v->data);
		v->len = 37;
	}
#else
	v->len = snprintf(v->data, sizeof(v->data),
		"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		uuid[0], uuid[1], uuid[2], uuid[3],
		uuid[4], uuid[5],
		uuid[6], uuid[7],
		uuid[8], uuid[9],
		uuid[10], uuid[11], uuid[12], uuid[13], uuid[14],uuid[15]);
	v->len++;
#endif
	return 0;
}

int blkid_probe_set_uuid(blkid_probe pr, unsigned char *uuid)
{
	return blkid_probe_set_uuid_as(pr, uuid, NULL);
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

