/*
 * Copyright (C) 2010 Davidlohr Bueso <dave@gnu.org>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 *
 * General memory allocation wrappers for malloc, realloc and calloc
 */

#ifndef UTIL_LINUX_XALLOC_H
#define UTIL_LINUX_XALLOC_H

#include <stdlib.h>
#include <err.h>

static inline __attribute__((alloc_size(1)))
void *xmalloc(const size_t size)
{
        void *ret = malloc(size);

        if (!ret && size)
                err(EXIT_FAILURE, "cannot allocate %zu bytes", size);
        return ret;
}

static inline __attribute__((alloc_size(2)))
void *xrealloc(void *ptr, const size_t size)
{
        void *ret = realloc(ptr, size);

        if (!ret && size)
                err(EXIT_FAILURE, "cannot allocate %zu bytes", size);
        return ret;
}

static inline __attribute__((alloc_size(1,2)))
void *xcalloc(const size_t nelems, const size_t size)
{
        void *ret = calloc(nelems, size);

        if (!ret && size && nelems)
                err(EXIT_FAILURE, "cannot allocate %zu bytes", size);
        return ret;
}

#endif
