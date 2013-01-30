#ifndef UTIL_LINUX_STRUTILS
#define UTIL_LINUX_STRUTILS

#include <inttypes.h>
#include <string.h>
#include <sys/types.h>

/* default strtoxx_or_err() exit code */
#ifndef STRTOXX_EXIT_CODE
# define STRTOXX_EXIT_CODE EXIT_FAILURE
#endif


extern int strtosize(const char *str, uintmax_t *res);
extern uintmax_t strtosize_or_err(const char *str, const char *errmesg);

extern int16_t strtos16_or_err(const char *str, const char *errmesg);
extern uint16_t strtou16_or_err(const char *str, const char *errmesg);

extern int32_t strtos32_or_err(const char *str, const char *errmesg);
extern uint32_t strtou32_or_err(const char *str, const char *errmesg);

extern int64_t strtos64_or_err(const char *str, const char *errmesg);
extern uint64_t strtou64_or_err(const char *str, const char *errmesg);

extern double strtod_or_err(const char *str, const char *errmesg);

extern long strtol_or_err(const char *str, const char *errmesg);
extern unsigned long strtoul_or_err(const char *str, const char *errmesg);

#ifndef HAVE_MEMPCPY
extern void *mempcpy(void *restrict dest, const void *restrict src, size_t n);
#endif
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

/* Options for size_to_human_string() */
enum
{
        SIZE_SUFFIX_1LETTER = 0,
        SIZE_SUFFIX_3LETTER = 1,
        SIZE_SUFFIX_SPACE   = 2
};

extern char *size_to_human_string(int options, uint64_t bytes);

extern int string_to_idarray(const char *list, int ary[], size_t arysz,
			   int (name2id)(const char *, size_t));
extern int string_add_to_idarray(const char *list, int ary[],
				 size_t arysz, int *ary_pos,
				 int (name2id)(const char *, size_t));

extern int string_to_bitarray(const char *list, char *ary,
			    int (*name2bit)(const char *, size_t));

extern int string_to_bitmask(const char *list,
			     unsigned long *mask,
			     long (*name2flag)(const char *, size_t));
extern int parse_range(const char *str, int *lower, int *upper, int def);

extern int streq_except_trailing_slash(const char *s1, const char *s2);

#endif
