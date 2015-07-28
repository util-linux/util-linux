#ifndef UTIL_LINUX_FDISK_LIST_H
#define UTIL_LINUX_FDISK_LIST_H

extern void list_disklabel(struct fdisk_context *cxt);
extern void list_disk_geometry(struct fdisk_context *cxt);
extern void list_freespace(struct fdisk_context *cxt);

extern char *next_proc_partition(FILE **f);
extern int print_device_pt(struct fdisk_context *cxt, char *device, int warnme, int verify);
extern int print_device_freespace(struct fdisk_context *cxt, char *device, int warnme);

extern void print_all_devices_pt(struct fdisk_context *cxt, int verify);
extern void print_all_devices_freespace(struct fdisk_context *cxt);

extern void list_available_columns(FILE *out);
extern int *init_fields(struct fdisk_context *cxt, const char *str, size_t *n);

#endif /* UTIL_LINUX_FDISK_LIST_H */
