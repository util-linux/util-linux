#ifdef HAVE_LIBBLKID
#include <blkid/blkid.h>
extern blkid_cache blkid;
#endif

extern void mount_blkid_get_cache(void);
extern void mount_blkid_put_cache(void);
extern const char *mount_get_devname_by_uuid(const char *uuid);
extern const char *mount_get_devname_by_label(const char *label);
extern const char *mount_get_volume_label_by_spec(const char *spec);
extern const char *mount_get_devname(const char *spec);
extern const char *mount_get_devname_for_mounting(const char *spec);
extern int fsprobe_known_fstype(const char *fstype);
extern const char *fsprobe_get_fstype_by_devname(const char *devname);

struct mountargs {
	const char *spec;
	const char *node;
	const char *type;
	int flags;
	void *data;
};

extern int fsprobe_known_fstype_in_procfs(const char *type);

extern int fsprobe_procfsloop_mount(int (*mount_fn)(struct mountargs *),
			struct mountargs *args,
			const char **types);

