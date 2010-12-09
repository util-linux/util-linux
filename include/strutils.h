#ifndef UTIL_LINUX_STRUTILS
#define UTIL_LINUX_STRUTILS

#include <inttypes.h>
#include <string.h>
#include <sys/types.h>

extern int strtosize(const char *str, uintmax_t *res);
extern long strtol_or_err(const char *str, const char *errmesg);

#ifndef HAVE_STRNLEN
extern size_t strnlen(const char *s, size_t maxlen);
#endif
#ifndef HAVE_STRNDUP
extern char *strndup(const char *s, size_t n);
#endif
#ifndef HAVE_STRNCHR
extern char *strnchr(const char *s, size_t maxlen, int c);
#endif

/* caller guarantees n > 0 */
static inline void xstrncpy(char *dest, const char *src, size_t n)
{
	strncpy(dest, src, n-1);
	dest[n-1] = 0;
}

extern void strmode(mode_t mode, char *str);
extern char *size_to_human_string(uint64_t bytes);

#endif
