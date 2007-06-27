#ifndef MOUNT_FSPROBE_H
#define MOUNT_FSPROBE_H
/*
 * This is the generic interface for filesystem guessing libraries.
 * Implementations are provided by
 *
 *    fsprobe_blkid.c for libblkid from e2fsprogs
 *    fsprobe_volumeid.c for libvolume_id from udev
 *
 * Copyright (C) 2007 Kay Sievers <kay.sievers@vrfy.org>
 * Copyright (C) 2007 Matthias Koenig <mkoenig@suse.de>
 * Copyright (C) 2007 Karel Zak <kzak@redhat.com>
 */

extern void fsprobe_init(void);
extern void fsprobe_exit(void);

extern const char *fsprobe_get_devname_by_uuid(const char *uuid);
extern const char *fsprobe_get_devname_by_label(const char *label);

extern const char *fsprobe_get_label_by_devname(const char *devname);
extern const char *fsprobe_get_uuid_by_devname(const char *devname);
extern const char *fsprobe_get_fstype_by_devname(const char *devname);

extern const char *fsprobe_get_devname(const char *spec);
extern const char *fsprobe_get_devname_for_mounting(const char *spec);

extern int fsprobe_known_fstype(const char *fstype);

struct mountargs {
	const char *spec;
	const char *node;
	const char *type;
	int flags;
	void *data;
};

extern int fsprobe_known_fstype_in_procfs(const char *type);

extern int fsprobe_procfsloop_mount(
			int (*mount_fn)(struct mountargs *, int *, int *),
			struct mountargs *args,
			const char **types,
			int *special, int *status);

#endif /* MOUNT_FSPROBE_H */
