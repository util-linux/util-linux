#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/syscall.h>

#include "cpuset.h"

static inline int val_to_char(int v)
{
	if (v >= 0 && v < 10)
		return '0' + v;
	else if (v >= 10 && v < 16)
		return ('a' - 10) + v;
	else
		return -1;
}

/*
 * The following bitmask declarations, bitmask_*() routines, and associated
 * _setbit() and _getbit() routines are:
 * Copyright (c) 2004 Silicon Graphics, Inc. (SGI) All rights reserved.
 * SGI publishes it under the terms of the GNU General Public License, v2,
 * as published by the Free Software Foundation.
 */

static unsigned int _getbit(const struct bitmask *bmp, unsigned int n)
{
	if (n < bmp->size)
		return (bmp->maskp[n/bitsperlong] >> (n % bitsperlong)) & 1;
	else
		return 0;
}

static void _setbit(struct bitmask *bmp, unsigned int n, unsigned int v)
{
	if (n < bmp->size) {
		if (v)
			bmp->maskp[n/bitsperlong] |= 1UL << (n % bitsperlong);
		else
			bmp->maskp[n/bitsperlong] &= ~(1UL << (n % bitsperlong));
	}
}

static int bitmask_isbitset(const struct bitmask *bmp, unsigned int i)
{
	return _getbit(bmp, i);
}

struct bitmask *bitmask_clearall(struct bitmask *bmp)
{
	unsigned int i;
	for (i = 0; i < bmp->size; i++)
		_setbit(bmp, i, 0);
	return bmp;
}

struct bitmask *bitmask_setbit(struct bitmask *bmp, unsigned int i)
{
	_setbit(bmp, i, 1);
	return bmp;
}

unsigned int bitmask_nbytes(struct bitmask *bmp)
{
	return longsperbits(bmp->size) * sizeof(unsigned long);
}


struct bitmask *bitmask_alloc(unsigned int n)
{
	struct bitmask *bmp;

	bmp = malloc(sizeof(*bmp));
	if (!bmp)
		return 0;
	bmp->size = n;
	bmp->maskp = calloc(longsperbits(n), sizeof(unsigned long));
	if (!bmp->maskp) {
		free(bmp);
		return 0;
	}
	return bmp;
}

static inline int char_to_val(int c)
{
	int cl;

	cl = tolower(c);
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (cl >= 'a' && cl <= 'f')
		return cl + (10 - 'a');
	else
		return -1;
}

static const char *nexttoken(const char *q,  int sep)
{
	if (q)
		q = strchr(q, sep);
	if (q)
		q++;
	return q;
}

char *cpuset_to_cstr(struct bitmask *mask, char *str)
{
	int i;
	char *ptr = str;
	int entry_made = 0;

	for (i = 0; i < mask->size; i++) {
		if (bitmask_isbitset(mask, i)) {
			int j;
			int run = 0;
			entry_made = 1;
			for (j = i + 1; j < mask->size; j++) {
				if (bitmask_isbitset(mask, j))
					run++;
				else
					break;
			}
			if (!run)
				sprintf(ptr, "%d,", i);
			else if (run == 1) {
				sprintf(ptr, "%d,%d,", i, i + 1);
				i++;
			} else {
				sprintf(ptr, "%d-%d,", i, i + run);
				i += run;
			}
			while (*ptr != 0)
				ptr++;
		}
	}
	ptr -= entry_made;
	*ptr = 0;

	return str;
}

char *cpuset_to_str(struct bitmask *mask, char *str)
{
	int base;
	char *ptr = str;
	char *ret = 0;

	for (base = mask->size - 4; base >= 0; base -= 4) {
		char val = 0;
		if (bitmask_isbitset(mask, base))
			val |= 1;
		if (bitmask_isbitset(mask, base + 1))
			val |= 2;
		if (bitmask_isbitset(mask, base + 2))
			val |= 4;
		if (bitmask_isbitset(mask, base + 3))
			val |= 8;
		if (!ret && val)
			ret = ptr;
		*ptr++ = val_to_char(val);
	}
	*ptr = 0;
	return ret ? ret : ptr - 1;
}

int str_to_cpuset(struct bitmask *mask, const char* str)
{
	int len = strlen(str);
	const char *ptr = str + len - 1;
	int base = 0;

	/* skip 0x, it's all hex anyway */
	if (len > 1 && !memcmp(str, "0x", 2L))
		str += 2;

	bitmask_clearall(mask);
	while (ptr >= str) {
		char val = char_to_val(*ptr);
		if (val == (char) -1)
			return -1;
		if (val & 1)
			bitmask_setbit(mask, base);
		if (val & 2)
			bitmask_setbit(mask, base + 1);
		if (val & 4)
			bitmask_setbit(mask, base + 2);
		if (val & 8)
			bitmask_setbit(mask, base + 3);
		len--;
		ptr--;
		base += 4;
	}

	return 0;
}

int cstr_to_cpuset(struct bitmask *mask, const char* str)
{
	const char *p, *q;
	q = str;
	bitmask_clearall(mask);

	while (p = q, q = nexttoken(q, ','), p) {
		unsigned int a;	/* beginning of range */
		unsigned int b;	/* end of range */
		unsigned int s;	/* stride */
		const char *c1, *c2;

		if (sscanf(p, "%u", &a) < 1)
			return 1;
		b = a;
		s = 1;

		c1 = nexttoken(p, '-');
		c2 = nexttoken(p, ',');
		if (c1 != NULL && (c2 == NULL || c1 < c2)) {
			if (sscanf(c1, "%u", &b) < 1)
				return 1;
			c1 = nexttoken(c1, ':');
			if (c1 != NULL && (c2 == NULL || c1 < c2))
				if (sscanf(c1, "%u", &s) < 1) {
					return 1;
			}
		}

		if (!(a <= b))
			return 1;
		while (a <= b) {
			bitmask_setbit(mask, a);
			a += s;
		}
	}

	return 0;
}

#ifdef TEST_PROGRAM

#include <err.h>
#include <getopt.h>

int main(int argc, char *argv[])
{
	struct bitmask *set;
	char *buf, *mask = NULL, *range = NULL;
	int ncpus = 2048, rc, c;

	struct option longopts[] = {
	    { "ncpus", 1, 0, 'n' },
	    { "mask",  1, 0, 'm' },
	    { "range", 1, 0, 'r' },
	    { NULL,    0, 0, 0 }
	};

	while ((c = getopt_long(argc, argv, "n:m:r:", longopts, NULL)) != -1) {
		switch(c) {
		case 'n':
			ncpus = atoi(optarg);
			break;
		case 'm':
			mask = strdup(optarg);
			break;
		case 'r':
			range = strdup(optarg);
			break;
		default:
			goto usage_err;
		}
	}

	if (!mask && !range)
		goto usage_err;

	set = bitmask_alloc(ncpus);
	if (!set)
		err(EXIT_FAILURE, "failed to allocate cpu set");

	buf = malloc(7 * ncpus);
	if (!buf)
		err(EXIT_FAILURE, "failed to allocate cpu set buffer");

	if (mask)
		rc = str_to_cpuset(set, mask);
	else
		rc = cstr_to_cpuset(set, range);

	if (rc)
		errx(EXIT_FAILURE, "failed to parse string: %s", mask ? : range);

	printf("%-15s = %15s ", mask ? : range,	cpuset_to_str(set, buf));
	printf("[%s]\n", cpuset_to_cstr(set, buf));

	free(buf);
	free(set->maskp);
	free(set);

	return EXIT_SUCCESS;

usage_err:
	fprintf(stderr,
		"usage: %s [--ncpus <num>] --mask <mask> | --range <list>",
		program_invocation_short_name);
	exit(EXIT_FAILURE);
}
#endif
