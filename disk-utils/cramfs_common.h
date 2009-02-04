/*
 * cramfs_common - cramfs common code
 *
 * Copyright (c) 2008 Roy Peled, the.roy.peled  -at-  gmail
 * Copyright (c) 2004-2006 by Michael Holzt, kju -at- fqdn.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __CRAMFS_COMMON_H
#define __CRAMFS_COMMON_H

#include "cramfs.h"

#ifndef HOST_IS_BIG_ENDIAN
#ifdef WORDS_BIGENDIAN
#define HOST_IS_BIG_ENDIAN 1
#else
#define HOST_IS_BIG_ENDIAN 0
#endif
#endif

u32 u32_toggle_endianness(int big_endian, u32 what);
void super_toggle_endianness(int from_big_endian, struct cramfs_super *super);
void inode_to_host(int from_big_endian, struct cramfs_inode *inode_in, struct cramfs_inode *inode_out);
void inode_from_host(int to_big_endian, struct cramfs_inode *inode_in, struct cramfs_inode *inode_out);

#endif
