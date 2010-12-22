/*
 * mountP.h - private library header file
 *
 * Copyright (C) 2008-2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#ifndef _LIBMOUNT_PRIVATE_H
#define _LIBMOUNT_PRIVATE_H

#include <sys/types.h>
#include <errno.h>
#include "c.h"

#define USE_UNSTABLE_LIBMOUNT_API

#include "mount.h"
#include "list.h"

/* features */
#define CONFIG_LIBMOUNT_ASSERT
#define CONFIG_LIBMOUNT_DEBUG

#ifdef CONFIG_LIBMOUNT_ASSERT
#include <assert.h>
#endif

/*
 * Debug
 */
#if defined(TEST_PROGRAM) && !defined(LIBMOUNT_DEBUG)
#define CONFIG_LIBMOUNT_DEBUG
#endif

#define MNT_DEBUG_INIT		(1 << 1)
#define MNT_DEBUG_CACHE		(1 << 2)
#define MNT_DEBUG_OPTIONS	(1 << 3)
#define MNT_DEBUG_LOCKS		(1 << 4)
#define MNT_DEBUG_TAB		(1 << 5)
#define MNT_DEBUG_FS		(1 << 6)
#define MNT_DEBUG_OPTS		(1 << 7)
#define MNT_DEBUG_UPDATE	(1 << 8)
#define MNT_DEBUG_UTILS		(1 << 9)
#define MNT_DEBUG_CXT		(1 << 10)
#define MNT_DEBUG_ALL		0xFFFF

#ifdef CONFIG_LIBMOUNT_DEBUG
# include <stdio.h>
# include <stdarg.h>

# define DBG(m,x)	do { \
				if ((MNT_DEBUG_ ## m) & libmount_debug_mask) {\
					fprintf(stderr, "libmount: %8s: ", # m); \
					x; \
				} \
			} while(0)

# define DBG_FLUSH	do { fflush(stderr); } while(0)

extern int libmount_debug_mask;

static inline void __attribute__ ((__format__ (__printf__, 1, 2)))
mnt_debug(const char *mesg, ...)
{
	va_list ap;
	va_start(ap, mesg);
	vfprintf(stderr, mesg, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static inline void __attribute__ ((__format__ (__printf__, 2, 3)))
mnt_debug_h(void *handler, const char *mesg, ...)
{
	va_list ap;

	fprintf(stderr, "[%p]: ", handler);
	va_start(ap, mesg);
	vfprintf(stderr, mesg, ap);
	va_end(ap);
	fputc('\n', stderr);
}

#else /* !CONFIG_LIBMOUNT_DEBUG */
# define DBG(m,x) do { ; } while(0)
# define DBG_FLUSH do { ; } while(0)
#endif

/* extension for files in the /etc/fstab.d directory */
#define MNT_MNTTABDIR_EXT	".fstab"

/* library private paths */
#define MNT_PATH_UTAB	"/dev/.mount/utab"

#define MNT_UTAB_HEADER	"# libmount utab file\n"

#ifdef TEST_PROGRAM
struct mtest {
	const char	*name;
	int		(*body)(struct mtest *ts, int argc, char *argv[]);
	const char	*usage;
};

/* test.c */
extern int mnt_run_test(struct mtest *tests, int argc, char *argv[]);
#endif

/* utils.c */
extern char *mnt_getenv_safe(const char *arg);
extern int endswith(const char *s, const char *sx);
extern int startswith(const char *s, const char *sx);

extern char *mnt_get_username(const uid_t uid);
extern int mnt_get_uid(const char *username, uid_t *uid);
extern int mnt_get_gid(const char *groupname, gid_t *gid);
extern int mnt_in_group(gid_t gid);

extern char *mnt_get_mountpoint(const char *path);
extern char *mnt_get_fs_root(const char *path, const char *mountpoint);
extern int mnt_open_uniq_filename(const char *filename, char **name, int flags);
extern int mnt_has_regular_utab(const char **utab, int *writable);
extern const char *mnt_get_utab_path(void);

/*
 * Generic iterator
 */
struct _mnt_iter {
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
 * This struct represents one entry in mtab/fstab/mountinfo file.
 * (note that fstab[1] means the first column from fstab, and so on...)
 */
struct _mnt_fs {
	struct list_head ents;

	int		id;		/* mountinfo[1]: ID */
	int		parent;		/* moutninfo[2]: parent */
	dev_t		devno;		/* moutninfo[3]: st_dev */

	char		*bindsrc;	/* utab, full path from fstab[1] for bind mounts */

	char		*source;	/* fstab[1], mountinfo[10]:
                                         * source dev, file, dir or TAG */
	char		*tagname;	/* fstab[1]: tag name - "LABEL", "UUID", ..*/
	char		*tagval;	/*           tag value */

	char		*root;		/* mountinfo[4]: root of the mount within the FS */
	char		*target;	/* mountinfo[5], fstab[2]: mountpoint */
	char		*fstype;	/* mountinfo[9], fstab[3]: filesystem type */

	char		*vfs_optstr;	/* mountinfo[6]: fs-independent (VFS) options */
	char		*fs_optstr;	/* mountinfo[11]: fs-depend options */
	char		*user_optstr;	/* userspace mount options */
	char		*attrs;		/* mount attributes */

	int		freq;		/* fstab[5]:  dump frequency in days */
	int		passno;		/* fstab[6]: pass number on parallel fsck */

	int		flags;		/* MNT_FS_* flags */

	void		*userdata;	/* library independent data */
};

/*
 * fs flags
 */
#define MNT_FS_PSEUDO	(1 << 1) /* pseudo filesystem */
#define MNT_FS_NET	(1 << 2) /* network filesystem */
#define MNT_FS_SWAP	(1 << 3) /* swap device */

/*
 * mtab/fstab/mountinfo file
 */
struct _mnt_tab {
	int		fmt;		/* MNT_FMT_* file format */
	int		nents;		/* number of valid entries */

	mnt_cache	*cache;		/* canonicalized paths/tags cache */

        int		(*errcb)(mnt_tab *tb, const char *filename, int line);

	struct list_head	ents;	/* list of entries (mentry) */
};

extern mnt_tab *__mnt_new_tab_from_file(const char *filename, int fmt);

/*
 * Tab file format
 */
enum {
	MNT_FMT_GUESS,
	MNT_FMT_FSTAB,			/* /etc/{fs,m}tab */
	MNT_FMT_MTAB = MNT_FMT_FSTAB,	/* alias */
	MNT_FMT_MOUNTINFO,		/* /proc/#/mountinfo */
	MNT_FMT_UTAB			/* /dev/.mount/utab */
};


/*
 * Mount context -- high-level API
 */
struct _mnt_context
{
	int	action;		/* MNT_ACT_{MOUNT,UMOUNT} */
	int	restricted;	/* root or not? */

	char	*fstype_pattern;	/* for mnt_match_fstype() */
	char	*optstr_pattern;	/* for mnt_match_options() */

	mnt_fs	*fs;		/* filesystem description (type, mountpopint, device, ...) */

	mnt_tab	*fstab;		/* fstab (or mtab for some remounts) entires */
	mnt_tab *mtab;		/* mtab entries */
	int	optsmode;	/* fstab optstr mode MNT_OPTSMODE_{AUTO,FORCE,IGNORE} */

	unsigned long	mountflags;	/* final mount(2) flags */
	const void	*mountdata;	/* final mount(2) data, string or binary data */

	unsigned long	user_mountflags;	/* MNT_MS_* (loop=, user=, ...) */

	mnt_cache	*cache;	/* paths cache */
	mnt_lock	*lock;	/* mtab lock */
	mnt_update	*update;/* mtab/utab update */

	const char	*mtab_path; /* writable mtab */
	int		mtab_writable; /* ismtab writeable */

	const char	*utab_path; /* writable mtab */
	int		utab_writable; /* ismtab writeable */

	int	flags;		/* private context flags */
	int	ambi;		/* libblkid returns ambivalent result */

	char	*helper;	/* name of the used /sbin/[u]mount.<type> helper */
	int	helper_status;	/* helper wait(2) status */
	int	helper_exec_status; /* 1: not called yet, 0: success, <0: -errno */

	char	*orig_user;	/* original (non-fixed) user= option */

	int	syscall_status;	/* 1: not called yet, 0: success, <0: -errno */
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
#define MNT_FL_NOLOCK		(1 << 10)	/* don't lock mtab file */
#define MNT_FL_RDONLY_UMOUNT	(1 << 11)	/* remount,ro after EBUSY umount(2) */

#define MNT_FL_EXTERN_FS	(1 << 15)	/* cxt->fs is not private */
#define MNT_FL_EXTERN_FSTAB	(1 << 16)	/* cxt->fstab is not private */
#define MNT_FL_EXTERN_CACHE	(1 << 17)	/* cxt->cache is not private */

#define MNT_FL_MOUNTDATA	(1 << 20)
#define MNT_FL_TAB_APPLIED	(1 << 21)	/* mtab/fstab merged to cxt->fs */
#define MNT_FL_MOUNTFLAGS_MERGED (1 << 22)	/* MS_* flags was read from optstr */
#define MNT_FL_SAVED_USER	(1 << 23)

/* default flags */
#define MNT_FL_DEFAULT		0

/* optmap.c */
extern const struct mnt_optmap *mnt_optmap_get_entry(struct mnt_optmap const **maps,
                             int nmaps, const char *name,
                             size_t namelen, const struct mnt_optmap **mapent);
extern int mnt_optmap_enum_to_number(const struct mnt_optmap *mapent,
                        const char *rawdata, size_t len);
extern const char *mnt_optmap_get_type(const struct mnt_optmap *mapent);
extern int mnt_optmap_require_value(const struct mnt_optmap *mapent);

/* optstr.c */
extern int mnt_optstr_remove_option_at(char **optstr, char *begin, char *end);
extern int mnt_optstr_fix_gid(char **optstr, char *value, size_t valsz, char **next);
extern int mnt_optstr_fix_uid(char **optstr, char *value, size_t valsz, char **next);
extern int mnt_optstr_fix_secontext(char **optstr, char *value, size_t valsz, char **next);
extern int mnt_optstr_fix_user(char **optstr);

/* fs.c */
extern int __mnt_fs_set_source_ptr(mnt_fs *fs, char *source);
extern int __mnt_fs_set_fstype_ptr(mnt_fs *fs, char *fstype);
extern int __mnt_fs_set_optstr_ptr(mnt_fs *fs, char *optstr, int split);
extern int __mnt_fs_set_optstr(mnt_fs *fs, const char *optstr, int split);

/* context.c */
extern int mnt_context_prepare_srcpath(mnt_context *cxt);
extern int mnt_context_guess_fstype(mnt_context *cxt);
extern int mnt_context_prepare_helper(mnt_context *cxt, const char *name, const char *type);
extern int mnt_context_prepare_update(mnt_context *cxt);
extern mnt_fs *mnt_context_get_fs(mnt_context *cxt);
extern int mnt_context_merge_mountflags(mnt_context *cxt);
extern int mnt_context_update_tabs(mnt_context *cxt);

/* tab_update.c */
extern mnt_fs *mnt_update_get_fs(mnt_update *upd);
extern int mnt_update_set_filename(mnt_update *upd, const char *filename, int userspace_only);

#endif /* _LIBMOUNT_PRIVATE_H */
