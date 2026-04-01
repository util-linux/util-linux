/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * General purpose random utilities. Based on libuuid code.
 *
 * This code is free software; you can redistribute it and/or modify it under
 * the terms of the Modified BSD License. The complete text of the license is
 * available in the Documentation/licenses/COPYING.BSD-3-Clause file.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#ifdef HAVE_SYS_SYSCALL_H
#include <sys/syscall.h>
#endif
#include "c.h"
#include "randutils.h"
#include "nls.h"


#ifdef HAVE_GETRANDOM
# include <sys/random.h>
#elif defined (__linux__)
# if !defined(SYS_getrandom) && defined(__NR_getrandom)
   /* usable kernel-headers, but old glibc-headers */
#  define SYS_getrandom __NR_getrandom
# endif
#endif

#if !defined(HAVE_GETRANDOM) && defined(SYS_getrandom)
/* libc without function, but we have syscall */
#define GRND_NONBLOCK 0x01
#define GRND_RANDOM 0x02
static int getrandom(void *buf, size_t buflen, unsigned int flags)
{
	return (syscall(SYS_getrandom, buf, buflen, flags));
}
# define HAVE_GETRANDOM
#endif

#ifdef HAVE_SRANDOM
#define srand(x)	srandom(x)
#define rand()		random()
#endif

#if defined(__linux__) && defined(__NR_gettid) && defined(HAVE_JRAND48)
#define DO_JRAND_MIX
THREAD_LOCAL unsigned short ul_jrand_seed[3];
#endif

static void crank_random(void)
{
	int i;
	struct timeval tv;
	unsigned int n_pid, n_uid;

	gettimeofday(&tv, NULL);
	n_pid = getpid();
	n_uid = getuid();
	srand((n_pid << 16) ^ n_uid ^ tv.tv_sec ^ tv.tv_usec);

#ifdef DO_JRAND_MIX
	ul_jrand_seed[0] = getpid() ^ (tv.tv_sec & 0xFFFF);
	ul_jrand_seed[1] = getppid() ^ (tv.tv_usec & 0xFFFF);
	ul_jrand_seed[2] = (tv.tv_sec ^ tv.tv_usec) >> 16;
#endif
	/* Crank the random number generator a few times */
	gettimeofday(&tv, NULL);
	for (i = (tv.tv_sec ^ tv.tv_usec) & 0x1F; i > 0; i--)
		rand();
}

static int random_get_fd(ul_random_src_t *src)
{
	int fd;

	*src = URANDOM;
	fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd == -1) {
		*src = RANDOM;
		fd = open("/dev/random", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	}
	crank_random();
	return fd;
}

/*
 * Generate a stream of random nbytes into buf.
 * Use /dev/urandom if possible, and if not,
 * use glibc pseudo-random functions.
 */
#define UL_RAND_READ_ATTEMPTS	8
#define UL_RAND_READ_DELAY	125000	/* microseconds */

/*
 * Write @nbytes random bytes into @buf.
 *
 * Returns 0 for good quality of random bytes or 1 for weak quality.
 */
ul_random_src_t ul_random_get_bytes(void *buf, size_t nbytes)
{
	unsigned char *cp = (unsigned char *)buf;
	size_t i, n = nbytes;
	ul_random_src_t src;
	int lose_counter = 0;

#ifdef HAVE_GETRANDOM
	while (n > 0) {
		int x;

		errno = 0;
		x = getrandom(cp, n, GRND_NONBLOCK);
		if (x > 0) {			/* success */
		       n -= x;
		       cp += x;
		       lose_counter = 0;
		       errno = 0;
		} else if (errno == ENOSYS) {	/* kernel without getrandom() */
			break;

		} else if (errno == EAGAIN && lose_counter < UL_RAND_READ_ATTEMPTS) {
			xusleep(UL_RAND_READ_DELAY);	/* no entropy, wait and try again */
			lose_counter++;
		} else
			break;
	}
	if (n == 0)
		return GETRANDOM;

	if (errno == ENOSYS)
	/*
	 * We've been built against headers that support getrandom, but the
	 * running kernel does not.  Fallback to reading from /dev/{u,}random
	 * as before
	 */
#endif
	{
		int fd = random_get_fd(&src);

		lose_counter = 0;
		if (fd >= 0) {
			while (n > 0) {
				ssize_t x = read(fd, cp, n);
				if (x <= 0) {
					if (lose_counter++ > UL_RAND_READ_ATTEMPTS)
						break;
					xusleep(UL_RAND_READ_DELAY);
					continue;
				}
				n -= x;
				cp += x;
				lose_counter = 0;
			}

			close(fd);
		}
		if (n == 0)
			return src;
	}
	/*
	 * We do this all the time, but this is the only source of
	 * randomness if /dev/random/urandom is out to lunch.
	 */
	crank_random();
	for (cp = buf, i = 0; i < nbytes; i++)
		*cp++ ^= (rand() >> 7) & 0xFF;

#ifdef DO_JRAND_MIX
	{
		unsigned short tmp_seed[3];

		memcpy(tmp_seed, ul_jrand_seed, sizeof(tmp_seed));
		ul_jrand_seed[2] = ul_jrand_seed[2] ^ syscall(__NR_gettid);
		for (cp = buf, i = 0; i < nbytes; i++)
			*cp++ ^= (jrand48(tmp_seed) >> 7) & 0xFF;
		memcpy(ul_jrand_seed, tmp_seed,
		       sizeof(ul_jrand_seed)-sizeof(unsigned short));
	}
#endif

	return WEAK;
}


/*
 * Tell source of randomness.
 */
const char *ul_random_tell_source(ul_random_src_t src)
{
	char *s;

	switch (src) {
#ifdef HAVE_GETRANDOM
	case GETRANDOM:
		s = _("getrandom() function");
		break;
#endif
	case RANDOM:
		s = "/dev/random";
		break;
	case URANDOM:
		s = "/dev/urandom";
		break;
	default:
		s = _("libc pseudo-random functions");
		break;
	}

	return s;
}

#ifdef TEST_PROGRAM_RANDUTILS
#include <inttypes.h>

int main(int argc, char *argv[])
{
	size_t i, n;
	int64_t *vp, v;
	char *buf;
	size_t bufsz;

	n = argc == 1 ? 16 : atoi(argv[1]);

	printf("Multiple random calls:\n");
	for (i = 0; i < n; i++) {
		ul_random_get_bytes(&v, sizeof(v));
		printf("#%02zu: %25"PRIu64"\n", i, v);
	}


	printf("One random call:\n");
	bufsz = n * sizeof(*vp);
	buf = malloc(bufsz);
	if (!buf)
		err(EXIT_FAILURE, "failed to allocate buffer");

	ul_random_get_bytes(buf, bufsz);
	for (i = 0; i < n; i++) {
		vp = (int64_t *) (buf + (i * sizeof(*vp)));
		printf("#%02zu: %25"PRIu64"\n", i, *vp);
	}

	return EXIT_SUCCESS;
}
#endif /* TEST_PROGRAM_RANDUTILS */
