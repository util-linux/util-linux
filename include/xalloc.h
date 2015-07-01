/*
 * Copyright (C) 2010 Davidlohr Bueso <dave@gnu.org>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 *
 * General memory allocation wrappers for malloc, realloc, calloc and strdup
 */

#ifndef UTIL_LINUX_XALLOC_H
#define UTIL_LINUX_XALLOC_H

#include <stdlib.h>
#include <string.h>

#include "c.h"

#ifndef XALLOC_EXIT_CODE
# define XALLOC_EXIT_CODE EXIT_FAILURE
#endif

static inline void __err_oom(const char *file, unsigned int line)
{
	err(XALLOC_EXIT_CODE, "%s: %u: cannot allocate memory", file, line);
}

#define err_oom()	__err_oom(__FILE__, __LINE__)

static inline __ul_alloc_size(1)
void *xmalloc(const size_t size)
{
        void *ret = malloc(size);

        if (!ret && size)
                err(XALLOC_EXIT_CODE, "cannot allocate %zu bytes", size);
        return ret;
}

static inline __ul_alloc_size(2)
void *xrealloc(void *ptr, const size_t size)
{
        void *ret = realloc(ptr, size);

        if (!ret && size)
                err(XALLOC_EXIT_CODE, "cannot allocate %zu bytes", size);
        return ret;
}

static inline __ul_calloc_size(1, 2)
void *xcalloc(const size_t nelems, const size_t size)
{
        void *ret = calloc(nelems, size);

        if (!ret && size && nelems)
                err(XALLOC_EXIT_CODE, "cannot allocate %zu bytes", size);
        return ret;
}

static inline char __attribute__((warn_unused_result)) *xstrdup(const char *str)
{
        char *ret;

        if (!str)
                return NULL;

        ret = strdup(str);

        if (!ret)
                err(XALLOC_EXIT_CODE, "cannot duplicate string");
        return ret;
}

static inline char * __attribute__((warn_unused_result)) xstrndup(const char *str, size_t size)
{
        char *ret;

        if (!str)
                return NULL;

        ret = strndup(str, size);

        if (!ret)
                err(XALLOC_EXIT_CODE, "cannot duplicate string");
        return ret;
}


static inline int __attribute__ ((__format__(printf, 2, 3)))
    xasprintf(char **strp, const char *fmt, ...)
{
	int ret;
	va_list args;
	va_start(args, fmt);
	ret = vasprintf(&(*strp), fmt, args);
	va_end(args);
	if (ret < 0)
		err(XALLOC_EXIT_CODE, "cannot allocate string");
	return ret;
}

static inline int  __attribute__ ((__format__(printf, 2, 0)))
xvasprintf(char **strp, const char *fmt, va_list ap)
{
	int ret = vasprintf(&(*strp), fmt, ap);
	if (ret < 0)
		err(XALLOC_EXIT_CODE, "cannot allocate string");
	return ret;
}


static inline char * __attribute__((warn_unused_result)) xgethostname(void)
{
	char *name;
	size_t sz = get_hostname_max() + 1;

	name = xmalloc(sizeof(char) * sz);

	if (gethostname(name, sz) != 0) {
		free(name);
		return NULL;
	}
	name[sz - 1] = '\0';
	return name;
}

#endif
