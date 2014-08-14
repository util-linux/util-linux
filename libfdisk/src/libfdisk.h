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

#include <stdarg.h>
#include <stdint.h>

struct fdisk_context;
struct fdisk_label;
struct fdisk_parttype;
struct fdisk_partition;
struct fdisk_ask;
struct fdisk_iter;
struct fdisk_table;

/*
 * Supported partition table types (labels)
 */
enum fdisk_labeltype {
	FDISK_DISKLABEL_DOS = (1 << 1),
	FDISK_DISKLABEL_SUN = (1 << 2),
	FDISK_DISKLABEL_SGI = (1 << 3),
	FDISK_DISKLABEL_BSD = (1 << 4),
	FDISK_DISKLABEL_GPT = (1 << 5)
};

enum {
	FDISK_ASKTYPE_NONE = 0,
	FDISK_ASKTYPE_NUMBER,
	FDISK_ASKTYPE_OFFSET,
	FDISK_ASKTYPE_WARN,
	FDISK_ASKTYPE_WARNX,
	FDISK_ASKTYPE_INFO,
	FDISK_ASKTYPE_YESNO,
	FDISK_ASKTYPE_STRING,
	FDISK_ASKTYPE_MENU
};

/* extra flags for info massages (see fdisk_sinfo() */
enum {
	FDISK_INFO_SUCCESS	/* info after successful action */
};

/* init.c */
extern void fdisk_init_debug(int mask);

/* context.h */
extern struct fdisk_context *fdisk_new_context(void);
extern struct fdisk_context *fdisk_new_nested_context(
			struct fdisk_context *parent, const char *name);
extern void fdisk_free_context(struct fdisk_context *cxt);

extern int fdisk_context_set_ask(struct fdisk_context *cxt,
			int (*ask_cb)(struct fdisk_context *, struct fdisk_ask *, void *),
			void *data);

extern int fdisk_context_is_readonly(struct fdisk_context *cxt);
extern int fdisk_context_assign_device(struct fdisk_context *cxt,
				const char *fname, int readonly);
extern int fdisk_context_deassign_device(struct fdisk_context *cxt, int nosync);

extern struct fdisk_label *fdisk_context_get_label(struct fdisk_context *cxt,
				const char *name);
extern int fdisk_context_next_label(struct fdisk_context *cxt, struct fdisk_label **lb);
extern size_t fdisk_context_get_nlabels(struct fdisk_context *cxt);

extern int fdisk_context_switch_label(struct fdisk_context *cxt,
				const char *name);

extern int fdisk_context_set_unit(struct fdisk_context *cxt, const char *str);

#define PLURAL	0
#define SINGULAR 1
extern const char *fdisk_context_get_unit(struct fdisk_context *cxt, int n);

extern unsigned int fdisk_context_get_units_per_sector(struct fdisk_context *cxt);

extern int fdisk_context_enable_details(struct fdisk_context *cxt, int enable);
extern int fdisk_context_use_cylinders(struct fdisk_context *cxt);
extern int fdisk_context_display_details(struct fdisk_context *cxt);

/* parttype.c */
extern struct fdisk_parttype *fdisk_get_parttype_from_code(struct fdisk_context *cxt,
                                unsigned int code);
extern struct fdisk_parttype *fdisk_get_parttype_from_string(struct fdisk_context *cxt,
                                const char *str);
extern struct fdisk_parttype *fdisk_parse_parttype(struct fdisk_context *cxt, const char *str);

extern struct fdisk_parttype *fdisk_new_unknown_parttype(unsigned int type, const char *typestr);
extern void fdisk_free_parttype(struct fdisk_parttype *type);
extern size_t fdisk_get_nparttypes(struct fdisk_context *cxt);

extern int fdisk_is_parttype_string(struct fdisk_context *cxt);

/* label.c */
enum {
	FDISK_COL_NONE = 0,

	/* generic */
	FDISK_COL_DEVICE,
	FDISK_COL_START,
	FDISK_COL_END,
	FDISK_COL_SECTORS,
	FDISK_COL_CYLINDERS,
	FDISK_COL_SIZE,
	FDISK_COL_TYPE,
	FDISK_COL_TYPEID,

	/* label specific */
	FDISK_COL_ATTR,
	FDISK_COL_BOOT,
	FDISK_COL_BSIZE,
	FDISK_COL_CPG,
	FDISK_COL_EADDR,
	FDISK_COL_FSIZE,
	FDISK_COL_NAME,
	FDISK_COL_SADDR,
	FDISK_COL_UUID,
};

extern int fdisk_require_geometry(struct fdisk_context *cxt);
extern int fdisk_missing_geometry(struct fdisk_context *cxt);

extern int fdisk_dev_has_disklabel(struct fdisk_context *cxt);

extern int fdisk_dev_is_disklabel(struct fdisk_context *cxt, enum fdisk_labeltype l);
#define fdisk_is_disklabel(c, x) fdisk_dev_is_disklabel(c, FDISK_DISKLABEL_ ## x)

extern int fdisk_write_disklabel(struct fdisk_context *cxt);
extern int fdisk_verify_disklabel(struct fdisk_context *cxt);
extern int fdisk_create_disklabel(struct fdisk_context *cxt, const char *name);
extern int fdisk_list_disklabel(struct fdisk_context *cxt);
extern int fdisk_locate_disklabel(struct fdisk_context *cxt, int n, const char **name, off_t *offset, size_t *size);

extern int fdisk_get_disklabel_id(struct fdisk_context *cxt, char **id);
extern int fdisk_set_disklabel_id(struct fdisk_context *cxt);

extern int fdisk_get_partition(struct fdisk_context *cxt, size_t partno, struct fdisk_partition **pa);

extern int fdisk_add_partition(struct fdisk_context *cxt, struct fdisk_partition *pa);
extern int fdisk_delete_partition(struct fdisk_context *cxt, size_t partnum);

extern int fdisk_set_partition_type(struct fdisk_context *cxt, size_t partnum,
			     struct fdisk_parttype *t);

extern int fdisk_get_columns(struct fdisk_context *cxt, int all, int **cols, size_t *ncols);

extern void fdisk_label_set_changed(struct fdisk_label *lb, int changed);
extern int fdisk_label_is_changed(struct fdisk_label *lb);

extern void fdisk_label_set_disabled(struct fdisk_label *lb, int disabled);
extern int fdisk_label_is_disabled(struct fdisk_label *lb);

extern int fdisk_is_partition_used(struct fdisk_context *cxt, size_t n);

extern int fdisk_partition_toggle_flag(struct fdisk_context *cxt, size_t partnum, unsigned long flag);

extern struct fdisk_partition *fdisk_new_partition(void);
extern void fdisk_reset_partition(struct fdisk_partition *pa);
extern void fdisk_ref_partition(struct fdisk_partition *pa);
extern void fdisk_unref_partition(struct fdisk_partition *pa);
extern int fdisk_partition_is_freespace(struct fdisk_partition *pa);

extern int fdisk_partition_set_start(struct fdisk_partition *pa, uint64_t off);
extern uint64_t fdisk_partition_get_start(struct fdisk_partition *pa);
extern int fdisk_partition_cmp_start(struct fdisk_partition *a,
			      struct fdisk_partition *b);

extern int fdisk_partition_set_end(struct fdisk_partition *pa, uint64_t off);
extern uint64_t fdisk_partition_get_end(struct fdisk_partition *pa);
extern int fdisk_partition_set_size(struct fdisk_partition *pa, uint64_t size);
extern uint64_t fdisk_partition_get_size(struct fdisk_partition *pa);

extern int fdisk_partition_set_partno(struct fdisk_partition *pa, size_t n);
extern size_t fdisk_partition_get_partno(struct fdisk_partition *pa);
extern int fdisk_partition_cmp_partno(struct fdisk_partition *a,
			      struct fdisk_partition *b);

extern int fdisk_partition_set_type(struct fdisk_partition *pa, struct fdisk_parttype *type);
extern const struct fdisk_parttype *fdisk_partition_get_type(struct fdisk_partition *pa);
extern int fdisk_partition_set_name(struct fdisk_partition *pa, const char *name);
extern const char *fdisk_partition_get_name(struct fdisk_partition *pa);
extern int fdisk_partition_set_uuid(struct fdisk_partition *pa, const char *uuid);
extern const char *fdisk_partition_get_uuid(struct fdisk_partition *pa);
extern const char *fdisk_partition_get_attrs(struct fdisk_partition *pa);
extern int fdisk_partition_is_nested(struct fdisk_partition *pa);
extern int fdisk_partition_is_container(struct fdisk_partition *pa);
extern int fdisk_partition_get_parent(struct fdisk_partition *pa, size_t *parent);
extern int fdisk_partition_is_used(struct fdisk_partition *pa);
extern int fdisk_partition_to_string(struct fdisk_partition *pa,
				     struct fdisk_context *cxt,
				     int id, char **data);

extern int fdisk_partition_next_partno(struct fdisk_partition *pa,
				       struct fdisk_context *cxt,
				       size_t *n);

extern int fdisk_partition_partno_follow_default(struct fdisk_partition *pa, int enable);
extern int fdisk_partition_start_follow_default(struct fdisk_partition *pa, int enable);
extern int fdisk_partition_end_follow_default(struct fdisk_partition *pa, int enable);

extern int fdisk_dump_partition(struct fdisk_partition *pa, FILE *f);

extern int fdisk_reorder_partitions(struct fdisk_context *cxt);

/* table.c */
extern struct fdisk_table *fdisk_new_table(void);
extern int fdisk_reset_table(struct fdisk_table *tb);
extern void fdisk_ref_table(struct fdisk_table *tb);
extern void fdisk_unref_table(struct fdisk_table *tb);
extern int fdisk_dump_table(struct fdisk_table *b, FILE *f);
extern int fdisk_table_get_nents(struct fdisk_table *tb);
extern int fdisk_table_is_empty(struct fdisk_table *tb);
extern int fdisk_table_add_partition(struct fdisk_table *tb, struct fdisk_partition *pa);
extern int fdisk_table_remove_partition(struct fdisk_table *tb, struct fdisk_partition *pa);

extern int fdisk_get_partitions(struct fdisk_context *cxt, struct fdisk_table **tb);
extern int fdisk_get_freespaces(struct fdisk_context *cxt, struct fdisk_table **tb);

extern int fdisk_table_wrong_order(struct fdisk_table *tb);
extern int fdisk_table_sort_partitions(struct fdisk_table *tb,
			int (*cmp)(struct fdisk_partition *,
				   struct fdisk_partition *));

extern int fdisk_table_to_string(struct fdisk_table *tb,
			  struct fdisk_context *cxt,
			  int *cols, size_t ncols,  char **data);

extern int fdisk_table_next_partition(
			struct fdisk_table *tb,
			struct fdisk_iter *itr,
			struct fdisk_partition **pa);

extern struct fdisk_partition *fdisk_table_get_partition(
			struct fdisk_table *tb,
			size_t n);

/* alignment.c */
extern int fdisk_reset_alignment(struct fdisk_context *cxt);
extern int fdisk_reset_device_properties(struct fdisk_context *cxt);

extern int fdisk_save_user_geometry(struct fdisk_context *cxt,
			    unsigned int cylinders,
			    unsigned int heads,
			    unsigned int sectors);

extern int fdisk_save_user_sector_size(struct fdisk_context *cxt,
				unsigned int phy,
				unsigned int log);

extern int fdisk_has_user_device_properties(struct fdisk_context *cxt);

extern int fdisk_reread_partition_table(struct fdisk_context *cxt);

/* iter.c */
enum {

	FDISK_ITER_FORWARD = 0,
	FDISK_ITER_BACKWARD
};
extern struct fdisk_iter *fdisk_new_iter(int direction);
extern void fdisk_free_iter(struct fdisk_iter *itr);
extern void fdisk_reset_iter(struct fdisk_iter *itr, int direction);
extern int fdisk_iter_get_direction(struct fdisk_iter *itr);


/* dos.c */
extern int fdisk_dos_enable_compatible(struct fdisk_label *lb, int enable);
extern int fdisk_dos_is_compatible(struct fdisk_label *lb);

/* sun.h */
extern int fdisk_sun_set_alt_cyl(struct fdisk_context *cxt);
extern int fdisk_sun_set_xcyl(struct fdisk_context *cxt);
extern int fdisk_sun_set_ilfact(struct fdisk_context *cxt);
extern int fdisk_sun_set_rspeed(struct fdisk_context *cxt);
extern int fdisk_sun_set_pcylcount(struct fdisk_context *cxt);

/* bsd.c */
extern int fdisk_bsd_edit_disklabel(struct fdisk_context *cxt);
extern int fdisk_bsd_write_bootstrap(struct fdisk_context *cxt);
extern int fdisk_bsd_link_partition(struct fdisk_context *cxt);

/* sgi.h */
#define SGI_FLAG_BOOT	1
#define SGI_FLAG_SWAP	2
extern int fdisk_sgi_set_bootfile(struct fdisk_context *cxt);
extern int fdisk_sgi_create_info(struct fdisk_context *cxt);

/* gpt */

/* GPT partition attributes */
enum {
	/* System partition (disk partitioning utilities must preserve the
	 * partition as is) */
	GPT_FLAG_REQUIRED = 1,

	/* EFI firmware should ignore the content of the partition and not try
	 * to read from it */
	GPT_FLAG_NOBLOCK,

	/* Legacy BIOS bootable  */
	GPT_FLAG_LEGACYBOOT,

	/* bites 48-63, Defined and used by the individual partition type.
	 *
	 * The flag GPT_FLAG_GUIDSPECIFIC forces libfdisk to ask (by ask API)
	 * for a bit number. If you want to toggle specific bit and avoid any
	 * dialog, then use the bit number (in range 48..63). For example:
	 *
	 * // start dialog to ask for bit number
	 * fdisk_partition_toggle_flag(cxt, n, GPT_FLAG_GUIDSPECIFIC);
	 *
	 * // toggle bit 60
	 * fdisk_partition_toggle_flag(cxt, n, 60);
	 */
	GPT_FLAG_GUIDSPECIFIC
};

extern int fdisk_gpt_partition_set_uuid(struct fdisk_context *cxt, size_t i);
extern int fdisk_gpt_partition_set_name(struct fdisk_context *cxt, size_t i);
extern int fdisk_gpt_is_hybrid(struct fdisk_context *cxt);

/* dos.c */
extern struct dos_partition *fdisk_dos_get_partition(
				struct fdisk_context *cxt,
				size_t i);

extern int fdisk_dos_move_begin(struct fdisk_context *cxt, size_t i);

#define DOS_FLAG_ACTIVE	1

/* ask.c */
#define fdisk_is_ask(a, x) (fdisk_ask_get_type(a) == FDISK_ASKTYPE_ ## x)

extern struct fdisk_ask *fdisk_new_ask(void);
extern void fdisk_reset_ask(struct fdisk_ask *ask);
extern void fdisk_free_ask(struct fdisk_ask *ask);
extern const char *fdisk_ask_get_query(struct fdisk_ask *ask);
extern int fdisk_ask_set_query(struct fdisk_ask *ask, const char *str);
extern int fdisk_ask_get_type(struct fdisk_ask *ask);
extern int fdisk_ask_set_type(struct fdisk_ask *ask, int type);
extern int fdisk_ask_set_flags(struct fdisk_ask *ask, unsigned int flags);
extern unsigned int fdisk_ask_get_flags(struct fdisk_ask *ask);

extern int fdisk_do_ask(struct fdisk_context *cxt, struct fdisk_ask *ask);

extern const char *fdisk_ask_number_get_range(struct fdisk_ask *ask);
extern int fdisk_ask_number_set_range(struct fdisk_ask *ask, const char *range);
extern uint64_t fdisk_ask_number_get_default(struct fdisk_ask *ask);
extern int fdisk_ask_number_set_default(struct fdisk_ask *ask, uint64_t dflt);
extern uint64_t fdisk_ask_number_get_low(struct fdisk_ask *ask);
extern int fdisk_ask_number_set_low(struct fdisk_ask *ask, uint64_t low);
extern uint64_t fdisk_ask_number_get_high(struct fdisk_ask *ask);
extern int fdisk_ask_number_set_high(struct fdisk_ask *ask, uint64_t high);
extern uint64_t fdisk_ask_number_get_base(struct fdisk_ask *ask);
extern int fdisk_ask_number_set_base(struct fdisk_ask *ask, uint64_t base);
extern uint64_t fdisk_ask_number_get_unit(struct fdisk_ask *ask);
extern int fdisk_ask_number_set_unit(struct fdisk_ask *ask, uint64_t unit);
extern uint64_t fdisk_ask_number_get_result(struct fdisk_ask *ask);
extern int fdisk_ask_number_set_result(struct fdisk_ask *ask, uint64_t result);
extern int fdisk_ask_number_set_relative(struct fdisk_ask *ask, int relative);
extern int fdisk_ask_number_is_relative(struct fdisk_ask *ask);
extern int fdisk_ask_number_inchars(struct fdisk_ask *ask);

extern int fdisk_ask_number(struct fdisk_context *cxt,
		     uintmax_t low,
		     uintmax_t dflt,
		     uintmax_t high,
		     const char *query,
		     uintmax_t *result);

extern int fdisk_ask_string(struct fdisk_context *cxt,
		     const char *query,
		     char **result);

extern char *fdisk_ask_string_get_result(struct fdisk_ask *ask);
extern int fdisk_ask_string_set_result(struct fdisk_ask *ask, char *result);

extern int fdisk_ask_yesno(struct fdisk_context *cxt, const char *query, int *result);
extern uint64_t fdisk_ask_yesno_get_result(struct fdisk_ask *ask);
extern int fdisk_ask_yesno_set_result(struct fdisk_ask *ask, uint64_t result);

extern int fdisk_info(struct fdisk_context *cxt, const char *fmt, ...)
			__attribute__ ((__format__ (__printf__, 2, 3)));
extern int fdisk_colon(struct fdisk_context *cxt, const char *fmt, ...)
			__attribute__ ((__format__ (__printf__, 2, 3)));
extern int fdisk_sinfo(struct fdisk_context *cxt, unsigned int flags, const char *fmt, ...)
			__attribute__ ((__format__ (__printf__, 3, 4)));

extern int fdisk_warnx(struct fdisk_context *cxt, const char *fmt, ...)
			__attribute__ ((__format__ (__printf__, 2, 3)));
extern int fdisk_warn(struct fdisk_context *cxt, const char *fmt, ...)
			__attribute__ ((__format__ (__printf__, 2, 3)));

extern int fdisk_ask_print_get_errno(struct fdisk_ask *ask);
extern int fdisk_ask_print_set_errno(struct fdisk_ask *ask, int errnum);
extern const char *fdisk_ask_print_get_mesg(struct fdisk_ask *ask);
extern int fdisk_ask_print_set_mesg(struct fdisk_ask *ask, const char *mesg);


extern size_t fdisk_ask_menu_get_nitems(struct fdisk_ask *ask);
extern int fdisk_ask_menu_set_default(struct fdisk_ask *ask, int dfl);
extern int fdisk_ask_menu_get_default(struct fdisk_ask *ask);
extern int fdisk_ask_menu_set_result(struct fdisk_ask *ask, int key);
extern int fdisk_ask_menu_get_result(struct fdisk_ask *ask, int *key);
extern int fdisk_ask_menu_get_item(struct fdisk_ask *ask, size_t idx, int *key,
			    const char **name, const char **desc);
extern int fdisk_ask_menu_add_item(struct fdisk_ask *ask, int key,
			const char *name, const char *desc);


#ifdef __cplusplus
}
#endif

#endif /* _LIBFDISK_FDISK_H */
