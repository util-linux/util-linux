/*
 * probe.h - constants and on-disk structures for extracting device data
 *
 * Copyright (C) 1999 by Andries Brouwer
 * Copyright (C) 1999, 2000, 2003 by Theodore Ts'o
 * Copyright (C) 2001 by Andreas Dilger
 * Copyright (C) 2008 Karel Zak <kzak@redhat.com>
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 * %End-Header%
 */

#ifndef _BLKID_PROBE_H
#define _BLKID_PROBE_H

extern const struct blkid_idinfo cramfs_idinfo;
extern const struct blkid_idinfo swap_idinfo;
extern const struct blkid_idinfo swsuspend_idinfo;
extern const struct blkid_idinfo adraid_idinfo;
extern const struct blkid_idinfo ddfraid_idinfo;
extern const struct blkid_idinfo iswraid_idinfo;

#endif /* _BLKID_PROBE_H */
