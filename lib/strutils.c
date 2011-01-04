/*
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 * Copyright (C) 2010 Davidlohr Bueso <dave@gnu.org>
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#include <errno.h>
#include <err.h>
#include <sys/stat.h>
#include <locale.h>
#include <string.h>

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
 *     where X = {K,M,G,T,P,E,Y,Z}
 *        or X = {k,m,g,t,p,e}  (undocumented for backward compatibility only)
 * for example:
 *		10KiB	= 10240
 *		10K	= 10240
 *
 * XB for 10^N
 *     where X = {K,M,G,T,P,E,Y,Z}
 * for example:
 *		10KB	= 10000
 *
 * Note that the function does not accept numbers with '-' (negative sign)
 * prefix.
 */
int strtosize(const char *str, uintmax_t *res)
{
	char *p;
	uintmax_t x;
	int base = 1024, rc = 0;

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

	switch(*p) {
	case 'K':
	case 'k':
		rc = do_scale_by_power(&x, base, 1);
		break;
	case 'M':
	case 'm':
		rc = do_scale_by_power(&x, base, 2);
		break;
	case 'G':
	case 'g':
		rc = do_scale_by_power(&x, base, 3);
		break;
	case 'T':
	case 't':
		rc = do_scale_by_power(&x, base, 4);
		break;
	case 'P':
	case 'p':
		rc = do_scale_by_power(&x, base, 5);
		break;
	case 'E':
	case 'e':
		rc = do_scale_by_power(&x, base, 6);
		break;
	case 'Z':
		rc = do_scale_by_power(&x, base, 7);
		break;
	case 'Y':
		rc = do_scale_by_power(&x, base, 8);
		break;
	default:
		goto err;
	}

done:
	*res = x;
	return rc;
err:
	return -1;
}

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

/*
 * same as strtol(3) but exit on failure instead of returning crap
 */
long strtol_or_err(const char *str, const char *errmesg)
{
       long num;
       char *end = NULL;

       if (str == NULL || *str == '\0')
               goto err;
       errno = 0;
       num = strtol(str, &end, 10);

       if (errno || (end && *end))
               goto err;

       return num;
err:
       if (errno)
               err(EXIT_FAILURE, "%s: '%s'", errmesg, str);
       else
               errx(EXIT_FAILURE, "%s: '%s'", errmesg, str);
       return 0;
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

char *size_to_human_string(uint64_t bytes)
{
	char buf[32];
	int dec, frac, exp;
	const char *letters = "BKMGTP";
	char c;

	exp  = get_exp(bytes);
	c    = *(letters + (exp ? exp / 10 : 0));
	dec  = exp ? bytes / (1ULL << exp) : bytes;
	frac = exp ? bytes % (1ULL << exp) : 0;

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
		snprintf(buf, sizeof(buf), "%d%s%d%c", dec, dp, frac, c);
	} else
		snprintf(buf, sizeof(buf), "%d%c", dec, c);

	return strdup(buf);
}


#ifdef TEST_PROGRAM

int main(int argc, char *argv[])
{
	uintmax_t size = 0;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <number>[suffix]\n",	argv[0]);
		exit(EXIT_FAILURE);
	}

	if (strtosize(argv[1], &size))
		errx(EXIT_FAILURE, "invalid size '%s' value", argv[1]);

	printf("%25s : %20ju\n", argv[1], size);
	return EXIT_FAILURE;
}
#endif /* TEST_PROGRAM */
