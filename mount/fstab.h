#include "mntent.h"
int mtab_is_writable(void);
int mtab_does_not_exist(void);
int mtab_is_a_symlink(void);
int is_mounted_once(const char *name);

struct mntentchn {
	struct mntentchn *nxt, *prev;
	struct my_mntent m;
};

struct mntentchn *mtab_head (void);
struct mntentchn *getmntfile (const char *name);
struct mntentchn *getmntoptfile (const char *file);
struct mntentchn *getmntdirbackward (const char *dir, struct mntentchn *mc);
struct mntentchn *getmntdevbackward (const char *dev, struct mntentchn *mc);

struct mntentchn *fstab_head (void);
struct mntentchn *getfsfile (const char *file);
struct mntentchn *getfsspec (const char *spec);
struct mntentchn *getfsspecfile (const char *spec, const char *file);
struct mntentchn *getfsuuidspec (const char *uuid);
struct mntentchn *getfsvolspec (const char *label);

void lock_mtab (void);
void unlock_mtab (void);
void update_mtab (const char *special, struct my_mntent *with);
