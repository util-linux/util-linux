/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * mountP.h - private library header file
 *
 * This file is part of libmount from util-linux project.
 *
 * Copyright (C) 2008-2018 Karel Zak <kzak@redhat.com>
 *
 * libmount is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 */
#ifndef _LIBMOUNT_PRIVATE_H
#define _LIBMOUNT_PRIVATE_H

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>

#include "c.h"
#include "list.h"
#include "debug.h"
#include "libmount.h"

/*
 * Debug
 */
#define MNT_DEBUG_HELP		(1 << 0)
#define MNT_DEBUG_INIT		(1 << 1)
#define MNT_DEBUG_CACHE		(1 << 2)
#define MNT_DEBUG_OPTIONS	(1 << 3)
#define MNT_DEBUG_LOCKS		(1 << 4)
#define MNT_DEBUG_TAB		(1 << 5)
#define MNT_DEBUG_FS		(1 << 6)
#define MNT_DEBUG_UPDATE	(1 << 7)
#define MNT_DEBUG_UTILS		(1 << 8)
#define MNT_DEBUG_CXT		(1 << 9)
#define MNT_DEBUG_DIFF		(1 << 10)
#define MNT_DEBUG_MONITOR	(1 << 11)
#define MNT_DEBUG_BTRFS		(1 << 12)
#define MNT_DEBUG_LOOP		(1 << 13)
#define MNT_DEBUG_VERITY	(1 << 14)

#define MNT_DEBUG_ALL		0xFFFF

UL_DEBUG_DECLARE_MASK(libmount);
#define DBG(m, x)	__UL_DBG(libmount, MNT_DEBUG_, m, x)
#define ON_DBG(m, x)	__UL_DBG_CALL(libmount, MNT_DEBUG_, m, x)
#define DBG_FLUSH	__UL_DBG_FLUSH(libmount, MNT_DEBUG_)

#define UL_DEBUG_CURRENT_MASK	UL_DEBUG_MASK(libmount)
#include "debugobj.h"

/*
 * NLS -- the library has to be independent on main program, so define
 * UL_TEXTDOMAIN_EXPLICIT before you include nls.h.
 *
 * Now we use util-linux.po (=PACKAGE), rather than maintain the texts
 * in the separate libmount.po file.
 */
#define LIBMOUNT_TEXTDOMAIN	PACKAGE
#define UL_TEXTDOMAIN_EXPLICIT	LIBMOUNT_TEXTDOMAIN
#include "nls.h"


/* extension for files in the directory */
#define MNT_MNTTABDIR_EXT	".fstab"

/* library private paths */
#define MNT_RUNTIME_TOPDIR	"/run"
#define MNT_RUNTIME_TOPDIR_OLD	"/dev"

#define MNT_PATH_UTAB		MNT_RUNTIME_TOPDIR "/mount/utab"
#define MNT_PATH_UTAB_OLD	MNT_RUNTIME_TOPDIR_OLD "/.mount/utab"

#define MNT_UTAB_HEADER	"# libmount utab file\n"

#ifdef TEST_PROGRAM
struct libmnt_test {
	const char	*name;
	int		(*body)(struct libmnt_test *ts, int argc, char *argv[]);
	const char	*usage;
};

/* test.c */
extern int mnt_run_test(struct libmnt_test *tests, int argc, char *argv[]);
#endif

/* utils.c */
extern int mnt_valid_tagname(const char *tagname);
extern int append_string(char **a, const char *b);

extern const char *mnt_statfs_get_fstype(struct statfs *vfs);
extern int is_procfs_fd(int fd);
extern int is_file_empty(const char *name);

extern int mnt_is_readonly(const char *path)
			__attribute__((nonnull));

extern int mnt_parse_offset(const char *str, size_t len, uintmax_t *res);

extern int mnt_chdir_to_parent(const char *target, char **filename);

extern char *mnt_get_username(const uid_t uid);
extern int mnt_get_uid(const char *username, uid_t *uid);
extern int mnt_get_gid(const char *groupname, gid_t *gid);
extern int mnt_in_group(gid_t gid);

extern int mnt_open_uniq_filename(const char *filename, char **name);

extern int mnt_has_regular_utab(const char **utab, int *writable);
extern const char *mnt_get_utab_path(void);

extern int mnt_get_filesystems(char ***filesystems, const char *pattern);
extern void mnt_free_filesystems(char **filesystems);

extern char *mnt_get_kernel_cmdline_option(const char *name);
extern int mnt_stat_mountpoint(const char *target, struct stat *st);
extern int mnt_lstat_mountpoint(const char *target, struct stat *st);
extern FILE *mnt_get_procfs_memstream(int fd, char **membuf);

/* tab.c */
extern int is_mountinfo(struct libmnt_table *tb);
extern int mnt_table_set_parser_fltrcb(	struct libmnt_table *tb,
					int (*cb)(struct libmnt_fs *, void *),
					void *data);

extern int __mnt_table_parse_mtab(struct libmnt_table *tb,
					const char *filename,
					struct libmnt_table *u_tb);

extern struct libmnt_fs *mnt_table_get_fs_root(struct libmnt_table *tb,
					struct libmnt_fs *fs,
					unsigned long mountflags,
					char **fsroot);

extern int __mnt_table_is_fs_mounted(	struct libmnt_table *tb,
					struct libmnt_fs *fstab_fs,
					const char *tgt_prefix);

/*
 * Generic iterator
 */
struct libmnt_iter {
        struct list_head        *p;		/* current position */
        struct list_head        *head;		/* start position */
	int			direction;	/* MNT_ITER_{FOR,BACK}WARD */
};

#define IS_ITER_FORWARD(_i)	((_i)->direction == MNT_ITER_FORWARD)
#define IS_ITER_BACKWARD(_i)	((_i)->direction == MNT_ITER_BACKWARD)

#define MNT_ITER_INIT(itr, list) \
	do { \
		(itr)->p = IS_ITER_FORWARD(itr) ? \
				(list)->next : (list)->prev; \
		(itr)->head = (list); \
	} while(0)

#define MNT_ITER_ITERATE(itr, res, restype, member) \
	do { \
		res = list_entry((itr)->p, restype, member); \
		(itr)->p = IS_ITER_FORWARD(itr) ? \
				(itr)->p->next : (itr)->p->prev; \
	} while(0)


/*
 * This struct represents one entry in a mtab/fstab/mountinfo file.
 * (note that fstab[1] means the first column from fstab, and so on...)
 */
struct libmnt_fs {
	struct list_head ents;
	struct libmnt_table *tab;

	int		refcount;	/* reference counter */
	int		id;		/* mountinfo[1]: ID */
	int		parent;		/* mountinfo[2]: parent */
	dev_t		devno;		/* mountinfo[3]: st_dev */

	char		*bindsrc;	/* utab, full path from fstab[1] for bind mounts */

	char		*source;	/* fstab[1], mountinfo[10], swaps[1]:
                                         * source dev, file, dir or TAG */
	char		*tagname;	/* fstab[1]: tag name - "LABEL", "UUID", ..*/
	char		*tagval;	/*           tag value */

	char		*root;		/* mountinfo[4]: root of the mount within the FS */
	char		*target;	/* mountinfo[5], fstab[2]: mountpoint */
	char		*fstype;	/* mountinfo[9], fstab[3]: filesystem type */

	char		*optstr;	/* fstab[4], merged options */
	char		*vfs_optstr;	/* mountinfo[6]: fs-independent (VFS) options */
	char		*opt_fields;	/* mountinfo[7]: optional fields */
	char		*fs_optstr;	/* mountinfo[11]: fs-dependent options */
	char		*user_optstr;	/* userspace mount options */
	char		*attrs;		/* mount attributes */

	int		freq;		/* fstab[5]: dump frequency in days */
	int		passno;		/* fstab[6]: pass number on parallel fsck */

	/* /proc/swaps */
	char		*swaptype;	/* swaps[2]: device type (partition, file, ...) */
	off_t		size;		/* swaps[3]: swaparea size */
	off_t		usedsize;	/* swaps[4]: used size */
	int		priority;	/* swaps[5]: swap priority */

	int		flags;		/* MNT_FS_* flags */
	pid_t		tid;		/* /proc/<tid>/mountinfo otherwise zero */

	char		*comment;	/* fstab comment */

	void		*userdata;	/* library independent data */
};

/*
 * fs flags
 */
#define MNT_FS_PSEUDO	(1 << 1) /* pseudo filesystem */
#define MNT_FS_NET	(1 << 2) /* network filesystem */
#define MNT_FS_SWAP	(1 << 3) /* swap device */
#define MNT_FS_KERNEL	(1 << 4) /* data from /proc/{mounts,self/mountinfo} */
#define MNT_FS_MERGED	(1 << 5) /* already merged data from /run/mount/utab */

#define mnt_fs_is_regular(_f)	(!(mnt_fs_is_pseudofs(_f) \
				   || mnt_fs_is_netfs(_f) \
				   || mnt_fs_is_swaparea(_f)))

/*
 * mtab/fstab/mountinfo file
 */
struct libmnt_table {
	int		fmt;		/* MNT_FMT_* file format */
	int		nents;		/* number of entries */
	int		refcount;	/* reference counter */
	int		comms;		/* enable/disable comment parsing */
	char		*comm_intro;	/* First comment in file */
	char		*comm_tail;	/* Last comment in file */

	struct libmnt_cache *cache;		/* canonicalized paths/tags cache */

        int		(*errcb)(struct libmnt_table *tb,
				 const char *filename, int line);

	int		(*fltrcb)(struct libmnt_fs *fs, void *data);
	void		*fltrcb_data;


	struct list_head	ents;	/* list of entries (libmnt_fs) */
	void		*userdata;
};

extern struct libmnt_table *__mnt_new_table_from_file(const char *filename, int fmt, int empty_for_enoent);

/*
 * Tab file format
 */
enum {
	MNT_FMT_GUESS,
	MNT_FMT_FSTAB,			/* /etc/{fs,m}tab */
	MNT_FMT_MTAB = MNT_FMT_FSTAB,	/* alias */
	MNT_FMT_MOUNTINFO,		/* /proc/#/mountinfo */
	MNT_FMT_UTAB,			/* /run/mount/utab */
	MNT_FMT_SWAPS			/* /proc/swaps */
};

/*
 * Additional mounts
 */
struct libmnt_addmount {
	unsigned long mountflags;

	struct list_head	mounts;
};

struct libmnt_ns {
	int fd;				/* file descriptor of namespace, -1 when inactive */
	struct libmnt_cache *cache;	/* paths cache associated with NS */
};

/*
 * Mount context -- high-level API
 */
struct libmnt_context
{
	int	action;		/* MNT_ACT_{MOUNT,UMOUNT} */
	int	restricted;	/* root or not? */

	char	*fstype_pattern;	/* for mnt_match_fstype() */
	char	*optstr_pattern;	/* for mnt_match_options() */

	struct libmnt_fs *fs;		/* filesystem description (type, mountpoint, device, ...) */
	struct libmnt_fs *fs_template;	/* used for @fs on mnt_reset_context() */

	struct libmnt_table *fstab;	/* fstab (or mtab for some remounts) entries */
	struct libmnt_table *mtab;	/* mtab entries */
	struct libmnt_table *utab;	/* rarely used by umount only */

	int	(*table_errcb)(struct libmnt_table *tb,	/* callback for libmnt_table structs */
			 const char *filename, int line);

	int	(*table_fltrcb)(struct libmnt_fs *fs, void *data);	/* callback for libmnt_table structs */
	void	*table_fltrcb_data;

	char	*(*pwd_get_cb)(struct libmnt_context *);		/* get encryption password */
	void	(*pwd_release_cb)(struct libmnt_context *, char *);	/* release password */

	int	optsmode;	/* fstab optstr mode MNT_OPTSMODE_{AUTO,FORCE,IGNORE} */
	int	loopdev_fd;	/* open loopdev */

	unsigned long	mountflags;	/* final mount(2) flags */
	const void	*mountdata;	/* final mount(2) data, string or binary data */

	unsigned long	user_mountflags;	/* MNT_MS_* (loop=, user=, ...) */

	struct list_head	addmounts;	/* additional mounts */

	struct libmnt_cache	*cache;	/* paths cache */
	struct libmnt_lock	*lock;	/* mtab lock */
	struct libmnt_update	*update;/* mtab/utab update */

	const char	*mtab_path; /* path to mtab */
	int		mtab_writable; /* is mtab writable */

	const char	*utab_path; /* path to utab */
	int		utab_writable; /* is utab writable */

	char		*tgt_prefix;	/* path used for all targets */

	int	flags;		/* private context flags */

	char	*helper;	/* name of the used /sbin/[u]mount.<type> helper */
	int	helper_status;	/* helper wait(2) status */
	int	helper_exec_status; /* 1: not called yet, 0: success, <0: -errno */

	char	*orig_user;	/* original (non-fixed) user= option */

	pid_t	*children;	/* "mount -a --fork" PIDs */
	int	nchildren;	/* number of children */
	pid_t	pid;		/* 0=parent; PID=child */


	int	syscall_status;	/* 1: not called yet, 0: success, <0: -errno */

	struct libmnt_ns	ns_orig;	/* original namespace */
	struct libmnt_ns	ns_tgt;		/* target namespace */
	struct libmnt_ns	*ns_cur;	/* pointer to current namespace */

	unsigned int	enabled_textdomain : 1;		/* bindtextdomain() called */
};

/* flags */
#define MNT_FL_NOMTAB		(1 << 1)
#define MNT_FL_FAKE		(1 << 2)
#define MNT_FL_SLOPPY		(1 << 3)
#define MNT_FL_VERBOSE		(1 << 4)
#define MNT_FL_NOHELPERS	(1 << 5)
#define MNT_FL_LOOPDEL		(1 << 6)
#define MNT_FL_LAZY		(1 << 7)
#define MNT_FL_FORCE		(1 << 8)
#define MNT_FL_NOCANONICALIZE	(1 << 9)
#define MNT_FL_RDONLY_UMOUNT	(1 << 11)	/* remount,ro after EBUSY umount(2) */
#define MNT_FL_FORK		(1 << 12)
#define MNT_FL_NOSWAPMATCH	(1 << 13)
#define MNT_FL_RWONLY_MOUNT	(1 << 14)	/* explicit mount -w; never try read-only  */

#define MNT_FL_MOUNTDATA	(1 << 20)
#define MNT_FL_TAB_APPLIED	(1 << 21)	/* mtab/fstab merged to cxt->fs */
#define MNT_FL_MOUNTFLAGS_MERGED (1 << 22)	/* MS_* flags was read from optstr */
#define MNT_FL_SAVED_USER	(1 << 23)
#define MNT_FL_PREPARED		(1 << 24)
#define MNT_FL_HELPER		(1 << 25)	/* [u]mount.<type> */
#define MNT_FL_LOOPDEV_READY	(1 << 26)	/* /dev/loop<N> initialized by the library */
#define MNT_FL_MOUNTOPTS_FIXED  (1 << 27)
#define MNT_FL_TABPATHS_CHECKED	(1 << 28)
#define MNT_FL_FORCED_RDONLY	(1 << 29)	/* mounted read-only on write-protected device */
#define MNT_FL_VERITYDEV_READY	(1 << 30)	/* /dev/mapper/<FOO> initialized by the library */

/* default flags */
#define MNT_FL_DEFAULT		0

/* Flags usable with MS_BIND|MS_REMOUNT */
#define MNT_BIND_SETTABLE	(MS_NOSUID|MS_NODEV|MS_NOEXEC|MS_NOATIME|MS_NODIRATIME|MS_RELATIME|MS_RDONLY)

/* lock.c */
extern int mnt_lock_use_simplelock(struct libmnt_lock *ml, int enable);

/* optmap.c */
extern const struct libmnt_optmap *mnt_optmap_get_entry(
			     struct libmnt_optmap const **maps,
                             int nmaps,
			     const char *name,
                             size_t namelen,
			     const struct libmnt_optmap **mapent);

/* optstr.c */
extern int mnt_optstr_remove_option_at(char **optstr, char *begin, char *end);
extern int mnt_optstr_fix_gid(char **optstr, char *value, size_t valsz, char **next);
extern int mnt_optstr_fix_uid(char **optstr, char *value, size_t valsz, char **next);
extern int mnt_optstr_fix_secontext(char **optstr, char *value, size_t valsz, char **next);
extern int mnt_optstr_fix_user(char **optstr);

/* fs.c */
extern struct libmnt_fs *mnt_copy_mtab_fs(const struct libmnt_fs *fs)
			__attribute__((nonnull));
extern int __mnt_fs_set_source_ptr(struct libmnt_fs *fs, char *source)
			__attribute__((nonnull(1)));
extern int __mnt_fs_set_fstype_ptr(struct libmnt_fs *fs, char *fstype)
			__attribute__((nonnull(1)));

/* context.c */
extern struct libmnt_context *mnt_copy_context(struct libmnt_context *o);
extern int mnt_context_mtab_writable(struct libmnt_context *cxt);
extern int mnt_context_utab_writable(struct libmnt_context *cxt);
extern const char *mnt_context_get_writable_tabpath(struct libmnt_context *cxt);

extern int mnt_context_get_mtab_for_target(struct libmnt_context *cxt,
				    struct libmnt_table **mtab, const char *tgt);

extern int mnt_context_prepare_srcpath(struct libmnt_context *cxt);
extern int mnt_context_prepare_target(struct libmnt_context *cxt);
extern int mnt_context_guess_srcpath_fstype(struct libmnt_context *cxt, char **type);
extern int mnt_context_guess_fstype(struct libmnt_context *cxt);
extern int mnt_context_prepare_helper(struct libmnt_context *cxt,
				      const char *name, const char *type);
extern int mnt_context_prepare_update(struct libmnt_context *cxt);
extern int mnt_context_merge_mflags(struct libmnt_context *cxt);
extern int mnt_context_update_tabs(struct libmnt_context *cxt);

extern int mnt_context_umount_setopt(struct libmnt_context *cxt, int c, char *arg);
extern int mnt_context_mount_setopt(struct libmnt_context *cxt, int c, char *arg);

extern int mnt_context_is_loopdev(struct libmnt_context *cxt)
			__attribute__((nonnull));

extern int mnt_context_propagation_only(struct libmnt_context *cxt)
			__attribute__((nonnull));

extern struct libmnt_addmount *mnt_new_addmount(void);
extern void mnt_free_addmount(struct libmnt_addmount *ad);

extern int mnt_context_setup_loopdev(struct libmnt_context *cxt);
extern int mnt_context_delete_loopdev(struct libmnt_context *cxt);
extern int mnt_context_clear_loopdev(struct libmnt_context *cxt);

extern int mnt_fork_context(struct libmnt_context *cxt);

extern int mnt_context_set_tabfilter(struct libmnt_context *cxt,
				     int (*fltr)(struct libmnt_fs *, void *),
				     void *data);

extern int mnt_context_get_generic_excode(int rc, char *buf, size_t bufsz, char *fmt, ...);
extern int mnt_context_get_mount_excode(struct libmnt_context *cxt, int mntrc, char *buf, size_t bufsz);
extern int mnt_context_get_umount_excode(struct libmnt_context *cxt, int mntrc, char *buf, size_t bufsz);

extern int mnt_context_has_template(struct libmnt_context *cxt);
extern int mnt_context_apply_template(struct libmnt_context *cxt);
extern int mnt_context_save_template(struct libmnt_context *cxt);

extern int mnt_context_apply_fs(struct libmnt_context *cxt, struct libmnt_fs *fs);

extern int mnt_context_is_veritydev(struct libmnt_context *cxt)
			__attribute__((nonnull));
extern int mnt_context_setup_veritydev(struct libmnt_context *cxt);
extern int mnt_context_deferred_delete_veritydev(struct libmnt_context *cxt);

/* tab_update.c */
extern int mnt_update_set_filename(struct libmnt_update *upd,
				   const char *filename, int userspace_only);
extern int mnt_update_already_done(struct libmnt_update *upd,
				   struct libmnt_lock *lc);

#if __linux__
/* btrfs.c */
extern uint64_t btrfs_get_default_subvol_id(const char *path);
#endif

#endif /* _LIBMOUNT_PRIVATE_H */
