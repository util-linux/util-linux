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
 *
 * Returns 0 on success, -1 in case of error, -2 in case of overflow.
 *
 * Copyright (C) 2010 Karel Zak <kzak@redhat.com>
 */
#include <stdio.h>
#include <inttypes.h>
#include <ctype.h>
#include <errno.h>

static int do_scale_by_power (uintmax_t *x, int base, int power)
{
	while (power--) {
		if (UINTMAX_MAX / base < *x)
			return -2;
		*x *= base;
	}
	return 0;
}

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

#ifdef TEST_PROGRAM

#include <stdio.h>
#include <stdlib.h>
#include <err.h>

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

