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

#include "mount.h"
#include "list.h"

/* features */
#define CONFIG_CDROM_NOMEDIUM_RETRIES    5
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
#define DEBUG_ALL	0xFFFF

#ifdef CONFIG_LIBMOUNT_DEBUG
#include <stdio.h>
extern int libmount_debug_mask;
extern void mnt_init_debug(int mask);
#define DBG(m,x)	if ((m) & libmount_debug_mask) x
#else
#define DBG(m,x)
#define mnt_init_debug(x)
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

#endif
