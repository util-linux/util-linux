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

/* features */
#define CONFIG_CDROM_NOMEDIUM_RETRIES    5
#define CONFIG_LIBMOUNT_ASSERT

#ifdef CONFIG_LIBMOUNT_ASSERT
#include <assert.h>
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

#endif
