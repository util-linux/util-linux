#ifdef HAVE_BLKID
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
