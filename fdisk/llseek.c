/*
 * llseek.c -- stub calling the llseek system call
 *
 * Copyright (C) 1994 Remy Card.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include <sys/types.h>

#include <errno.h>
#include <unistd.h>

extern long long ext2_llseek (unsigned int, long long, unsigned int);

#ifdef __linux__

#ifdef HAVE_LLSEEK
#include <syscall.h>

#else	/* HAVE_LLSEEK */

#if defined(__alpha__) || defined(__ia64__) || defined(__s390x__)

#define my_llseek lseek

#else
#include <linux/unistd.h>	/* for __NR__llseek */

static int _llseek (unsigned int, unsigned long,
		   unsigned long, long long *, unsigned int);

#ifdef __NR__llseek

static _syscall5(int,_llseek,unsigned int,fd,unsigned long,offset_high,
		 unsigned long, offset_low,long long *,result,
		 unsigned int, origin)

#else

/* no __NR__llseek on compilation machine - might give it explicitly */
static int _llseek (unsigned int fd, unsigned long oh,
		    unsigned long ol, long long *result,
		    unsigned int origin) {
	errno = ENOSYS;
	return -1;
}

#endif

static long long my_llseek (unsigned int fd, long long offset,
		unsigned int origin)
{
	long long result;
	int retval;

	retval = _llseek (fd, ((unsigned long long) offset) >> 32,
			((unsigned long long) offset) & 0xffffffff,
			&result, origin);
	return (retval == -1 ? (long long) retval : result);
}

#endif /* __alpha__ */

#endif	/* HAVE_LLSEEK */

long long ext2_llseek (unsigned int fd, long long offset,
			 unsigned int origin)
{
	long long result;
	static int do_compat = 0;

	if (!do_compat) {
		result = my_llseek (fd, offset, origin);
		if (!(result == -1 && errno == ENOSYS))
			return result;

		/*
		 * Just in case this code runs on top of an old kernel
		 * which does not support the llseek system call
		 */
		do_compat = 1;
		/*
		 * Now try ordinary lseek.
		 */
	}

	if ((sizeof(off_t) >= sizeof(long long)) ||
	    (offset < ((long long) 1 << ((sizeof(off_t)*8) -1))))
		return lseek(fd, (off_t) offset, origin);

	errno = EINVAL;
	return -1;
}

#else /* !linux */

long long ext2_llseek (unsigned int fd, long long offset,
			 unsigned int origin)
{
	if ((sizeof(off_t) < sizeof(long long)) &&
	    (offset >= ((long long) 1 << ((sizeof(off_t)*8) -1)))) {
		errno = EINVAL;
		return -1;
	}
	return lseek (fd, (off_t) offset, origin);
}

#endif 	/* linux */


