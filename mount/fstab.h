#define _PATH_FSTAB	"/etc/fstab"
#define MOUNTED_LOCK	"/etc/mtab~"
#define MOUNTED_TEMP	"/etc/mtab.tmp"
#define LOCK_BUSY	10

int mtab_is_writable(void);
int mtab_does_not_exist(void);
int mtab_is_a_symlink(void);

struct mntentchn {
     struct mntentchn *nxt, *prev;
     char *mnt_fsname;
     char *mnt_dir;
     char *mnt_type;
     char *mnt_opts;
};

struct mntentchn *mtab_head (void);
struct mntentchn *getmntfile (const char *name);
struct mntentchn *getmntoptfile (const char *file);

struct mntentchn *fstab_head (void);
struct mntentchn *getfsfile (const char *file);
struct mntentchn *getfsspec (const char *spec);

#include <mntent.h>
void lock_mtab (void);
void unlock_mtab (void);
void update_mtab (const char *special, struct mntent *with);
