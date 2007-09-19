/*
 * taskset.c - taskset
 * Command-line utility for setting and retrieving a task's CPU affinity
 *
 * Robert Love <rml@tech9.net>		25 April 2002
 *
 * Linux kernels as of 2.5.8 provide the needed syscalls for
 * working with a task's cpu affinity.  Currently 2.4 does not
 * support these syscalls, but patches are available at:
 *
 * 	http://www.kernel.org/pub/linux/kernel/people/rml/cpu-affinity/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, v2, as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Copyright (C) 2004 Robert Love
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/syscall.h>

struct bitmask {
	unsigned int size;
	unsigned long *maskp;
};

static void show_usage(const char *cmd)
{
	fprintf(stderr, "taskset (%s)\n", PACKAGE_STRING);
	fprintf(stderr, "usage: %s [options] [mask | cpu-list] [pid |"\
		" cmd [args...]]\n", cmd);
	fprintf(stderr, "set or get the affinity of a process\n\n");
	fprintf(stderr, "  -p, --pid                  "
		"operate on existing given pid\n");
        fprintf(stderr, "  -c, --cpu-list             "\
		"display and specify cpus in list format\n");
	fprintf(stderr, "  -h, --help                 display this help\n");
	fprintf(stderr, "  -V, --version              "\
		"output version information\n\n");
	fprintf(stderr, "The default behavior is to run a new command:\n");
	fprintf(stderr, "  %s 03 sshd -b 1024\n", cmd);
	fprintf(stderr, "You can retrieve the mask of an existing task:\n");
	fprintf(stderr, "  %s -p 700\n", cmd);
	fprintf(stderr, "Or set it:\n");
	fprintf(stderr, "  %s -p 03 700\n", cmd);
	fprintf(stderr, "List format uses a comma-separated list instead"\
			" of a mask:\n");
	fprintf(stderr, "  %s -pc 0,3,7-11 700\n", cmd);
	fprintf(stderr, "Ranges in list format can take a stride argument:\n");
	fprintf(stderr, "  e.g. 0-31:2 is equivalent to mask 0x55555555\n\n");
}

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
#define howmany(x,y) (((x)+((y)-1))/(y))
#define bitsperlong (8 * sizeof(unsigned long))
#define longsperbits(n) howmany(n, bitsperlong)
#define bytesperbits(x) ((x+7)/8)

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

int bitmask_isbitset(const struct bitmask *bmp, unsigned int i)
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

static char * cpuset_to_str(struct bitmask *mask, char *str)
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

static char * cpuset_to_cstr(struct bitmask *mask, char *str)
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

static int str_to_cpuset(struct bitmask *mask, const char* str)
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

static const char *nexttoken(const char *q,  int sep)
{
	if (q)
		q = strchr(q, sep);
	if (q)
		q++;
	return q;
}

static int cstr_to_cpuset(struct bitmask *mask, const char* str)
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

/*
 * Number of bits in a CPU bitmask on current system
 */
static int
max_number_of_cpus(void)
{
	int n;
	int cpus = 2048;

	for (;;) {
		unsigned long buffer[longsperbits(cpus)];
		memset(buffer, 0, sizeof(buffer));
		/* the library version does not return size of cpumask_t */
		n = syscall(SYS_sched_getaffinity, 0, bytesperbits(cpus),
								&buffer);
		if (n < 0 && errno == EINVAL && cpus < 1024*1024) {
			cpus *= 2;
			continue;
		}
		return n*8;
	}
	fprintf (stderr, "cannot determine NR_CPUS; aborting");
	exit(1);
	return 0;
}

int main(int argc, char *argv[])
{
	struct bitmask *new_mask, *cur_mask;
	pid_t pid = 0;
	int opt, err;
	char *mstr;
	char *cstr;
	int c_opt = 0;
        unsigned int cpus_configured;
	int new_mask_byte_size, cur_mask_byte_size;

	struct option longopts[] = {
		{ "pid",	0, NULL, 'p' },
		{ "cpu-list",	0, NULL, 'c' },
		{ "help",	0, NULL, 'h' },
		{ "version",	0, NULL, 'V' },
		{ NULL,		0, NULL, 0 }
	};

	while ((opt = getopt_long(argc, argv, "+pchV", longopts, NULL)) != -1) {
		int ret = 1;

		switch (opt) {
		case 'p':
			pid = atoi(argv[argc - 1]);
			break;
		case 'c':
			c_opt = 1;
			break;
		case 'V':
			printf("taskset (%s)\n", PACKAGE_STRING);
			return 0;
		case 'h':
			ret = 0;
		default:
			show_usage(argv[0]);
			return ret;
		}
	}

	if ((!pid && argc - optind < 2)
			|| (pid && (argc - optind < 1 || argc - optind > 2))) {
		show_usage(argv[0]);
		return 1;
	}

	cpus_configured = max_number_of_cpus();

	/*
	 * cur_mask is always used for the sched_getaffinity call
	 * On the sched_getaffinity the kernel demands a user mask of
	 * at least the size of its own cpumask_t.
	 */
	cur_mask = bitmask_alloc(cpus_configured);
	if (!cur_mask) {
		fprintf (stderr, "bitmask_alloc failed\n");
		exit(1);
	}
	cur_mask_byte_size = bitmask_nbytes(cur_mask);
	mstr = malloc(1 + cur_mask->size / 4);
	cstr = malloc(7 * cur_mask->size);

	/*
	 * new_mask is always used for the sched_setaffinity call
	 * On the sched_setaffinity the kernel will zero-fill its
	 * cpumask_t if the user's mask is shorter.
	 */
	new_mask = bitmask_alloc(cpus_configured);
	if (!new_mask) {
		fprintf (stderr, "bitmask_alloc failed\n");
		exit(1);
	}
	new_mask_byte_size = bitmask_nbytes(new_mask);

	if (pid) {
		if (sched_getaffinity(pid, cur_mask_byte_size,
					(cpu_set_t *)cur_mask->maskp) < 0) {
			perror("sched_getaffinity");
			fprintf(stderr, "failed to get pid %d's affinity\n",
				pid);
			return 1;
		}
		if (c_opt)
			printf("pid %d's current affinity list: %s\n", pid,
				cpuset_to_cstr(cur_mask, cstr));
		else
			printf("pid %d's current affinity mask: %s\n", pid,
				cpuset_to_str(cur_mask, mstr));

		if (argc - optind == 1)
			return 0;
	}

	if (c_opt)
		err = cstr_to_cpuset(new_mask, argv[optind]);
	else
		err = str_to_cpuset(new_mask, argv[optind]);

	if (err) {
		if (c_opt)
			fprintf(stderr, "failed to parse CPU list %s\n",
				argv[optind]);
		else
			fprintf(stderr, "failed to parse CPU mask %s\n",
				argv[optind]);
		return 1;
	}

	if (sched_setaffinity(pid, new_mask_byte_size,
					(cpu_set_t *) new_mask->maskp) < 0) {
		perror("sched_setaffinity");
		fprintf(stderr, "failed to set pid %d's affinity.\n", pid);
		return 1;
	}

	if (sched_getaffinity(pid, cur_mask_byte_size,
					(cpu_set_t *)cur_mask->maskp) < 0) {
		perror("sched_getaffinity");
		fprintf(stderr, "failed to get pid %d's affinity.\n", pid);
		return 1;
	}

	if (pid) {
		if (c_opt)
			printf("pid %d's new affinity list: %s\n", pid, 
		               cpuset_to_cstr(cur_mask, cstr));
		else
			printf("pid %d's new affinity mask: %s\n", pid, 
		               cpuset_to_str(cur_mask, mstr));
	} else {
		argv += optind + 1;
		execvp(argv[0], argv);
		perror("execvp");
		fprintf(stderr, "failed to execute %s\n", argv[0]);
		return 1;
	}

	return 0;
}
