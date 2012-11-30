/*
 * fdiskP.h - private library header file
 *
 * Copyright (C) 2012 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#ifndef _LIBFDISK_PRIVATE_H
#define _LIBFDISK_PRIVATE_H

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "c.h"
#include "libfdisk.h"

/* features */
#define CONFIG_LIBFDISK_ASSERT
#define CONFIG_LIBFDISK_DEBUG

#ifdef CONFIG_LIBFDISK_ASSERT
#include <assert.h>
#endif

/*
 * Debug
 */
#if defined(TEST_PROGRAM) && !defined(LIBFDISK_DEBUG)
#define CONFIG_LIBFDISK_DEBUG
#endif

#ifdef CONFIG_LIBFDISK_DEBUG
# include <stdio.h>
# include <stdarg.h>

/* fdisk debugging flags/options */
#define FDISK_DEBUG_INIT	(1 << 1)
#define FDISK_DEBUG_CONTEXT	(1 << 2)
#define FDISK_DEBUG_TOPOLOGY    (1 << 3)
#define FDISK_DEBUG_GEOMETRY    (1 << 4)
#define FDISK_DEBUG_LABEL       (1 << 5)
#define FDISK_DEBUG_ALL		0xFFFF

# define ON_DBG(m, x)	do { \
				if ((FDISK_DEBUG_ ## m) & fdisk_debug_mask) { \
					x; \
				}	   \
			} while (0)

# define DBG(m, x)	do { \
				if ((FDISK_DEBUG_ ## m) & fdisk_debug_mask) { \
					fprintf(stderr, "%d: fdisk: %8s: ", getpid(), # m); \
					x;				\
				} \
			} while (0)

# define DBG_FLUSH	do { \
				if (fdisk_debug_mask && \
				    fdisk_debug_mask != FDISK_DEBUG_INIT) \
					fflush(stderr);			\
			} while(0)

static inline void __attribute__ ((__format__ (__printf__, 1, 2)))
dbgprint(const char *mesg, ...)
{
	va_list ap;
	va_start(ap, mesg);
	vfprintf(stderr, mesg, ap);
	va_end(ap);
	fputc('\n', stderr);
}

extern int fdisk_debug_mask;

#else /* !CONFIG_LIBFDISK_DEBUG */
# define ON_DBG(m,x) do { ; } while (0)
# define DBG(m,x) do { ; } while (0)
# define DBG_FLUSH do { ; } while(0)
#endif

#endif /* _LIBFDISK_PRIVATE_H */
