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

#define DEBUG_INIT	(1 << 1)
#define DEBUG_CACHE	(1 << 2)
#define DEBUG_OPTIONS	(1 << 3)
#define DEBUG_LOCKS	(1 << 4)
#define DEBUG_TAB	(1 << 5)
#define DEBUG_MTAB	(1 << 6)
#define DEBUG_UTILS	(1 << 7)
#define DEBUG_ALL	0xFFFF

#ifdef CONFIG_LIBMOUNT_DEBUG
#include <stdio.h>
extern int libmount_debug_mask;
#define DBG(m,x)	if ((m) & libmount_debug_mask) x
#else
#define DBG(m,x)
#endif

/* extension for files in the /etc/fstab.d directory */
#define MNT_MNTTABDIR_EXT	".fstab"

/* library private paths */
#define MNT_PATH_RUNDIR		"/var/run/mount"
#define MNT_PATH_MOUNTINFO	MNT_PATH_RUNDIR "/mountinfo"

#ifdef TEST_PROGRAM
struct mtest {
	const char	*name;
	int		(*body)(struct mtest *ts, int argc, char *argv[]);
	const char	*usage;
};

/* utils.c */
extern int mnt_run_test(struct mtest *tests, int argc, char *argv[]);
#endif

/* utils.c */
extern char *mnt_getenv_safe(const char *arg);
extern int endswith(const char *s, const char *sx);
extern int startswith(const char *s, const char *sx);

extern char *mnt_get_username(const uid_t uid);
extern int mnt_has_regular_mtab(void);

extern char *mnt_get_mountpoint(const char *path);
extern char *mnt_get_fs_root(const char *path, const char *mountpoint);

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
 */
struct _mnt_fs {
	struct list_head ents;

	int		id;		/* mountinfo[1]: ID */
	int		parent;		/* moutninfo[2]: parent */
	dev_t		devno;		/* moutninfo[3]: st_dev */

	char		*source;	/* fstab[1]: mountinfo[10]:
                                         * source dev, file, dir or TAG */
	char		*tagname;	/* fstab[1]: tag name - "LABEL", "UUID", ..*/
	char		*tagval;	/*           tag value */

	char		*root;		/* mountinfo[4]: root of the mount within the FS */
	char		*target;	/* mountinfo[5], fstab[2]: mountpoint */
	char		*fstype;	/* mountinfo[9], fstab[3]: filesystem type */

	char		*optstr;	/* mountinfo[6,11], fstab[4]: option string */
	char		*vfs_optstr;	/* mountinfo[6]: fs-independent (VFS) options */
	char		*fs_optstr;	/* mountinfo[11]: fs-depend options */

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

/*
 * mtab/fstab/mountinfo file
 */
struct _mnt_tab {
	int		fmt;		/* MNT_FMT_* file format */
	int		nents;		/* number of valid entries */

	mnt_cache	*cache;		/* canonicalized paths/tags cache */

        int		(*errcb)(mnt_tab *tb, const char *filename,
						int line, int flag);

	struct list_head	ents;	/* list of entries (mentry) */
};


/* optmap.c */
extern const struct mnt_optmap *mnt_optmap_get_entry(struct mnt_optmap const **maps,
                             int nmaps, const char *name,
                             size_t namelen, const struct mnt_optmap **mapent);
extern int mnt_optmap_enum_to_number(const struct mnt_optmap *mapent,
                        const char *rawdata, size_t len);
extern const char *mnt_optmap_get_type(const struct mnt_optmap *mapent);
extern int mnt_optmap_require_value(const struct mnt_optmap *mapent);

/* fs.c */
extern int __mnt_fs_set_source(mnt_fs *fs, char *source);
extern int __mnt_fs_set_fstype(mnt_fs *fs, char *fstype);

#endif /* _LIBMOUNT_PRIVATE_H */
