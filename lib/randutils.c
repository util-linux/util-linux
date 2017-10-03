/*
 * General purpose random utilities
 *
 * Based on libuuid code.
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <sys/syscall.h>

#include "c.h"
#include "randutils.h"
#include "nls.h"

#ifdef HAVE_TLS
#define THREAD_LOCAL static __thread
#else
#define THREAD_LOCAL static
#endif

#ifdef HAVE_GETRANDOM
# include <sys/random.h>
#elif defined (__linux__)
# if !defined(SYS_getrandom) && defined(__NR_getrandom)
   /* usable kernel-headers, but old glibc-headers */
#  define SYS_getrandom __NR_getrandom
# endif
#endif

#if !defined(HAVE_GETRANDOM) && defined(SYS_getrandom)
/* libc without function, but we have syscal */
static int getrandom(void *buf, size_t buflen, unsigned int flags)
{
	return (syscall(SYS_getrandom, buf, buflen, flags));
}
# define HAVE_GETRANDOM
#endif

#if defined(__linux__) && defined(__NR_gettid) && defined(HAVE_JRAND48)
#define DO_JRAND_MIX
THREAD_LOCAL unsigned short ul_jrand_seed[3];
#endif

int rand_get_number(int low_n, int high_n)
{
	return rand() % (high_n - low_n + 1) + low_n;
}

static void crank_random(void)
{
	int i;
	struct timeval tv;

	gettimeofday(&tv, NULL);
	srand((getpid() << 16) ^ getuid() ^ tv.tv_sec ^ tv.tv_usec);

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

int random_get_fd(void)
{
	int i, fd;

	fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd == -1)
		fd = open("/dev/random", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd >= 0) {
		i = fcntl(fd, F_GETFD);
		if (i >= 0)
			fcntl(fd, F_SETFD, i | FD_CLOEXEC);
	}
	crank_random();
	return fd;
}

/*
 * Generate a stream of random nbytes into buf.
 * Use /dev/urandom if possible, and if not,
 * use glibc pseudo-random functions.
 */
void random_get_bytes(void *buf, size_t nbytes)
{
	unsigned char *cp = (unsigned char *)buf;
	size_t i, n = nbytes;
	int lose_counter = 0;

#ifdef HAVE_GETRANDOM
	while (n > 0) {
		int x;

		errno = 0;
		x = getrandom(cp, n, 0);
		if (x > 0) {			/* success */
		       n -= x;
		       cp += x;
		       lose_counter = 0;
		} else if (errno == ENOSYS)	/* kernel without getrandom() */
			break;
		else if (lose_counter++ > 16)	/* entropy problem? */
			break;
	}

	if (errno == ENOSYS)
#endif
	/*
	 * We've been built against headers that support getrandom, but the
	 * running kernel does not.  Fallback to reading from /dev/{u,}random
	 * as before
	 */
	{
		int fd = random_get_fd();

		lose_counter = 0;
		if (fd >= 0) {
			while (n > 0) {
				ssize_t x = read(fd, cp, n);
				if (x <= 0) {
					if (lose_counter++ > 16)
						break;
					continue;
				}
				n -= x;
				cp += x;
				lose_counter = 0;
			}

			close(fd);
		}
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
	return;
}


/*
 * Tell source of randomness.
 */
const char *random_tell_source(void)
{
#ifdef HAVE_GETRANDOM
	return _("getrandom() function");
#else
	size_t i;
	static const char *random_sources[] = {
		"/dev/urandom",
		"/dev/random"
	};

	for (i = 0; i < ARRAY_SIZE(random_sources); i++) {
		if (!access(random_sources[i], R_OK))
			return random_sources[i];
	}
#endif
	return _("libc pseudo-random functions");
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
		random_get_bytes(&v, sizeof(v));
		printf("#%02zu: %25"PRIu64"\n", i, v);
	}


	printf("One random call:\n");
	bufsz = n * sizeof(*vp);
	buf = malloc(bufsz);
	if (!buf)
		err(EXIT_FAILURE, "failed to allocate buffer");

	random_get_bytes(buf, bufsz);
	for (i = 0; i < n; i++) {
		vp = (int64_t *) (buf + (i * sizeof(*vp)));
		printf("#%02zu: %25"PRIu64"\n", i, *vp);
	}

	return EXIT_SUCCESS;
}
#endif /* TEST_PROGRAM_RANDUTILS */
