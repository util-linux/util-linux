/*
 * libfdisk.h - libfdisk API
 *
 * Copyright (C) 2012 Karel Zak <kzak@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _LIBFDISK_FDISK_H
#define _LIBFDISK_FDISK_H

#ifdef __cplusplus
extern "C" {
#endif

struct fdisk_context;
struct fdisk_parttype;

/* init.c */
extern void fdisk_init_debug(int mask);

/* parttype.c */
extern struct fdisk_parttype *fdisk_get_parttype_from_code(struct fdisk_context *cxt,
                                unsigned int code);
extern struct fdisk_parttype *fdisk_get_parttype_from_string(struct fdisk_context *cxt,
                                const char *str);
extern struct fdisk_parttype *fdisk_parse_parttype(struct fdisk_context *cxt, const char *str);

extern struct fdisk_parttype *fdisk_new_unknown_parttype(unsigned int type, const char *typestr);
extern void fdisk_free_parttype(struct fdisk_parttype *type);
extern size_t fdisk_get_nparttypes(struct fdisk_context *cxt);

#ifdef __cplusplus
}
#endif

#endif /* _LIBFDISK_FDISK_H */
