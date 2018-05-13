#ifndef LINUX_VERSION_H
#define LINUX_VERSION_H

#include <inttypes.h>

#ifdef HAVE_LINUX_VERSION_H
# include <linux/version.h>
#endif

#ifndef KERNEL_VERSION
# define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#endif

uint32_t get_linux_version(void);

#endif /* LINUX_VERSION_H */
