/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_MOUNT_API_UTILS
#define UTIL_LINUX_MOUNT_API_UTILS

#ifdef HAVE_LINUX_MOUNT_H
#include <sys/mount.h>
#include <linux/mount.h>
#include <sys/syscall.h>
#include <inttypes.h>

/*
 * File descritors based mount API
 */
#ifdef HAVE_MOUNTFD_API

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
# define MOVE_MOUNT_F_SYMLINKS   0x00000001 /* Follow symlinks on from path */
#endif

#ifndef MOVE_MOUNT_F_AUTOMOUNTS
# define MOVE_MOUNT_F_AUTOMOUNTS 0x00000002 /* Follow automounts on from path */
#endif

#ifndef MOVE_MOUNT_F_EMPTY_PATH
# define MOVE_MOUNT_F_EMPTY_PATH 0x00000004 /* Empty from path permitted */
#endif

#ifndef MOVE_MOUNT_T_SYMLINKS
# define MOVE_MOUNT_T_SYMLINKS   0x00000010 /* Follow symlinks on to path */
#endif

#ifndef MOVE_MOUNT_T_AUTOMOUNTS
# define MOVE_MOUNT_T_AUTOMOUNTS 0x00000020 /* Follow automounts on to path */
#endif

#ifndef MOVE_MOUNT_T_EMPTY_PATH
# define MOVE_MOUNT_T_EMPTY_PATH 0x00000040 /* Empty to path permitted */
#endif

#ifndef MOVE_MOUNT_SET_GROUP
# define MOVE_MOUNT_SET_GROUP    0x00000100 /* Set sharing group instead */
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

#ifndef MOUNT_ATTR_NODEV
# define MOUNT_ATTR_NODEV 0x00000004
#endif

#ifndef MOUNT_ATTR_NOEXEC
# define MOUNT_ATTR_NOEXEC 0x00000008
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

#ifndef MOUNT_ATTR_NODIRATIME
# define MOUNT_ATTR_NODIRATIME 0x00000080
#endif

#ifndef MOUNT_ATTR_IDMAP
# define MOUNT_ATTR_IDMAP 0x00100000
#endif

#ifndef MOUNT_ATTR_NOSYMFOLLOW
# define MOUNT_ATTR_NOSYMFOLLOW 0x00200000
#endif

#ifndef HAVE_STRUCT_MOUNT_ATTR
# ifndef MOUNT_ATTR_SIZE_VER0 /* For case mount.h comes from a place invisible for autotools/meson */
# include <inttypes.h>
struct mount_attr {
	uint64_t attr_set;
	uint64_t attr_clr;
	uint64_t propagation;
	uint64_t userns_fd;
};
# endif
#endif

#if !defined(HAVE_MOUNT_SETATTR) && defined(SYS_mount_setattr)
static inline int mount_setattr(int dfd, const char *path, unsigned int flags,
				struct mount_attr *attr, size_t size)
{
	return syscall(SYS_mount_setattr, dfd, path, flags, attr, size);
}
#endif

#ifndef HAVE_ENUM_FSCONFIG_COMMAND
# ifndef FSOPEN_CLOEXEC /* For case mount.h comes from a place invisible for autotools/meson */
enum fsconfig_command {
	FSCONFIG_SET_FLAG	= 0,	/* Set parameter, supplying no value */
	FSCONFIG_SET_STRING	= 1,	/* Set parameter, supplying a string value */
	FSCONFIG_SET_BINARY	= 2,	/* Set parameter, supplying a binary blob value */
	FSCONFIG_SET_PATH	= 3,	/* Set parameter, supplying an object by path */
	FSCONFIG_SET_PATH_EMPTY	= 4,	/* Set parameter, supplying an object by (empty) path */
	FSCONFIG_SET_FD		= 5,	/* Set parameter, supplying an object by fd */
	FSCONFIG_CMD_CREATE	= 6,	/* Invoke superblock creation */
	FSCONFIG_CMD_RECONFIGURE = 7,	/* Invoke superblock reconfiguration */
};
# endif
#endif

#if !defined(HAVE_FSCONFIG) && defined(SYS_fsconfig)
static inline int fsconfig(int fd, unsigned int cmd, const char *key,
                    const void *value, int aux)
{
        return syscall(SYS_fsconfig, fd, cmd, key, value, aux);
}
#endif

#ifndef FSOPEN_CLOEXEC
# define FSOPEN_CLOEXEC          0x00000001
#endif

#if !defined(HAVE_FSOPEN) && defined(SYS_fsopen)
static inline int fsopen(const char *fsname, unsigned int flags)
{
        return syscall(SYS_fsopen, fsname, flags);
}
#endif

#ifndef FSMOUNT_CLOEXEC
# define FSMOUNT_CLOEXEC         0x00000001
#endif

#if !defined(HAVE_FSMOUNT) && defined(SYS_fsmount)
static inline int fsmount(int fd, unsigned int flags, unsigned int mount_attrs)
{
        return syscall(SYS_fsmount, fd, flags, mount_attrs);
}
#endif

#ifndef FSPICK_CLOEXEC
# define FSPICK_CLOEXEC          0x00000001
#endif

#ifndef FSPICK_SYMLINK_NOFOLLOW
# define FSPICK_SYMLINK_NOFOLLOW 0x00000002
#endif

#ifndef FSPICK_NO_AUTOMOUNT
# define FSPICK_NO_AUTOMOUNT     0x00000004
#endif

#ifdef FSPICK_EMPTY_PATH
# define FSPICK_EMPTY_PATH       0x00000008
#endif

#if !defined(HAVE_FSPICK) && defined(SYS_fspick)
static inline int fspick(int dfd, const char *pathname, unsigned int flags)
{
        return syscall(SYS_fspick, dfd, pathname, flags);
}
#endif

#endif /* HAVE_MOUNTFD_API */

/*
 * statmount() and listmount()
 */
#ifdef HAVE_STATMOUNT_API

#ifndef MNT_ID_REQ_SIZE_VER0
# define MNT_ID_REQ_SIZE_VER0    24 /* sizeof first published struct */
#endif
#ifndef MNT_ID_REQ_SIZE_VER1
# define MNT_ID_REQ_SIZE_VER1    32 /* sizeof second published struct */
#endif

/*
 * The structs mnt_id_req and statmount may differ between kernel versions, so
 * we must ensure that the structs contain everything we need. For now (during
 * development), it seems best to define local copies of the structs to avoid
 * relying on installed kernel headers and to avoid a storm of #ifdefs.
 */

/*
 * listmount() and statmount() request
 */
struct ul_mnt_id_req {
	uint32_t size;
	uint32_t spare;
	uint64_t mnt_id;
	uint64_t param;
	uint64_t mnt_ns_id;
};

/*
 * Please note that due to the variable length of the statmount buffer, the
 * struct cannot be versioned by size (like struct mnt_id_req).
 */
struct ul_statmount {
	uint32_t size;            /* Total size, including strings */
	uint32_t mnt_opts;        /* [str] Mount options of the mount */
	uint64_t mask;            /* What results were written */
	uint32_t sb_dev_major;    /* Device ID */
	uint32_t sb_dev_minor;
	uint64_t sb_magic;        /* ..._SUPER_MAGIC */
	uint32_t sb_flags;        /* SB_{RDONLY,SYNCHRONOUS,DIRSYNC,LAZYTIME} */
	uint32_t fs_type;         /* [str] Filesystem type */
	uint64_t mnt_id;          /* Unique ID of mount */
	uint64_t mnt_parent_id;   /* Unique ID of parent (for root == mnt_id) */
	uint32_t mnt_id_old;      /* Reused IDs used in proc/.../mountinfo */
	uint32_t mnt_parent_id_old;
	uint64_t mnt_attr;        /* MOUNT_ATTR_... */
	uint64_t mnt_propagation; /* MS_{SHARED,SLAVE,PRIVATE,UNBINDABLE} */
	uint64_t mnt_peer_group;  /* ID of shared peer group */
	uint64_t mnt_master;      /* Mount receives propagation from this ID */
	uint64_t propagate_from;  /* Propagation from in current namespace */
	uint32_t mnt_root;        /* [str] Root of mount relative to root of fs */
	uint32_t mnt_point;       /* [str] Mountpoint relative to current root */
	uint64_t mnt_ns_id;       /* ID of the mount namespace */
	uint32_t fs_subtype;      /* [str] Subtype of fs_type (if any) */
	uint32_t sb_source;       /* [str] Source string of the mount */
	uint32_t opt_num;         /* Number of fs options */
	uint32_t opt_array;       /* [str] Array of nul terminated fs options */
	uint32_t opt_sec_num;     /* Number of security options */
	uint32_t opt_sec_array;   /* [str] Array of nul terminated security options */
	uint64_t __spare2[46];
	char str[];               /* Variable size part containing strings */
};

/* sb_flags (defined in kernel include/linux/fs.h) */
#ifndef SB_RDONLY
# define SB_RDONLY       BIT(0)   /* Mount read-only */
# define SB_NOSUID       BIT(1)   /* Ignore suid and sgid bits */
# define SB_NODEV        BIT(2)   /* Disallow access to device special files */
# define SB_NOEXEC       BIT(3)   /* Disallow program execution */
# define SB_SYNCHRONOUS  BIT(4)   /* Writes are synced at once */
# define SB_MANDLOCK     BIT(6)   /* Allow mandatory locks on an FS */
# define SB_DIRSYNC      BIT(7)   /* Directory modifications are synchronous */
# define SB_NOATIME      BIT(10)  /* Do not update access times. */
# define SB_NODIRATIME   BIT(11)  /* Do not update directory access times */
# define SB_SILENT       BIT(15)
# define SB_POSIXACL     BIT(16)  /* Supports POSIX ACLs */
# define SB_INLINECRYPT  BIT(17)  /* Use blk-crypto for encrypted files */
# define SB_KERNMOUNT    BIT(22)  /* this is a kern_mount call */
# define SB_I_VERSION    BIT(23)  /* Update inode I_version field */
# define SB_LAZYTIME     BIT(25)  /* Update the on-disk [acm]times lazily */
#endif

/*
 * @mask bits for statmount(2)
 */
#ifndef STATMOUNT_SB_BASIC
# define STATMOUNT_SB_BASIC            0x00000001U   /* Want/got sb_... */
#endif
#ifndef STATMOUNT_MNT_BASIC
# define STATMOUNT_MNT_BASIC           0x00000002U   /* Want/got mnt_... */
#endif
#ifndef STATMOUNT_PROPAGATE_FROM
# define STATMOUNT_PROPAGATE_FROM      0x00000004U   /* Want/got propagate_from */
#endif
#ifndef STATMOUNT_MNT_ROOT
# define STATMOUNT_MNT_ROOT            0x00000008U   /* Want/got mnt_root  */
#endif
#ifndef STATMOUNT_MNT_POINT
# define STATMOUNT_MNT_POINT           0x00000010U   /* Want/got mnt_point */
#endif
#ifndef STATMOUNT_FS_TYPE
# define STATMOUNT_FS_TYPE             0x00000020U   /* Want/got fs_type */
#endif
#ifndef STATMOUNT_MNT_NS_ID
# define STATMOUNT_MNT_NS_ID           0x00000040U   /* Want/got mnt_ns_id */
#endif
#ifndef STATMOUNT_MNT_OPTS
# define STATMOUNT_MNT_OPTS            0x00000080U   /* Want/got mnt_opts */
#endif
#ifndef STATMOUNT_FS_SUBTYPE
# define STATMOUNT_FS_SUBTYPE          0x00000100U   /* Want/got fs_subtype */
#endif
#ifndef STATMOUNT_SB_SOURCE
# define STATMOUNT_SB_SOURCE           0x00000200U   /* Want/got sb_source */
#endif
#ifndef STATMOUNT_OPT_ARRAY
# define STATMOUNT_OPT_ARRAY           0x00000400U   /* Want/got opt_... */
#endif
#ifndef STATMOUNT_OPT_SEC_ARRAY
# define STATMOUNT_OPT_SEC_ARRAY       0x00000800U   /* Want/got opt_sec... */
#endif


/*
 * Special @mnt_id values that can be passed to listmount
 */
#ifdef LSMT_ROOT
# define LSMT_ROOT              0xffffffffffffffff    /* root mount */
#endif

#ifndef LISTMOUNT_REVERSE
# define LISTMOUNT_REVERSE      BIT(0)               /* List later mounts first */
#endif

#if defined(SYS_statmount)
static inline int ul_statmount(uint64_t mnt_id,
			uint64_t ns_id,
			uint64_t mask,
			struct ul_statmount *buf,
			size_t bufsize, unsigned int flags)
{
       struct ul_mnt_id_req req = {
		.size = MNT_ID_REQ_SIZE_VER1,
		.mnt_id = mnt_id,
		.param = mask,
		.mnt_ns_id = ns_id
       };

       return syscall(SYS_statmount, &req, buf, bufsize, flags);
}
#endif

#if defined(SYS_listmount)
static inline ssize_t ul_listmount(uint64_t mnt_id,
			uint64_t ns_id,
			uint64_t last_mnt_id,
			uint64_t list[], size_t num,
			unsigned int flags)
{
       struct ul_mnt_id_req req = {
		.size = MNT_ID_REQ_SIZE_VER1,
		.mnt_id = mnt_id,
		.param = last_mnt_id,
		.mnt_ns_id = ns_id
       };

       return syscall(SYS_listmount, &req, list, num, flags);
}
#endif

/* This is a version of statmount() that reallocates @buf to be large enough to
 * store data for the requested @id. This function never deallocates; it is the
 * caller's responsibility.
 */
static inline int sys_statmount(uint64_t id,
			uint64_t ns_id,
			uint64_t mask,
			struct ul_statmount **buf,
			size_t *bufsiz,
			unsigned int flags)
{
	size_t sz;
	int rc = 0;

	if (!buf || !bufsiz)
		return -EINVAL;

	sz = *bufsiz;
	if (!sz)
		sz = 32 * 1024;

	do {
		if (sz > *bufsiz) {
			struct ul_statmount *tmp = realloc(*buf, sz);
			if (!tmp)
				return -ENOMEM;
			*buf = tmp;
			*bufsiz = sz;
		}

		errno = 0;
		rc = ul_statmount(id, ns_id, mask, *buf, *bufsiz, flags);
		if (!rc)
			break;
		if (errno != EOVERFLOW)
			break;
		if (sz >= SIZE_MAX / 2)
			break;
		sz <<= 1;
	} while (rc);

	return rc;
}

#endif /* HAVE_STATMOUNT_API */

#endif /* HAVE_LINUX_MOUNT_H */

#endif /* UTIL_LINUX_MOUNT_API_UTILS */
