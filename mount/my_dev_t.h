/* silliness to get dev_t defined as the kernel defines it */
/* glibc uses a different dev_t */

#include <linux/posix_types.h>
#include <linux/version.h>

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(1,3,78)
/* for i386 - alpha uses unsigned int */
#define my_dev_t unsigned short
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,68)
#define my_dev_t __kernel_dev_t
#else
#define my_dev_t __kernel_old_dev_t
#endif
#endif
