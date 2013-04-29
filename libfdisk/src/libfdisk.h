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
struct fdisk_ask;
struct tt;

/*
 * Supported partition table types (labels)
 */
enum fdisk_labeltype {
	FDISK_DISKLABEL_DOS = 1,
	FDISK_DISKLABEL_SUN = 2,
	FDISK_DISKLABEL_SGI = 4,
	FDISK_DISKLABEL_OSF = 8,
	FDISK_DISKLABEL_MAC = 16,
	FDISK_DISKLABEL_GPT = 32,
	FDISK_DISKLABEL_ANY = -1
};

enum {
	FDISK_PARTSTAT_NONE = 0,
	FDISK_PARTSTAT_USED	/* partition used */
};

enum {
	FDISK_ASKTYPE_NONE = 0,
	FDISK_ASKTYPE_NUMBER,
	FDISK_ASKTYPE_OFFSET,
	FDISK_ASKTYPE_WARN,
	FDISK_ASKTYPE_WARNX,
	FDISK_ASKTYPE_INFO,
	FDISK_ASKTYPE_YESNO,
	FDISK_ASKTYPE_TABLE
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

extern int fdisk_context_assign_device(struct fdisk_context *cxt,
				const char *fname, int readonly);

extern struct fdisk_label *fdisk_context_get_label(struct fdisk_context *cxt,
				const char *name);

extern int fdisk_context_switch_label(struct fdisk_context *cxt,
				const char *name);

extern int fdisk_context_set_unit(struct fdisk_context *cxt, const char *str);
extern const char *fdisk_context_get_unit(struct fdisk_context *cxt, int n);
extern int fdisk_context_use_cylinders(struct fdisk_context *cxt);
extern unsigned int fdisk_context_get_units_per_sector(struct fdisk_context *cxt);

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

extern int fdisk_add_partition(struct fdisk_context *cxt, struct fdisk_parttype *t);
extern int fdisk_delete_partition(struct fdisk_context *cxt, size_t partnum);

extern struct fdisk_parttype *fdisk_get_partition_type(struct fdisk_context *cxt, size_t partnum);
extern int fdisk_set_partition_type(struct fdisk_context *cxt, size_t partnum,
			     struct fdisk_parttype *t);

extern void fdisk_label_set_changed(struct fdisk_label *lb, int changed);
extern int fdisk_label_is_changed(struct fdisk_label *lb);

extern int fdisk_partition_get_status(struct fdisk_context *cxt, size_t partnum, int *status);
extern int fdisk_partition_is_used(struct fdisk_context *cxt, size_t partnum);
extern int fdisk_partition_toggle_flag(struct fdisk_context *cxt, size_t partnum, unsigned long flag);

/* alignment.c */
extern int fdisk_reset_alignment(struct fdisk_context *cxt);


/* dos.c */
extern int fdisk_dos_enable_compatible(struct fdisk_label *lb, int enable);
extern int fdisk_dos_is_compatible(struct fdisk_label *lb);

/* ask.c */
#define fdisk_is_ask(a, x) (fdisk_ask_get_type(a) == FDISK_ASKTYPE_ ## x)

extern struct fdisk_ask *fdisk_new_ask(void);
extern void fdisk_reset_ask(struct fdisk_ask *ask);
extern void fdisk_free_ask(struct fdisk_ask *ask);
extern const char *fdisk_ask_get_query(struct fdisk_ask *ask);
extern int fdisk_ask_set_query(struct fdisk_ask *ask, const char *str);
extern int fdisk_ask_get_type(struct fdisk_ask *ask);
extern int fdisk_ask_set_type(struct fdisk_ask *ask, int type);
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


extern int fdisk_ask_number(struct fdisk_context *cxt,
		     uintmax_t low,
		     uintmax_t dflt,
		     uintmax_t high,
		     const char *query,
		     uintmax_t *result);

extern int fdisk_ask_yesno(struct fdisk_context *cxt, const char *query, int *result);
extern uint64_t fdisk_ask_yesno_get_result(struct fdisk_ask *ask);
extern int fdisk_ask_yesno_set_result(struct fdisk_ask *ask, uint64_t result);

extern int fdisk_info(struct fdisk_context *cxt, const char *fmt, ...);
extern int fdisk_warnx(struct fdisk_context *cxt, const char *fmt, ...);
extern int fdisk_warn(struct fdisk_context *cxt, const char *fmt, ...);

extern int fdisk_ask_print_get_errno(struct fdisk_ask *ask);
extern int fdisk_ask_print_set_errno(struct fdisk_ask *ask, int errnum);
extern const char *fdisk_ask_print_get_mesg(struct fdisk_ask *ask);
extern int fdisk_ask_print_set_mesg(struct fdisk_ask *ask, const char *mesg);

#ifdef __cplusplus
}
#endif

#endif /* _LIBFDISK_FDISK_H */
