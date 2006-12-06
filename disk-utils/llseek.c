/*
 * llseek.c -- stub calling the llseek system call
 *
 * Copyright (C) 1994 Remy Card.  This file may be redistributed
 * under the terms of the GNU Public License.
 */

#define FOR_UTIL_LINUX

#include <sys/types.h>

#include <errno.h>
#include <unistd.h>
#include <linux/unistd.h>
#ifndef FOR_UTIL_LINUX
#include "et/com_err.h"
#include "ext2fs/io.h"
#endif

#ifdef FOR_UTIL_LINUX
#if defined(__GNUC__) || defined(HAS_LONG_LONG)
typedef long long ext2_loff_t;
#else
typedef long      ext2_loff_t;
#endif

extern ext2_loff_t ext2_llseek(unsigned int fd,
			       ext2_loff_t offset,
			       unsigned int origin);
#endif

#ifdef __linux__

#ifndef __NR__llseek
#define __NR__llseek            140
#endif

static int _llseek (unsigned int, unsigned long,
		   unsigned long, ext2_loff_t *, unsigned int);

static _syscall5(int,_llseek,unsigned int,fd,unsigned long,offset_high,
		 unsigned long, offset_low,ext2_loff_t *,result,
		 unsigned int, origin)

ext2_loff_t ext2_llseek (unsigned int fd, ext2_loff_t offset,
			 unsigned int origin)
{
	unsigned long offset_high;
	unsigned long offset_low;
	ext2_loff_t result;
	int retval;
	static int do_compat = 0;

	if (do_compat) {
	compat_lseek:
		if ((sizeof(off_t) < sizeof(ext2_loff_t)) &&
		    (offset >= ((ext2_loff_t) 1 << ((sizeof(off_t)*8) -1)))) {
			errno = -EINVAL;
			return -1;
		}
		return lseek (fd, (off_t) offset, origin);
	}
	
	offset_high = ((unsigned long long) offset) >> 32;
	offset_low = ((unsigned long long) offset) & 0xffffffff;
	retval = _llseek (fd, offset_high, offset_low, &result, origin);
	if (retval == -1 && errno == ENOSYS) {
		/*
		 * Just in case this code runs on top of an old kernel
		 * which does not support the llseek system call
		 */
		do_compat++;
		goto compat_lseek;
	}
	if (retval == -1)
		result = -1;
	return result;
}

#else

ext2_loff_t ext2_llseek (unsigned int fd, ext2_loff_t offset,
			 unsigned int origin)
{
	if ((sizeof(off_t) < sizeof(ext2_loff_t)) &&
	    (offset >= ((ext2_loff_t) 1 << ((sizeof(off_t)*8) -1)))) {
		errno = -EINVAL;
		return -1;
	}
	return lseek (fd, (off_t) offset, origin);
}

#endif


