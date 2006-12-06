/*
 * llseek.c -- stub calling the llseek system call
 *
 * Copyright (C) 1994 Remy Card.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#include <sys/types.h>

#include <errno.h>
#include <unistd.h>

#if defined(__GNUC__) || defined(HAS_LONG_LONG)
typedef long long       ext2_loff_t;
#else
typedef long            ext2_loff_t;
#endif

extern ext2_loff_t ext2_llseek (unsigned int, ext2_loff_t, unsigned int);

#ifdef __linux__

#ifdef HAVE_LLSEEK
#include <syscall.h>

#else	/* HAVE_LLSEEK */

#if defined(__alpha__) || defined(__ia64__)

#define my_llseek lseek

#else
#include <linux/unistd.h>	/* for __NR__llseek */

static int _llseek (unsigned int, unsigned long,
		   unsigned long, ext2_loff_t *, unsigned int);

static _syscall5(int,_llseek,unsigned int,fd,unsigned long,offset_high,
		 unsigned long, offset_low,ext2_loff_t *,result,
		 unsigned int, origin)

static ext2_loff_t my_llseek (unsigned int fd, ext2_loff_t offset,
		unsigned int origin)
{
	ext2_loff_t result;
	int retval;

	retval = _llseek (fd, ((unsigned long long) offset) >> 32,
			((unsigned long long) offset) & 0xffffffff,
			&result, origin);
	return (retval == -1 ? (ext2_loff_t) retval : result);
}

#endif /* __alpha__ */

#endif	/* HAVE_LLSEEK */

ext2_loff_t ext2_llseek (unsigned int fd, ext2_loff_t offset,
			 unsigned int origin)
{
	ext2_loff_t result;
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

	if ((sizeof(off_t) >= sizeof(ext2_loff_t)) ||
	    (offset < ((ext2_loff_t) 1 << ((sizeof(off_t)*8) -1))))
		return lseek(fd, (off_t) offset, origin);

	errno = EINVAL;
	return -1;
}

#else /* !linux */

ext2_loff_t ext2_llseek (unsigned int fd, ext2_loff_t offset,
			 unsigned int origin)
{
	if ((sizeof(off_t) < sizeof(ext2_loff_t)) &&
	    (offset >= ((ext2_loff_t) 1 << ((sizeof(off_t)*8) -1)))) {
		errno = EINVAL;
		return -1;
	}
	return lseek (fd, (off_t) offset, origin);
}

#endif 	/* linux */


