/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef UTIL_LINUX_STRV
#define UTIL_LINUX_STRV

#include <stdarg.h>

#include "c.h"

char **ul_strv_free(char **l);
char **ul_strv_copy(char * const *l);
unsigned ul_strv_length(char * const *l);

int ul_strv_extend_strv(char ***a, char **b);
int ul_strv_extend_strv_concat(char ***a, char **b, const char *suffix);
int ul_strv_extend(char ***l, const char *value);

int ul_strv_extendv(char ***l, const char *format, va_list ap)
		__attribute__ ((__format__ (__printf__, 2, 0)));
int ul_strv_extendf(char ***l, const char *format, ...)
		__attribute__ ((__format__ (__printf__, 2, 3)));

int ul_strv_push(char ***l, char *value);
int ul_strv_push_prepend(char ***l, char *value);
int ul_strv_consume(char ***l, char *value);
int ul_strv_consume_prepend(char ***l, char *value);

char **ul_strv_remove(char **l, const char *s);

char **ul_strv_new(const char *x, ...);

static inline const char* UL_STRV_IFNOTNULL(const char *x) {
        return x ? x : (const char *) -1;
}

static inline int ul_strv_isempty(char * const *l) {
        return !l || !*l;
}

char **ul_strv_split(const char *s, const char *separator);
char *ul_strv_join(char **l, const char *separator);

#define UL_STRV_FOREACH(s, l)                      \
        for ((s) = (l); (s) && *(s); (s)++)

#define UL_STRV_FOREACH_BACKWARDS(s, l)            \
        UL_STRV_FOREACH(s, l)                      \
                ;                               \
        for ((s)--; (l) && ((s) >= (l)); (s)--)


#define UL_STRV_MAKE_EMPTY ((char*[1]) { NULL })

char **ul_strv_reverse(char **l);

#endif /* UTIL_LINUX_STRV */


