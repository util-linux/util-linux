#ifndef HAVE_BLKID
/*
 * Get label. Used by both mount and umount.
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "linux_fs.h"
#include "get_label_uuid.h"

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

/*
 * Get both label and uuid.
 * For now, only ext2, ext3, xfs, ocfs, reiserfs are supported
 */
int
get_label_uuid(const char *device, char **label, char *uuid) {
	int fd;
	int rv = 1;
	size_t namesize;
	struct ext2_super_block e2sb;
	struct xfs_super_block xfsb;
	struct jfs_super_block jfssb;
	struct ocfs_volume_header ovh;	/* Oracle */
	struct ocfs_volume_label olbl;
	struct reiserfs_super_block reiserfssb;

	fd = open(device, O_RDONLY);
	if (fd < 0)
		return rv;

	/* If there is a RAID partition, or an error, ignore this partition */
	if (is_raid_partition(fd)) {
		close(fd);
		return rv;
	}

	if (lseek(fd, 1024, SEEK_SET) == 1024
	    && read(fd, (char *) &e2sb, sizeof(e2sb)) == sizeof(e2sb)
	    && (ext2magic(e2sb) == EXT2_SUPER_MAGIC)) {
		memcpy(uuid, e2sb.s_uuid, sizeof(e2sb.s_uuid));
		namesize = sizeof(e2sb.s_volume_name);
		if ((*label = calloc(namesize + 1, 1)) != NULL)
			memcpy(*label, e2sb.s_volume_name, namesize);
		rv = 0;
	}
	else if (lseek(fd, 0, SEEK_SET) == 0
	    && read(fd, (char *) &xfsb, sizeof(xfsb)) == sizeof(xfsb)
	    && (strncmp(xfsb.s_magic, XFS_SUPER_MAGIC, 4) == 0)) {
		memcpy(uuid, xfsb.s_uuid, sizeof(xfsb.s_uuid));
		namesize = sizeof(xfsb.s_fname);
		if ((*label = calloc(namesize + 1, 1)) != NULL)
			memcpy(*label, xfsb.s_fname, namesize);
		rv = 0;
	}
	else if (lseek(fd, 0, SEEK_SET) == 0
	    && read(fd, (char *) &ovh, sizeof(ovh)) == sizeof(ovh)
	    && (strncmp(ovh.signature, OCFS_MAGIC, sizeof(OCFS_MAGIC)) == 0)
	    && (lseek(fd, 512, SEEK_SET) == 512)
	    && read(fd, (char *) &olbl, sizeof(olbl)) == sizeof(olbl)) {
		uuid[0] = '\0';
		namesize = ocfslabellen(olbl);
		if ((*label = calloc(namesize + 1, 1)) != NULL)
			memcpy(*label, olbl.label, namesize);
		rv = 0;
	}
	else if (lseek(fd, JFS_SUPER1_OFF, SEEK_SET) == JFS_SUPER1_OFF
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
			memset(uuid, 0, 16);
			namesize = sizeof(jfssb.s_fpack);
			if ((*label = calloc(namesize + 1, 1)) != NULL)
				memcpy(*label, jfssb.s_fpack, namesize);
		} else {
			memcpy(uuid, jfssb.s_uuid, sizeof(jfssb.s_uuid));
			namesize = sizeof(jfssb.s_label);
			if ((*label = calloc(namesize + 1, 1)) != NULL)
			    memcpy(*label, jfssb.s_label, namesize);
		}
		rv = 0;
	}
	else if (lseek(fd, REISERFS_DISK_OFFSET_IN_BYTES, SEEK_SET)
		 == REISERFS_DISK_OFFSET_IN_BYTES
	    && read(fd, (char *) &reiserfssb, sizeof(reiserfssb))
		 == sizeof(reiserfssb)
		/* Only 3.6.x format supers have labels or uuids.
		   Label and UUID can be set by reiserfstune -l/-u. */
	    && reiserfs_magic_version(reiserfssb.s_magic) > 1) {
		namesize = sizeof (reiserfssb.s_label);
		if ((*label = calloc(namesize + 1, 1)) != NULL)
			memcpy(*label, reiserfssb.s_label, namesize);
		memcpy(uuid, reiserfssb.s_uuid, sizeof (reiserfssb.s_uuid));
		rv = 0;
	}

	close(fd);
	return rv;
}
#endif
