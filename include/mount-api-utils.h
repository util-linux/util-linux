#ifndef UTIL_LINUX_MOUNT_API_UTILS
#define UTIL_LINUX_MOUNT_API_UTILS

#if defined(__linux__)
#include <sys/syscall.h>
#include <linux/mount.h>

/* Accepted by both open_tree() and mount_setattr(). */
#ifndef AT_RECURSIVE
# define AT_RECURSIVE 0x8000
#endif

#ifndef OPEN_TREE_CLONE
# define OPEN_TREE_CLONE 1
#endif

#ifndef OPEN_TREE_CLOEXEC
# define OPEN_TREE_CLOEXEC O_CLOEXEC
#endif

#if !defined(HAVE_OPEN_TREE) && defined(SYS_open_tree)
static inline int open_tree(int dfd, const char *filename, unsigned int flags)
{
	return syscall(SYS_open_tree, dfd, filename, flags);
}
#endif

#ifndef MOVE_MOUNT_F_SYMLINKS
# define MOVE_MOUNT_F_SYMLINKS 0x00000001 /* Follow symlinks on from path */
#endif

#ifndef MOVE_MOUNT_F_AUTOMOUNTS
# define MOVE_MOUNT_F_AUTOMOUNTS 0x00000002 /* Follow automounts on from path */
#endif

#ifndef MOVE_MOUNT_F_EMPTY_PATH
# define MOVE_MOUNT_F_EMPTY_PATH 0x00000004 /* Empty from path permitted */
#endif

#ifndef MOVE_MOUNT_T_SYMLINKS
# define MOVE_MOUNT_T_SYMLINKS 0x00000010 /* Follow symlinks on to path */
#endif

#ifndef MOVE_MOUNT_T_AUTOMOUNTS
# define MOVE_MOUNT_T_AUTOMOUNTS 0x00000020 /* Follow automounts on to path */
#endif

#ifndef MOVE_MOUNT_T_EMPTY_PATH
# define MOVE_MOUNT_T_EMPTY_PATH 0x00000040 /* Empty to path permitted */
#endif

#ifndef MOVE_MOUNT__MASK
# define MOVE_MOUNT__MASK 0x00000077
#endif

#if !defined(HAVE_MOVE_MOUNT) && defined(SYS_move_mount)
static inline int move_mount(int from_dfd, const char *from_pathname, int to_dfd,
			     const char *to_pathname, unsigned int flags)
{
	return syscall(SYS_move_mount, from_dfd, from_pathname, to_dfd,
		       to_pathname, flags);
}
#endif

#ifndef MOUNT_ATTR_RDONLY
# define MOUNT_ATTR_RDONLY 0x00000001
#endif

#ifndef MOUNT_ATTR_NOSUID
# define MOUNT_ATTR_NOSUID 0x00000002
#endif

#ifndef MOUNT_ATTR_NOEXEC
# define MOUNT_ATTR_NOEXEC 0x00000008
#endif

#ifndef MOUNT_ATTR_NODIRATIME
# define MOUNT_ATTR_NODIRATIME 0x00000080
#endif

#ifndef MOUNT_ATTR__ATIME
# define MOUNT_ATTR__ATIME 0x00000070
#endif

#ifndef MOUNT_ATTR_RELATIME
# define MOUNT_ATTR_RELATIME 0x00000000
#endif

#ifndef MOUNT_ATTR_NOATIME
# define MOUNT_ATTR_NOATIME 0x00000010
#endif

#ifndef MOUNT_ATTR_STRICTATIME
# define MOUNT_ATTR_STRICTATIME 0x00000020
#endif

#ifndef MOUNT_ATTR_IDMAP
# define MOUNT_ATTR_IDMAP 0x00100000
#endif

#ifndef HAVE_STRUCT_MOUNT_ATTR
# include <linux/types.h>
struct mount_attr {
	__u64 attr_set;
	__u64 attr_clr;
	__u64 propagation;
	__u64 userns_fd;
};
#endif

#if !defined(HAVE_MOUNT_SETATTR) && defined(SYS_mount_setattr)
static inline int mount_setattr(int dfd, const char *path, unsigned int flags,
				struct mount_attr *attr, size_t size)
{
	return syscall(SYS_mount_setattr, dfd, path, flags, attr, size);
}
#endif


/*
 * UL_HAVE_MOUNT_API is used by applications to check that all new mount API is
 * avalable.
 */
#if defined(SYS_open_tree) && \
    defined(SYS_mount_setattr) && \
    defined(SYS_move_mount)

# define UL_HAVE_MOUNT_API 1
#endif

#endif /* __linux__ */
#endif /* UTIL_LINUX_MOUNT_API_UTILS */

