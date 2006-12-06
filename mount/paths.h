#include <mntent.h>
#define _PATH_FSTAB	"/etc/fstab"
#ifdef _PATH_MOUNTED
#define MOUNTED_LOCK	_PATH_MOUNTED "~"
#define MOUNTED_TEMP	_PATH_MOUNTED ".tmp"
#else
#define MOUNTED_LOCK	"/etc/mtab~"
#define MOUNTED_TEMP	"/etc/mtab.tmp"
#endif
#define LOCK_TIMEOUT	10
