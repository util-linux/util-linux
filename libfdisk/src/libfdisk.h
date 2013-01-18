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
struct fdisk_label;
struct fdisk_parttype;

/*
 * Supported partition table types (labels)
 */
enum fdisk_labeltype {
	FDISK_DISKLABEL_DOS = 1,
	FDISK_DISKLABEL_SUN = 2,
	FDISK_DISKLABEL_SGI = 4,
	FDISK_DISKLABEL_AIX = 8,
	FDISK_DISKLABEL_OSF = 16,
	FDISK_DISKLABEL_MAC = 32,
	FDISK_DISKLABEL_GPT = 64,
	FDISK_DISKLABEL_ANY = -1
};

/* init.c */
extern void fdisk_init_debug(int mask);

/* context.h */
extern struct fdisk_context *fdisk_new_context(void);
extern void fdisk_free_context(struct fdisk_context *cxt);

extern int fdisk_context_assign_device(struct fdisk_context *cxt,
				const char *fname, int readonly);

extern struct fdisk_label *fdisk_context_get_label(struct fdisk_context *cxt,
				const char *name);

extern int fdisk_context_switch_label(struct fdisk_context *cxt,
				const char *name);

/* parttype.c */
extern struct fdisk_parttype *fdisk_get_parttype_from_code(struct fdisk_context *cxt,
                                unsigned int code);
extern struct fdisk_parttype *fdisk_get_parttype_from_string(struct fdisk_context *cxt,
                                const char *str);
extern struct fdisk_parttype *fdisk_parse_parttype(struct fdisk_context *cxt, const char *str);

extern struct fdisk_parttype *fdisk_new_unknown_parttype(unsigned int type, const char *typestr);
extern void fdisk_free_parttype(struct fdisk_parttype *type);
extern size_t fdisk_get_nparttypes(struct fdisk_context *cxt);

/* label.c */
extern int fdisk_dev_has_disklabel(struct fdisk_context *cxt);

extern int fdisk_dev_is_disklabel(struct fdisk_context *cxt, enum fdisk_labeltype l);
#define fdisk_is_disklabel(c, x) fdisk_dev_is_disklabel(c, FDISK_DISKLABEL_ ## x)

extern int fdisk_write_disklabel(struct fdisk_context *cxt);
extern int fdisk_verify_disklabel(struct fdisk_context *cxt);
extern int fdisk_create_disklabel(struct fdisk_context *cxt, const char *name);

extern int fdisk_add_partition(struct fdisk_context *cxt, int partnum, struct fdisk_parttype *t);
extern int fdisk_delete_partition(struct fdisk_context *cxt, int partnum);

extern struct fdisk_parttype *fdisk_get_partition_type(struct fdisk_context *cxt, int partnum);
extern int fdisk_set_partition_type(struct fdisk_context *cxt, int partnum,
			     struct fdisk_parttype *t);

extern void fdisk_label_set_changed(struct fdisk_label *lb, int changed);
extern int fdisk_label_is_changed(struct fdisk_label *lb);


/* alignment.c */
extern int fdisk_reset_alignment(struct fdisk_context *cxt);


/* dos.c */
extern int fdisk_dos_enable_compatible(struct fdisk_label *lb, int enable);
extern int fdisk_dos_is_compatible(struct fdisk_label *lb);

#ifdef __cplusplus
}
#endif

#endif /* _LIBFDISK_FDISK_H */
