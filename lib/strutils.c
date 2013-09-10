/*
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2010 Davidlohr Bueso <dave@gnu.org>
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <assert.h>

#include "c.h"
#include "nls.h"
#include "strutils.h"
#include "bitops.h"

static int do_scale_by_power (uintmax_t *x, int base, int power)
{
	while (power--) {
		if (UINTMAX_MAX / base < *x)
			return -2;
		*x *= base;
	}
	return 0;
}

/*
 * strtosize() - convert string to size (uintmax_t).
 *
 * Supported suffixes:
 *
 * XiB or X for 2^N
 *     where X = {K,M,G,T,P,E,Z,Y}
 *        or X = {k,m,g,t,p,e}  (undocumented for backward compatibility only)
 * for example:
 *		10KiB	= 10240
 *		10K	= 10240
 *
 * XB for 10^N
 *     where X = {K,M,G,T,P,E,Z,Y}
 * for example:
 *		10KB	= 10000
 *
 * The optinal 'power' variable returns number associated with used suffix
 * {K,M,G,T,P,E,Z,Y}  = {1,2,3,4,5,6,7,8}.
 *
 * Note that the function does not accept numbers with '-' (negative sign)
 * prefix.
 */
int parse_size(const char *str, uintmax_t *res, int *power)
{
	char *p;
	uintmax_t x;
	int base = 1024, rc = 0, pwr = 0;

	static const char *suf  = "KMGTPEYZ";
	static const char *suf2 = "kmgtpeyz";
	const char *sp;

	*res = 0;

	if (!str || !*str)
		goto err;

	/* Only positive numbers are acceptable
	 *
	 * Note that this check is not perfect, it would be better to
	 * use lconv->negative_sign. But coreutils use the same solution,
	 * so it's probably good enough...
	 */
	p = (char *) str;
	while (isspace((unsigned char) *p))
		p++;
	if (*p == '-')
		goto err;
	p = NULL;

	errno = 0;
	x = strtoumax(str, &p, 0);

	if (p == str ||
	    (errno != 0 && (x == UINTMAX_MAX || x == 0)))
		goto err;

	if (!p || !*p)
		goto done;			/* without suffix */

	/*
	 * Check size suffixes
	 */
	if (*(p + 1) == 'i' && *(p + 2) == 'B' && !*(p + 3))
		base = 1024;			/* XiB, 2^N */
	else if (*(p + 1) == 'B' && !*(p + 2))
		base = 1000;			/* XB, 10^N */
	else if (*(p + 1))
		goto err;			/* unexpected suffix */

	sp = strchr(suf, *p);
	if (sp)
		pwr = (sp - suf) + 1;
	else {
		sp = strchr(suf2, *p);
		if (sp)
			pwr = (sp - suf2) + 1;
		else
			goto err;
	}

	rc = do_scale_by_power(&x, base, pwr);
	if (power)
		*power = pwr;
done:
	*res = x;
	return rc;
err:
	return -1;
}

int strtosize(const char *str, uintmax_t *res)
{
	return parse_size(str, res, NULL);
}

int isdigit_string(const char *str)
{
	const char *p;

	for (p = str; p && *p && isdigit((unsigned char) *p); p++);

	return p && p > str && !*p;
}


#ifndef HAVE_MEMPCPY
void *mempcpy(void *restrict dest, const void *restrict src, size_t n)
{
    return ((char *)memcpy(dest, src, n)) + n;
}
#endif

#ifndef HAVE_STRNLEN
size_t strnlen(const char *s, size_t maxlen)
{
        int i;

        for (i = 0; i < maxlen; i++) {
                if (s[i] == '\0')
                        return i + 1;
        }
        return maxlen;
}
#endif

#ifndef HAVE_STRNCHR
char *strnchr(const char *s, size_t maxlen, int c)
{
	for (; maxlen-- && *s != '\0'; ++s)
		if (*s == (char)c)
			return (char *)s;
	return NULL;
}
#endif

#ifndef HAVE_STRNDUP
char *strndup(const char *s, size_t n)
{
	size_t len = strnlen(s, n);
	char *new = (char *) malloc((len + 1) * sizeof(char));
	if (!new)
		return NULL;
	new[len] = '\0';
	return (char *) memcpy(new, s, len);
}
#endif

int16_t strtos16_or_err(const char *str, const char *errmesg)
{
	int32_t num = strtos32_or_err(str, errmesg);

	if (num < INT16_MIN || num > INT16_MAX)
		errx(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);

	return num;
}

uint16_t strtou16_or_err(const char *str, const char *errmesg)
{
	uint32_t num = strtou32_or_err(str, errmesg);

	if (num > UINT16_MAX)
		errx(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);

	return num;
}

int32_t strtos32_or_err(const char *str, const char *errmesg)
{
	int64_t num = strtos64_or_err(str, errmesg);

	if (num < INT32_MIN || num > INT32_MAX)
		errx(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);

	return num;
}

uint32_t strtou32_or_err(const char *str, const char *errmesg)
{
	uint64_t num = strtou64_or_err(str, errmesg);

	if (num > UINT32_MAX)
		errx(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);

	return num;
}

int64_t strtos64_or_err(const char *str, const char *errmesg)
{
	int64_t num;
	char *end = NULL;

	if (str == NULL || *str == '\0')
		goto err;
	errno = 0;
	num = strtoimax(str, &end, 10);

	if (errno || str == end || (end && *end))
		goto err;

	return num;
err:
	if (errno)
		err(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);

	errx(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);
}

uint64_t strtou64_or_err(const char *str, const char *errmesg)
{
	uintmax_t num;
	char *end = NULL;

	if (str == NULL || *str == '\0')
		goto err;
	errno = 0;
	num = strtoumax(str, &end, 10);

	if (errno || str == end || (end && *end))
		goto err;

	return num;
err:
	if (errno)
		err(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);

	errx(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);
}


double strtod_or_err(const char *str, const char *errmesg)
{
	double num;
	char *end = NULL;

	if (str == NULL || *str == '\0')
		goto err;
	errno = 0;
	num = strtod(str, &end);

	if (errno || str == end || (end && *end))
		goto err;

	return num;
err:
	if (errno)
		err(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);

	errx(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);
}

long strtol_or_err(const char *str, const char *errmesg)
{
	long num;
	char *end = NULL;

	if (str == NULL || *str == '\0')
		goto err;
	errno = 0;
	num = strtol(str, &end, 10);

	if (errno || str == end || (end && *end))
		goto err;

	return num;
err:
	if (errno)
		err(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);
	errx(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);
}

unsigned long strtoul_or_err(const char *str, const char *errmesg)
{
	unsigned long num;
	char *end = NULL;

	if (str == NULL || *str == '\0')
		goto err;
	errno = 0;
	num = strtoul(str, &end, 10);

	if (errno || str == end || (end && *end))
		goto err;

	return num;
err:
	if (errno)
		err(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);

	errx(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);
}

uintmax_t strtosize_or_err(const char *str, const char *errmesg)
{
	uintmax_t num;

	if (strtosize(str, &num) == 0)
		return num;

	if (errno)
		err(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);

	errx(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);
}


void strtotimeval_or_err(const char *str, struct timeval *tv, const char *errmesg)
{
	double user_input;

	user_input = strtod_or_err(str, errmesg);
	tv->tv_sec = (time_t) user_input;
	tv->tv_usec = (long)((user_input - tv->tv_sec) * 1000000);
}

/*
 * Converts stat->st_mode to ls(1)-like mode string. The size of "str" must
 * be 10 bytes.
 */
void strmode(mode_t mode, char *str)
{
	if (S_ISDIR(mode))
		str[0] = 'd';
	else if (S_ISLNK(mode))
		str[0] = 'l';
	else if (S_ISCHR(mode))
		str[0] = 'c';
	else if (S_ISBLK(mode))
		str[0] = 'b';
	else if (S_ISSOCK(mode))
		str[0] = 's';
	else if (S_ISFIFO(mode))
		str[0] = 'p';
	else if (S_ISREG(mode))
		str[0] = '-';

	str[1] = mode & S_IRUSR ? 'r' : '-';
	str[2] = mode & S_IWUSR ? 'w' : '-';
	str[3] = (mode & S_ISUID
		? (mode & S_IXUSR ? 's' : 'S')
		: (mode & S_IXUSR ? 'x' : '-'));
	str[4] = mode & S_IRGRP ? 'r' : '-';
	str[5] = mode & S_IWGRP ? 'w' : '-';
	str[6] = (mode & S_ISGID
		? (mode & S_IXGRP ? 's' : 'S')
		: (mode & S_IXGRP ? 'x' : '-'));
	str[7] = mode & S_IROTH ? 'r' : '-';
	str[8] = mode & S_IWOTH ? 'w' : '-';
	str[9] = (mode & S_ISVTX
		? (mode & S_IXOTH ? 't' : 'T')
		: (mode & S_IXOTH ? 'x' : '-'));
	str[10] = '\0';
}

/*
 * returns exponent (2^x=n) in range KiB..PiB
 */
static int get_exp(uint64_t n)
{
	int shft;

	for (shft = 10; shft <= 60; shft += 10) {
		if (n < (1ULL << shft))
			break;
	}
	return shft - 10;
}

char *size_to_human_string(int options, uint64_t bytes)
{
	char buf[32];
	int dec, exp;
	uint64_t frac;
	const char *letters = "BKMGTPE";
	char suffix[sizeof(" KiB")], *psuf = suffix;
	char c;

	if (options & SIZE_SUFFIX_SPACE)
		*psuf++ = ' ';

	exp  = get_exp(bytes);
	c    = *(letters + (exp ? exp / 10 : 0));
	dec  = exp ? bytes / (1ULL << exp) : bytes;
	frac = exp ? bytes % (1ULL << exp) : 0;

	*psuf++ = c;

	if ((options & SIZE_SUFFIX_3LETTER) && (c != 'B')) {
		*psuf++ = 'i';
		*psuf++ = 'B';
	}

	*psuf = '\0';

	/* fprintf(stderr, "exp: %d, unit: %c, dec: %d, frac: %jd\n",
	 *                 exp, suffix[0], dec, frac);
	 */

	if (frac) {
		/* round */
		frac = (frac / (1ULL << (exp - 10)) + 50) / 100;
		if (frac == 10)
			dec++, frac = 0;
	}

	if (frac) {
		struct lconv const *l = localeconv();
		char *dp = l ? l->decimal_point : NULL;

		if (!dp || !*dp)
			dp = ".";
		snprintf(buf, sizeof(buf), "%d%s%jd%s", dec, dp, frac, suffix);
	} else
		snprintf(buf, sizeof(buf), "%d%s", dec, suffix);

	return strdup(buf);
}

/*
 * Parses comma delimited list to array with IDs, for example:
 *
 * "aaa,bbb,ccc" --> ary[0] = FOO_AAA;
 *                   ary[1] = FOO_BBB;
 *                   ary[3] = FOO_CCC;
 *
 * The function name2id() provides conversion from string to ID.
 *
 * Returns: >= 0  : number of items added to ary[]
 *            -1  : parse error or unknown item
 *            -2  : arysz reached
 */
int string_to_idarray(const char *list, int ary[], size_t arysz,
			int (name2id)(const char *, size_t))
{
	const char *begin = NULL, *p;
	size_t n = 0;

	if (!list || !*list || !ary || !arysz || !name2id)
		return -1;

	for (p = list; p && *p; p++) {
		const char *end = NULL;
		int id;

		if (n >= arysz)
			return -2;
		if (!begin)
			begin = p;		/* begin of the column name */
		if (*p == ',')
			end = p;		/* terminate the name */
		if (*(p + 1) == '\0')
			end = p + 1;		/* end of string */
		if (!begin || !end)
			continue;
		if (end <= begin)
			return -1;

		id = name2id(begin, end - begin);
		if (id == -1)
			return -1;
		ary[ n++ ] = id;
		begin = NULL;
		if (end && !*end)
			break;
	}
	return n;
}

/*
 * Parses the array like string_to_idarray but if format is "+aaa,bbb"
 * it adds fields to array instead of replacing them.
 */
int string_add_to_idarray(const char *list, int ary[], size_t arysz,
			int *ary_pos, int (name2id)(const char *, size_t))
{
	const char *list_add;
	int r;

	if (!list || !*list || !ary_pos ||
	    *ary_pos < 0 || (size_t) *ary_pos > arysz)
		return -1;

	if (list[0] == '+')
		list_add = &list[1];
	else {
		list_add = list;
		*ary_pos = 0;
	}

	r = string_to_idarray(list_add, &ary[*ary_pos], arysz - *ary_pos, name2id);
	if (r > 0)
		*ary_pos += r;
	return r;
}

/*
 * LIST ::= <item> [, <item>]
 *
 * The <item> is translated to 'id' by name2id() function and the 'id' is used
 * as a position in the 'ary' bit array. It means that the 'id' has to be in
 * range <0..N> where N < sizeof(ary) * NBBY.
 *
 * Returns: 0 on success, <0 on error.
 */
int string_to_bitarray(const char *list,
		     char *ary,
		     int (*name2bit)(const char *, size_t))
{
	const char *begin = NULL, *p;

	if (!list || !name2bit || !ary)
		return -EINVAL;

	for (p = list; p && *p; p++) {
		const char *end = NULL;
		int bit;

		if (!begin)
			begin = p;		/* begin of the level name */
		if (*p == ',')
			end = p;		/* terminate the name */
		if (*(p + 1) == '\0')
			end = p + 1;		/* end of string */
		if (!begin || !end)
			continue;
		if (end <= begin)
			return -1;

		bit = name2bit(begin, end - begin);
		if (bit < 0)
			return bit;
		setbit(ary, bit);
		begin = NULL;
		if (end && !*end)
			break;
	}
	return 0;
}

/*
 * LIST ::= <item> [, <item>]
 *
 * The <item> is translated to 'id' by name2flag() function and the flags is
 * set to the 'mask'
*
 * Returns: 0 on success, <0 on error.
 */
int string_to_bitmask(const char *list,
		     unsigned long *mask,
		     long (*name2flag)(const char *, size_t))
{
	const char *begin = NULL, *p;

	if (!list || !name2flag || !mask)
		return -EINVAL;

	for (p = list; p && *p; p++) {
		const char *end = NULL;
		long flag;

		if (!begin)
			begin = p;		/* begin of the level name */
		if (*p == ',')
			end = p;		/* terminate the name */
		if (*(p + 1) == '\0')
			end = p + 1;		/* end of string */
		if (!begin || !end)
			continue;
		if (end <= begin)
			return -1;

		flag = name2flag(begin, end - begin);
		if (flag < 0)
			return flag;	/* error */
		*mask |= flag;
		begin = NULL;
		if (end && !*end)
			break;
	}
	return 0;
}

/*
 * Parse the lower and higher values in a string containing
 * "lower:higher" or "lower-higher" format. Note that either
 * the lower or the higher values may be missing, and the def
 * value will be assigned to it by default.
 *
 * Returns: 0 on success, <0 on error.
 */
int parse_range(const char *str, int *lower, int *upper, int def)
{
	char *end = NULL;

	if (!str)
		return 0;

	*upper = *lower = def;
	errno = 0;

	if (*str == ':') {				/* <:N> */
		str++;
		*upper = strtol(str, &end, 10);
		if (errno || !end || *end || end == str)
			return -1;
	} else {
		*upper = *lower = strtol(str, &end, 10);
		if (errno || !end || end == str)
			return -1;

		if (*end == ':' && !*(end + 1))		/* <M:> */
			*upper = 0;
		else if (*end == '-' || *end == ':') {	/* <M:N> <M-N> */
			str = end + 1;
			end = NULL;
			errno = 0;
			*upper = strtol(str, &end, 10);

			if (errno || !end || *end || end == str)
				return -1;
		}
	}
	return 0;
}

/*
 * Compare two strings for equality, ignoring at most one trailing
 * slash.
 */
int streq_except_trailing_slash(const char *s1, const char *s2)
{
	int equal;

	if (!s1 && !s2)
		return 1;
	if (!s1 || !s2)
		return 0;

	equal = !strcmp(s1, s2);

	if (!equal) {
		size_t len1 = strlen(s1);
		size_t len2 = strlen(s2);

		if (len1 && *(s1 + len1 - 1) == '/')
			len1--;
		if (len2 && *(s2 + len2 - 1) == '/')
			len2--;
		if (len1 != len2)
			return 0;

		equal = !strncmp(s1, s2, len1);
	}

	return equal;
}


#ifdef TEST_PROGRAM

int main(int argc, char *argv[])
{
	uintmax_t size = 0;
	char *hum, *hum2;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <number>[suffix]\n",	argv[0]);
		exit(EXIT_FAILURE);
	}

	if (strtosize(argv[1], &size))
		errx(EXIT_FAILURE, "invalid size '%s' value", argv[1]);

	hum = size_to_human_string(SIZE_SUFFIX_1LETTER, size);
	hum2 = size_to_human_string(SIZE_SUFFIX_3LETTER |
				    SIZE_SUFFIX_SPACE, size);

	printf("%25s : %20ju : %8s : %12s\n", argv[1], size, hum, hum2);
	free(hum);
	free(hum2);

	return EXIT_SUCCESS;
}
#endif /* TEST_PROGRAM */
