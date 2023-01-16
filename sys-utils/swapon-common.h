#ifndef UTIL_LINUX_SWAPON_COMMON_H
#define UTIL_LINUX_SWAPON_COMMON_H

#include <libmount.h>

extern struct libmnt_cache *mntcache;

extern struct libmnt_table *get_fstab(const char *filename);
extern struct libmnt_table *get_swaps(void);
extern void free_tables(void);

extern int match_swap(struct libmnt_fs *fs, void *data);
extern int is_active_swap(const char *filename);

extern int cannot_find(const char *special);

extern void add_label(const char *label);
extern const char *get_label(size_t i);
extern size_t numof_labels(void);

extern void add_uuid(const char *uuid);
extern const char *get_uuid(size_t i);
extern size_t numof_uuids(void);

#endif /* UTIL_LINUX_SWAPON_COMMON_H */
