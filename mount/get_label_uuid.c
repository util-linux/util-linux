#ifndef HAVE_LIBBLKID
/*
 * Get label. Used by mount, umount and swapon.
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "xmalloc.h"
#include "linux_fs.h"
#include "get_label_uuid.h"
#include "../disk-utils/swapheader.h"

/*
 * See whether this device has (the magic of) a RAID superblock at the end.
 * If so, it probably is, or has been, part of a RAID array.
 *
 * For the moment this test is switched off - it causes problems.
 * "Checking for a disk label should only be done on the full raid,
 *  not on the disks that form the raid array. This test causes a lot of
 *  problems when run on my striped promise fasttrak 100 array."
 */
static inline int
is_raid_partition(int fd) {
#if 0
	struct mdp_super_block mdsb;
	int n;

	/* hardcode 4096 here in various places, because that's
	   what it's defined to be.  Note that even if we used
	   the actual kernel headers, sizeof(mdp_super_t) is
	   slightly larger in the 2.2 kernel on 64-bit archs,
	   so using that wouldn't work. */
	lseek(fd, -4096, SEEK_END);	/* Ignore possible error
					   about return value overflow */
	n = 4096;
	if (sizeof(mdsb) < n)
		n = sizeof(mdsb);
	if (read(fd, &mdsb, n) != n)
		return 1;		/* error */
	return (mdsbmagic(mdsb) == MD_SB_MAGIC);
#else
	return 0;
#endif
}

int
reiserfs_magic_version(const char *magic) {
	int rc = 0;

	if (!strncmp(magic, REISERFS_SUPER_MAGIC_STRING,
		     strlen(REISERFS_SUPER_MAGIC_STRING)))
		rc = 1;
	if (!strncmp(magic, REISER2FS_SUPER_MAGIC_STRING, 
		     strlen(REISER2FS_SUPER_MAGIC_STRING)))
		rc = 2;
	if (!strncmp(magic, REISER3FS_SUPER_MAGIC_STRING, 
		     strlen(REISER3FS_SUPER_MAGIC_STRING)))
		rc = 3;
	return rc;
}

static void
store_uuid(char *udest, char *usrc) {
	if (usrc)
		memcpy(udest, usrc, 16);
	else
		memset(udest, 0, 16);
}

static void
store_label(char **ldest, char *lsrc, int len) {
	*ldest = xmalloc(len+1);
	memset(*ldest, 0, len+1);
	memcpy(*ldest, lsrc, len);
}

static int
is_v1_swap_partition(int fd, char **label, char *uuid) {
	int n = getpagesize();
	char *buf = xmalloc(n);
	struct swap_header_v1_2 *p = (struct swap_header_v1_2 *) buf;

	if (lseek(fd, 0, SEEK_SET) == 0
	    && read(fd, buf, n) == n
	    && !strncmp(buf+n-10, "SWAPSPACE2", 10)
	    && p->version == 1) {
		store_uuid(uuid, p->uuid);
		store_label(label, p->volume_name, 16);
		return 1;
	}
	return 0;
}
	    

/*
 * Get both label and uuid.
 * For now, only ext2, ext3, xfs, ocfs, ocfs2, reiserfs, swap are supported
 *
 * Return 0 on success.
 */
int
get_label_uuid(const char *device, char **label, char *uuid) {
	int fd;
	struct ext2_super_block e2sb;
	struct xfs_super_block xfsb;
	struct jfs_super_block jfssb;
	struct ocfs_volume_header ovh;	/* Oracle */
	struct ocfs_volume_label olbl;
	struct ocfs2_super_block osb;
	struct reiserfs_super_block reiserfssb;
	int blksize;
	int rv = 0;

	fd = open(device, O_RDONLY);
	if (fd < 0)
		return -1;

	/* If there is a RAID partition, or an error, ignore this partition */
	if (is_raid_partition(fd)) {
		rv = 1;
		goto done;
	}

	if (is_v1_swap_partition(fd, label, uuid))
		goto done;

	if (lseek(fd, 1024, SEEK_SET) == 1024
	    && read(fd, (char *) &e2sb, sizeof(e2sb)) == sizeof(e2sb)
	    && (ext2magic(e2sb) == EXT2_SUPER_MAGIC)) {
		store_uuid(uuid, e2sb.s_uuid);
		store_label(label, e2sb.s_volume_name,
			    sizeof(e2sb.s_volume_name));
		goto done;
	}

	if (lseek(fd, 0, SEEK_SET) == 0
	    && read(fd, (char *) &xfsb, sizeof(xfsb)) == sizeof(xfsb)
	    && (strncmp(xfsb.s_magic, XFS_SUPER_MAGIC, 4) == 0)) {
		store_uuid(uuid, xfsb.s_uuid);
		store_label(label, xfsb.s_fname, sizeof(xfsb.s_fname));
		goto done;
	}

	if (lseek(fd, 0, SEEK_SET) == 0
	    && read(fd, (char *) &ovh, sizeof(ovh)) == sizeof(ovh)
	    && (strncmp(ovh.signature, OCFS_MAGIC, sizeof(OCFS_MAGIC)) == 0)
	    && (lseek(fd, 512, SEEK_SET) == 512)
	    && read(fd, (char *) &olbl, sizeof(olbl)) == sizeof(olbl)) {
		store_uuid(uuid, NULL);
		store_label(label, olbl.label, ocfslabellen(olbl));
		goto done;
	}

	if (lseek(fd, JFS_SUPER1_OFF, SEEK_SET) == JFS_SUPER1_OFF
	    && read(fd, (char *) &jfssb, sizeof(jfssb)) == sizeof(jfssb)
	    && (strncmp(jfssb.s_magic, JFS_MAGIC, 4) == 0)) {

/* The situation for jfs is rather messy. The structure of the
   superblock changed a few times, but there seems to be no good way
   to check what kind of sb we have.
   Old (OS/2 compatible) jfs filesystems don't have UUIDs and have
   an 11-byte label in s_fpack[].
   Kernel 2.5.6 supports jfs v1; 2.5.8 supports v2; 2.5.18 has label/uuid.
   Kernel 2.4.20 supports jfs v2 with label/uuid.
   s_version will be 2 for new filesystems using an external log.
   Other new filesystems will have version 1.
   Label and UUID can be set by jfs_tune. */

/* Let us believe label/uuid on v2, and on v1 only when label agrees
   with s_fpack in the first 11 bytes. */

		if (assemble4le(jfssb.s_version) == 1 &&
		    strncmp(jfssb.s_label, jfssb.s_fpack, 11) != 0) {
			store_uuid(uuid, NULL);
			store_label(label, jfssb.s_fpack,
				    sizeof(jfssb.s_fpack));
		} else {
			store_uuid(uuid, jfssb.s_uuid);
			store_label(label, jfssb.s_label,
				    sizeof(jfssb.s_label));
		}
		goto done;
	}

	if (lseek(fd, REISERFS_DISK_OFFSET_IN_BYTES, SEEK_SET)
	    == REISERFS_DISK_OFFSET_IN_BYTES
	    && read(fd, (char *) &reiserfssb, sizeof(reiserfssb))
	    == sizeof(reiserfssb)
	    /* Only 3.6.x format supers have labels or uuids.
	       Label and UUID can be set by reiserfstune -l/-u. */
	    && reiserfs_magic_version(reiserfssb.s_magic) > 1) {
		store_uuid(uuid, reiserfssb.s_uuid);
		store_label(label, reiserfssb.s_label,
			    sizeof(reiserfssb.s_label));
		goto done;
	}

	for (blksize = OCFS2_MIN_BLOCKSIZE;
	     blksize <= OCFS2_MAX_BLOCKSIZE;
	     blksize <<= 1) {
		int blkoff = blksize * OCFS2_SUPER_BLOCK_BLKNO;

		if (lseek(fd, blkoff, SEEK_SET) == blkoff
		    && read(fd, (char *) &osb, sizeof(osb)) == sizeof(osb)
		    && strncmp(osb.signature,
			       OCFS2_SUPER_BLOCK_SIGNATURE,
			       sizeof(OCFS2_SUPER_BLOCK_SIGNATURE)) == 0) {
			store_uuid(uuid, osb.s_uuid);
			store_label(label, osb.s_label, sizeof(osb.s_label));
			goto done;
		}
	}
	rv = 1;
 done:
	close(fd);
	return rv;
}
#endif
