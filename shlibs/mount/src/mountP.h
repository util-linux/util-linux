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
#define DEBUG_ALL	0xFFFF

#ifdef CONFIG_LIBMOUNT_DEBUG
#include <stdio.h>
extern int libmount_debug_mask;
#define DBG(m,x)	if ((m) & libmount_debug_mask) x
#else
#define DBG(m,x)
#endif

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
#ifndef HAVE_STRNLEN
extern size_t strnlen(const char *s, size_t maxlen);
#endif
#ifndef HAVE_STRNDUP
extern char *strndup(const char *s, size_t n);
#endif
#ifndef HAVE_STRNCHR
extern char *strnchr(const char *s, size_t maxlen, int c);
#endif
extern char *mnt_get_username(const uid_t uid);
extern char *mnt_strconcat3(char *s, const char *t, const char *u);


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
 * mnt_optls entry
 */
struct _mnt_optent {
	char			*name;	/* option name (allcocated when mapent is NULL) */
	char			*value;	/* option argument value */

	int			mask;	/* MNT_{INVMASK,MDATA,MFLAG,NOMTAB,NOSYS}
					 * modifiable flags (initial value comes from map->mask)
					 */
	const struct mnt_optmap	*mapent;/* the option description (msp entry) */
	const struct mnt_optmap	*map;   /* head of the map */

	struct list_head	opts;	/* list of options */
};

/*
 * Container (list) for mount options
 */
struct _mnt_optls {
	struct mnt_optmap const	**maps;	/* array with option maps */
	size_t			nmaps;	/* number of maps */

	struct list_head	opts;	/* list of options */
};

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

	char		*mntroot;	/* mountinfo[4]: root of the mount within the FS */
	char		*target;	/* mountinfo[5], fstab[2]: mountpoint */
	char		*fstype;	/* mountinfo[9], fstab[3]: filesystem type */

	char		*optstr;	/* mountinfo[6,11], fstab[4]: option string */
	char		*vfs_optstr;	/* mountinfo[6]: fs-independent (VFS) options */
	char		*fs_optstr;	/* mountinfo[11]: fs-depend options */

	int		freq;		/* fstab[5]:  dump frequency in days */
	int		passno;		/* fstab[6]: pass number on parallel fsck */

	int		flags;		/* MNT_FS_* flags */
	int		lineno;		/* line number in the parental file */

	void		*userdata;	/* library independent data */
};

/*
 * fs flags
 */
#define MNT_FS_ERROR	(1 << 1) /* broken entry */
#define MNT_FS_PSEUDO	(1 << 2) /* pseudo filesystem */
#define MNT_FS_NET	(1 << 3) /* network filesystem */

/*
 * File format
 */
enum {
       MNT_FMT_FSTAB = 1,              /* /etc/{fs,m}tab */
       MNT_FMT_MOUNTINFO               /* /proc/#/mountinfo */
};

/*
 * mtab/fstab/mountinfo file
 */
struct _mnt_tab {
	char		*filename;	/* file name or NULL */
	int		fmt;		/* MNT_FMT_* file format */

	int		nlines;		/* number of lines in the file (include commentrys) */
	int		nents;		/* number of valid entries */
	int		nerrs;		/* number of broken entries (parse errors) */

	mnt_cache	*cache;		/* canonicalized paths/tags cache */

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

/* optent.c */

/* private option masks -- see mount.h.in for the publick masks */
#define MNT_HASVAL	(1 << 10)

extern mnt_optent *mnt_new_optent(const char *name, size_t namesz,
				const char *value, size_t valsz,
				struct mnt_optmap const **maps, int nmaps);
extern void mnt_free_optent(mnt_optent *op);
extern mnt_optent *mnt_new_optent_from_optstr(char **optstr,
	                        struct mnt_optmap const **maps, int nmaps);
extern int mnt_optent_assign_map(mnt_optent *op,
	                        struct mnt_optmap const **maps, int nmaps);

/* fs.c */
extern int __mnt_fs_set_source(mnt_fs *fs, char *source);
extern int __mnt_fs_set_fstype(mnt_fs *fs, char *fstype);



#endif /* _LIBMOUNT_PRIVATE_H */
