#ifndef UTIL_LINUX_STRUTILS
#define UTIL_LINUX_STRUTILS

#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

/* initialize a custom exit code for all *_or_err functions */
extern void strutils_set_exitcode(int exit_code);

extern int parse_size(const char *str, uintmax_t *res, int *power);
extern int strtosize(const char *str, uintmax_t *res);
extern uintmax_t strtosize_or_err(const char *str, const char *errmesg);

extern int16_t strtos16_or_err(const char *str, const char *errmesg);
extern uint16_t strtou16_or_err(const char *str, const char *errmesg);
extern uint16_t strtox16_or_err(const char *str, const char *errmesg);

extern int32_t strtos32_or_err(const char *str, const char *errmesg);
extern uint32_t strtou32_or_err(const char *str, const char *errmesg);
extern uint32_t strtox32_or_err(const char *str, const char *errmesg);

extern int64_t strtos64_or_err(const char *str, const char *errmesg);
extern uint64_t strtou64_or_err(const char *str, const char *errmesg);
extern uint64_t strtox64_or_err(const char *str, const char *errmesg);

extern double strtod_or_err(const char *str, const char *errmesg);

extern long strtol_or_err(const char *str, const char *errmesg);
extern unsigned long strtoul_or_err(const char *str, const char *errmesg);

extern void strtotimeval_or_err(const char *str, struct timeval *tv,
		const char *errmesg);

extern int isdigit_strend(const char *str, const char **end);
#define isdigit_string(_s)	isdigit_strend(_s, NULL)

extern int isxdigit_strend(const char *str, const char **end);
#define isxdigit_string(_s)	isxdigit_strend(_s, NULL)


extern int parse_switch(const char *arg, const char *errmesg, ...);

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

/* This is like strncpy(), but based on memcpy(), so compilers and static
 * analyzers do not complain when sizeof(destination) is the same as 'n' and
 * result is not terminated by zero.
 *
 * Use this function to copy string to logs with fixed sizes (wtmp/utmp. ...)
 * where string terminator is optional.
 */
static inline void *str2memcpy(void *dest, const char *src, size_t n)
{
	size_t bytes = strlen(src) + 1;

	if (bytes > n)
		bytes = n;

	memcpy(dest, src, bytes);
	return dest;
}

static inline char *mem2strcpy(char *dest, const void *src, size_t n, size_t nmax)
{
	if (n + 1 > nmax)
		n = nmax - 1;

	memcpy(dest, src, n);
	dest[nmax-1] = '\0';
	return dest;
}

static inline int strdup_to_offset(void *stru, size_t offset, const char *str)
{
	char *n = NULL;
	char **o;

	if (!stru)
		return -EINVAL;

	o = (char **) ((char *) stru + offset);
	if (str) {
		n = strdup(str);
		if (!n)
			return -ENOMEM;
	}

	free(*o);
	*o = n;
	return 0;
}

#define strdup_to_struct_member(_s, _m, _str) \
		strdup_to_offset((void *) _s, offsetof(__typeof__(*(_s)), _m), _str)

extern char *xstrmode(mode_t mode, char *str);

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
				 size_t arysz, size_t *ary_pos,
				 int (name2id)(const char *, size_t));

extern int string_to_bitarray(const char *list, char *ary,
			    int (*name2bit)(const char *, size_t));

extern int string_to_bitmask(const char *list,
			     unsigned long *mask,
			     long (*name2flag)(const char *, size_t));
extern int parse_range(const char *str, int *lower, int *upper, int def);

extern int streq_paths(const char *a, const char *b);

/*
 * Match string beginning.
 */
static inline const char *startswith(const char *s, const char *prefix)
{
	size_t sz = prefix ? strlen(prefix) : 0;

        if (s && sz && strncmp(s, prefix, sz) == 0)
                return s + sz;
	return NULL;
}

/*
 * Case insensitive match string beginning.
 */
static inline const char *startswith_no_case(const char *s, const char *prefix)
{
	size_t sz = prefix ? strlen(prefix) : 0;

        if (s && sz && strncasecmp(s, prefix, sz) == 0)
                return s + sz;
	return NULL;
}

/*
 * Match string ending.
 */
static inline const char *endswith(const char *s, const char *postfix)
{
	size_t sl = s ? strlen(s) : 0;
	size_t pl = postfix ? strlen(postfix) : 0;

	if (pl == 0)
		return s + sl;
	if (sl < pl)
		return NULL;
	if (memcmp(s + sl - pl, postfix, pl) != 0)
		return NULL;
	return s + sl - pl;
}

/*
 * Skip leading white space.
 */
static inline const char *skip_space(const char *p)
{
	while (isspace(*p))
		++p;
	return p;
}

static inline const char *skip_blank(const char *p)
{
	while (isblank(*p))
		++p;
	return p;
}


/* Removes whitespace from the right-hand side of a string (trailing
 * whitespace).
 *
 * Returns size of the new string (without \0).
 */
static inline size_t rtrim_whitespace(unsigned char *str)
{
	size_t i;

	if (!str)
		return 0;
	i = strlen((char *) str);
	while (i) {
		i--;
		if (!isspace(str[i])) {
			i++;
			break;
		}
	}
	str[i] = '\0';
	return i;
}

/* Removes whitespace from the left-hand side of a string.
 *
 * Returns size of the new string (without \0).
 */
static inline size_t ltrim_whitespace(unsigned char *str)
{
	size_t len;
	unsigned char *p;

	if (!str)
		return 0;
	for (p = str; *p && isspace(*p); p++);

	len = strlen((char *) p);

	if (p > str)
		memmove(str, p, len + 1);

	return len;
}

extern char *strnappend(const char *s, const char *suffix, size_t b);
extern char *strappend(const char *s, const char *suffix);
extern char *strfappend(const char *s, const char *format, ...)
		 __attribute__ ((__format__ (__printf__, 2, 0)));
extern const char *split(const char **state, size_t *l, const char *separator, int quoted);

extern int skip_fline(FILE *fp);

#endif
