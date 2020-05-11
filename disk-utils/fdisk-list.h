#ifndef UTIL_LINUX_FDISK_LIST_H
#define UTIL_LINUX_FDISK_LIST_H

extern void list_disklabel(struct fdisk_context *cxt);
extern void list_disk_identifier(struct fdisk_context *cxt);
extern void list_disk_geometry(struct fdisk_context *cxt);
extern void list_freespace(struct fdisk_context *cxt);

extern char *next_proc_partition(FILE **f);
extern int print_device_pt(struct fdisk_context *cxt, char *device, int warnme, int verify, int seperator);
extern int print_device_freespace(struct fdisk_context *cxt, char *device, int warnme, int seperator);

extern void print_all_devices_pt(struct fdisk_context *cxt, int verify);
extern void print_all_devices_freespace(struct fdisk_context *cxt);

extern void list_available_columns(FILE *out);
extern int *init_fields(struct fdisk_context *cxt, const char *str, size_t *n);


/* used by fdisk and sfdisk */
enum {
	WIPEMODE_AUTO = 0,
	WIPEMODE_NEVER = 1,
	WIPEMODE_ALWAYS = 2
};

static inline int wipemode_from_string(const char *str)
{
	size_t i;
	static const char *modes[] = {
		[WIPEMODE_AUTO]   = "auto",
		[WIPEMODE_NEVER]  = "never",
		[WIPEMODE_ALWAYS] = "always"
	};

	if (!str || !*str)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(modes); i++) {
		if (strcasecmp(str, modes[i]) == 0)
			return i;
	}

	return -EINVAL;
}

#endif /* UTIL_LINUX_FDISK_LIST_H */
