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

static int STRTOXX_EXIT_CODE = EXIT_FAILURE;

void strutils_set_exitcode(int ex) {
	STRTOXX_EXIT_CODE = ex;
}

static int do_scale_by_power (uintmax_t *x, int base, int power)
{
	while (power--) {
		if (UINTMAX_MAX / base < *x)
			return -ERANGE;
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
 * The optional 'power' variable returns number associated with used suffix
 * {K,M,G,T,P,E,Z,Y}  = {1,2,3,4,5,6,7,8}.
 *
 * The function also supports decimal point, for example:
 *              0.5MB   = 500000
 *              0.5MiB  = 512000
 *
 * Note that the function does not accept numbers with '-' (negative sign)
 * prefix.
 */
int parse_size(const char *str, uintmax_t *res, int *power)
{
	const char *p;
	char *end;
	uintmax_t x, frac = 0;
	int base = 1024, rc = 0, pwr = 0, frac_zeros = 0;

	static const char *suf  = "KMGTPEZY";
	static const char *suf2 = "kmgtpezy";
	const char *sp;

	*res = 0;

	if (!str || !*str) {
		rc = -EINVAL;
		goto err;
	}

	/* Only positive numbers are acceptable
	 *
	 * Note that this check is not perfect, it would be better to
	 * use lconv->negative_sign. But coreutils use the same solution,
	 * so it's probably good enough...
	 */
	p = str;
	while (isspace((unsigned char) *p))
		p++;
	if (*p == '-') {
		rc = -EINVAL;
		goto err;
	}

	errno = 0, end = NULL;
	x = strtoumax(str, &end, 0);

	if (end == str ||
	    (errno != 0 && (x == UINTMAX_MAX || x == 0))) {
		rc = errno ? -errno : -EINVAL;
		goto err;
	}
	if (!end || !*end)
		goto done;			/* without suffix */
	p = end;

	/*
	 * Check size suffixes
	 */
check_suffix:
	if (*(p + 1) == 'i' && (*(p + 2) == 'B' || *(p + 2) == 'b') && !*(p + 3))
		base = 1024;			/* XiB, 2^N */
	else if ((*(p + 1) == 'B' || *(p + 1) == 'b') && !*(p + 2))
		base = 1000;			/* XB, 10^N */
	else if (*(p + 1)) {
		struct lconv const *l = localeconv();
		const char *dp = l ? l->decimal_point : NULL;
		size_t dpsz = dp ? strlen(dp) : 0;

		if (frac == 0 && *p && dp && strncmp(dp, p, dpsz) == 0) {
			const char *fstr = p + dpsz;

			for (p = fstr; *p == '0'; p++)
				frac_zeros++;
			errno = 0, end = NULL;
			frac = strtoumax(fstr, &end, 0);
			if (end == fstr ||
			    (errno != 0 && (frac == UINTMAX_MAX || frac == 0))) {
				rc = errno ? -errno : -EINVAL;
				goto err;
			}
			if (frac && (!end  || !*end)) {
				rc = -EINVAL;
				goto err;		/* without suffix, but with frac */
			}
			p = end;
			goto check_suffix;
		}
		rc = -EINVAL;
		goto err;			/* unexpected suffix */
	}

	sp = strchr(suf, *p);
	if (sp)
		pwr = (sp - suf) + 1;
	else {
		sp = strchr(suf2, *p);
		if (sp)
			pwr = (sp - suf2) + 1;
		else {
			rc = -EINVAL;
			goto err;
		}
	}

	rc = do_scale_by_power(&x, base, pwr);
	if (power)
		*power = pwr;
	if (frac && pwr) {
		int zeros_in_pwr = frac_zeros % 3;
		int frac_pwr = pwr - (frac_zeros / 3) - 1;
		uintmax_t y = frac * (zeros_in_pwr == 0 ? 100 :
				      zeros_in_pwr == 1 ?  10 : 1);

		if (frac_pwr < 0) {
			rc = -EINVAL;
			goto err;
		}
		do_scale_by_power(&y, base, frac_pwr);
		x += y;
	}
done:
	*res = x;
err:
	if (rc < 0)
		errno = -rc;
	return rc;
}

int strtosize(const char *str, uintmax_t *res)
{
	return parse_size(str, res, NULL);
}

int isdigit_strend(const char *str, const char **end)
{
	const char *p;

	for (p = str; p && *p && isdigit((unsigned char) *p); p++);

	if (end)
		*end = p;
	return p && p > str && !*p;
}

int isxdigit_strend(const char *str, const char **end)
{
	const char *p;

	for (p = str; p && *p && isxdigit((unsigned char) *p); p++);

	if (end)
		*end = p;

	return p && p > str && !*p;
}

/*
 *  parse_switch(argv[i], "on", "off",  "yes", "no",  NULL);
 */
int parse_switch(const char *arg, const char *errmesg, ...)
{
	const char *a, *b;
	va_list ap;

	va_start(ap, errmesg);
	do {
		a = va_arg(ap, char *);
		if (!a)
			break;
		b = va_arg(ap, char *);
		if (!b)
			break;

		if (strcmp(arg, a) == 0) {
			va_end(ap);
			return 1;
		} else if (strcmp(arg, b) == 0) {
			va_end(ap);
			return 0;
		}
	} while (1);
	va_end(ap);

	errx(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, arg);
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
        size_t i;

        for (i = 0; i < maxlen; i++) {
                if (s[i] == '\0')
                        return i;
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
	char *new = malloc((len + 1) * sizeof(char));
	if (!new)
		return NULL;
	new[len] = '\0';
	return (char *) memcpy(new, s, len);
}
#endif

static uint32_t _strtou32_or_err(const char *str, const char *errmesg, int base);
static uint64_t _strtou64_or_err(const char *str, const char *errmesg, int base);

int16_t strtos16_or_err(const char *str, const char *errmesg)
{
	int32_t num = strtos32_or_err(str, errmesg);

	if (num < INT16_MIN || num > INT16_MAX) {
		errno = ERANGE;
		err(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);
	}
	return num;
}

static uint16_t _strtou16_or_err(const char *str, const char *errmesg, int base)
{
	uint32_t num = _strtou32_or_err(str, errmesg, base);

	if (num > UINT16_MAX) {
		errno = ERANGE;
		err(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);
	}
	return num;
}

uint16_t strtou16_or_err(const char *str, const char *errmesg)
{
	return _strtou16_or_err(str, errmesg, 10);
}

uint16_t strtox16_or_err(const char *str, const char *errmesg)
{
	return _strtou16_or_err(str, errmesg, 16);
}

int32_t strtos32_or_err(const char *str, const char *errmesg)
{
	int64_t num = strtos64_or_err(str, errmesg);

	if (num < INT32_MIN || num > INT32_MAX) {
		errno = ERANGE;
		err(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);
	}
	return num;
}

static uint32_t _strtou32_or_err(const char *str, const char *errmesg, int base)
{
	uint64_t num = _strtou64_or_err(str, errmesg, base);

	if (num > UINT32_MAX) {
		errno = ERANGE;
		err(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);
	}
	return num;
}

uint32_t strtou32_or_err(const char *str, const char *errmesg)
{
	return _strtou32_or_err(str, errmesg, 10);
}

uint32_t strtox32_or_err(const char *str, const char *errmesg)
{
	return _strtou32_or_err(str, errmesg, 16);
}

int64_t strtos64_or_err(const char *str, const char *errmesg)
{
	int64_t num;
	char *end = NULL;

	errno = 0;
	if (str == NULL || *str == '\0')
		goto err;
	num = strtoimax(str, &end, 10);

	if (errno || str == end || (end && *end))
		goto err;

	return num;
err:
	if (errno == ERANGE)
		err(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);

	errx(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);
}

static uint64_t _strtou64_or_err(const char *str, const char *errmesg, int base)
{
	uintmax_t num;
	char *end = NULL;

	errno = 0;
	if (str == NULL || *str == '\0')
		goto err;
	num = strtoumax(str, &end, base);

	if (errno || str == end || (end && *end))
		goto err;

	return num;
err:
	if (errno == ERANGE)
		err(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);

	errx(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);
}

uint64_t strtou64_or_err(const char *str, const char *errmesg)
{
	return _strtou64_or_err(str, errmesg, 10);
}

uint64_t strtox64_or_err(const char *str, const char *errmesg)
{
	return _strtou64_or_err(str, errmesg, 16);
}

double strtod_or_err(const char *str, const char *errmesg)
{
	double num;
	char *end = NULL;

	errno = 0;
	if (str == NULL || *str == '\0')
		goto err;
	num = strtod(str, &end);

	if (errno || str == end || (end && *end))
		goto err;

	return num;
err:
	if (errno == ERANGE)
		err(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);

	errx(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);
}

long strtol_or_err(const char *str, const char *errmesg)
{
	long num;
	char *end = NULL;

	errno = 0;
	if (str == NULL || *str == '\0')
		goto err;
	num = strtol(str, &end, 10);

	if (errno || str == end || (end && *end))
		goto err;

	return num;
err:
	if (errno == ERANGE)
		err(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);

	errx(STRTOXX_EXIT_CODE, "%s: '%s'", errmesg, str);
}

unsigned long strtoul_or_err(const char *str, const char *errmesg)
{
	unsigned long num;
	char *end = NULL;

	errno = 0;
	if (str == NULL || *str == '\0')
		goto err;
	num = strtoul(str, &end, 10);

	if (errno || str == end || (end && *end))
		goto err;

	return num;
err:
	if (errno == ERANGE)
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
 * be 11 bytes.
 */
char *xstrmode(mode_t mode, char *str)
{
	unsigned short i = 0;

	if (S_ISDIR(mode))
		str[i++] = 'd';
	else if (S_ISLNK(mode))
		str[i++] = 'l';
	else if (S_ISCHR(mode))
		str[i++] = 'c';
	else if (S_ISBLK(mode))
		str[i++] = 'b';
	else if (S_ISSOCK(mode))
		str[i++] = 's';
	else if (S_ISFIFO(mode))
		str[i++] = 'p';
	else if (S_ISREG(mode))
		str[i++] = '-';

	str[i++] = mode & S_IRUSR ? 'r' : '-';
	str[i++] = mode & S_IWUSR ? 'w' : '-';
	str[i++] = (mode & S_ISUID
		? (mode & S_IXUSR ? 's' : 'S')
		: (mode & S_IXUSR ? 'x' : '-'));
	str[i++] = mode & S_IRGRP ? 'r' : '-';
	str[i++] = mode & S_IWGRP ? 'w' : '-';
	str[i++] = (mode & S_ISGID
		? (mode & S_IXGRP ? 's' : 'S')
		: (mode & S_IXGRP ? 'x' : '-'));
	str[i++] = mode & S_IROTH ? 'r' : '-';
	str[i++] = mode & S_IWOTH ? 'w' : '-';
	str[i++] = (mode & S_ISVTX
		? (mode & S_IXOTH ? 't' : 'T')
		: (mode & S_IXOTH ? 'x' : '-'));
	str[i] = '\0';

	return str;
}

/*
 * returns exponent (2^x=n) in range KiB..EiB (2^10..2^60)
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
		snprintf(buf, sizeof(buf), "%d%s%" PRIu64 "%s", dec, dp, frac, suffix);
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
			size_t *ary_pos, int (name2id)(const char *, size_t))
{
	const char *list_add;
	int r;

	if (!list || !*list || !ary_pos || *ary_pos > arysz)
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
			*upper = def;
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

static const char *next_path_segment(const char *str, size_t *sz)
{
	const char *start, *p;

	start = str;
	*sz = 0;
	while (start && *start == '/' && *(start + 1) == '/')
		start++;

	if (!start || !*start)
		return NULL;

	for (*sz = 1, p = start + 1; *p && *p != '/'; p++) {
		(*sz)++;
	}

	return start;
}

int streq_paths(const char *a, const char *b)
{
	while (a && b) {
		size_t a_sz, b_sz;
		const char *a_seg = next_path_segment(a, &a_sz);
		const char *b_seg = next_path_segment(b, &b_sz);

		/*
		fprintf(stderr, "A>>>(%zu) '%s'\n", a_sz, a_seg);
		fprintf(stderr, "B>>>(%zu) '%s'\n", b_sz, b_seg);
		*/

		/* end of the path */
		if (a_sz + b_sz == 0)
			return 1;

		/* ignore tailing slash */
		if (a_sz + b_sz == 1 &&
		    ((a_seg && *a_seg == '/') || (b_seg && *b_seg == '/')))
			return 1;

		if (a_sz != b_sz || strncmp(a_seg, b_seg, a_sz) != 0)
			return 0;

		a = a_seg + a_sz;
		b = b_seg + b_sz;
	};

	return 0;
}

char *strnappend(const char *s, const char *suffix, size_t b)
{
        size_t a;
        char *r;

        if (!s && !suffix)
                return strdup("");
        if (!s)
                return strndup(suffix, b);
        if (!suffix)
                return strdup(s);

        assert(s);
        assert(suffix);

        a = strlen(s);
        if (b > ((size_t) -1) - a)
                return NULL;

        r = malloc(a + b + 1);
        if (!r)
                return NULL;

        memcpy(r, s, a);
        memcpy(r + a, suffix, b);
        r[a+b] = 0;

        return r;
}

char *strappend(const char *s, const char *suffix)
{
        return strnappend(s, suffix, suffix ? strlen(suffix) : 0);
}

char *strfappend(const char *s, const char *format, ...)
{
	va_list ap;
	char *val, *res;
	int sz;

	va_start(ap, format);
	sz = vasprintf(&val, format, ap);
	va_end(ap);

	if (sz < 0)
		return NULL;

	res = strnappend(s, val, sz);
	free(val);
	return res;
}

static size_t strcspn_escaped(const char *s, const char *reject)
{
        int escaped = 0;
        int n;

        for (n=0; s[n]; n++) {
                if (escaped)
                        escaped = 0;
                else if (s[n] == '\\')
                        escaped = 1;
                else if (strchr(reject, s[n]))
                        break;
        }

        /* if s ends in \, return index of previous char */
        return n - escaped;
}

/* Split a string into words. */
const char *split(const char **state, size_t *l, const char *separator, int quoted)
{
        const char *current;

        current = *state;

        if (!*current) {
                assert(**state == '\0');
                return NULL;
        }

        current += strspn(current, separator);
        if (!*current) {
                *state = current;
                return NULL;
        }

        if (quoted && strchr("\'\"", *current)) {
                char quotechars[2] = {*current, '\0'};

                *l = strcspn_escaped(current + 1, quotechars);
                if (current[*l + 1] == '\0' || current[*l + 1] != quotechars[0] ||
                    (current[*l + 2] && !strchr(separator, current[*l + 2]))) {
                        /* right quote missing or garbage at the end */
                        *state = current;
                        return NULL;
                }
                *state = current++ + *l + 2;
        } else if (quoted) {
                *l = strcspn_escaped(current, separator);
                if (current[*l] && !strchr(separator, current[*l])) {
                        /* unfinished escape */
                        *state = current;
                        return NULL;
                }
                *state = current + *l;
        } else {
                *l = strcspn(current, separator);
                *state = current + *l;
        }

        return current;
}

/* Rewind file pointer forward to new line.  */
int skip_fline(FILE *fp)
{
	int ch;

	do {
		if ((ch = fgetc(fp)) == EOF)
			return 1;
		if (ch == '\n')
			return 0;
	} while (1);
}

#ifdef TEST_PROGRAM_STRUTILS

static int test_strutils_sizes(int argc, char *argv[])
{
	uintmax_t size = 0;
	char *hum, *hum2;

	if (argc < 2)
		return EXIT_FAILURE;

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

static int test_strutils_cmp_paths(int argc, char *argv[])
{
	int rc = streq_paths(argv[1], argv[2]);

	if (argc < 3)
		return EXIT_FAILURE;

	printf("%s: '%s' '%s'\n", rc == 1 ? "YES" : "NOT", argv[1], argv[2]);
	return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
	if (argc == 3 && strcmp(argv[1], "--size") == 0)
		return test_strutils_sizes(argc - 1, argv + 1);

	else if (argc == 4 && strcmp(argv[1], "--cmp-paths") == 0)
		return test_strutils_cmp_paths(argc - 1, argv + 1);

	else {
		fprintf(stderr, "usage: %1$s --size <number>[suffix]\n"
				"       %1$s --cmp-paths <path> <path>\n",
				argv[0]);
		exit(EXIT_FAILURE);
	}

	return EXIT_FAILURE;
}
#endif /* TEST_PROGRAM_STRUTILS */
