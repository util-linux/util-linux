#ifndef MOUNT_PATHS_H
#define MOUNT_PATHS_H

#include <mntent.h>

#define _PATH_FSTAB	"/etc/fstab"
#define PROC_SWAPS      "/proc/swaps"

#ifdef _PATH_MOUNTED
# define MOUNTED_LOCK	_PATH_MOUNTED "~"
# define MOUNTED_TEMP	_PATH_MOUNTED ".tmp"
#else
# define MOUNTED_LOCK	"/etc/mtab~"
# define MOUNTED_TEMP	"/etc/mtab.tmp"
#endif

#define LOCK_TIMEOUT	10

#define ETC_FILESYSTEMS		"/etc/filesystems"
#define PROC_FILESYSTEMS	"/proc/filesystems"

/* udev paths */
#define PATH_DEV_BYLABEL	"/dev/disk/by-label"
#define PATH_DEV_BYUUID		"/dev/disk/by-uuid"

#endif /* MOUNT_PATHS_H */
