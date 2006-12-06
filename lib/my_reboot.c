/* Including <unistd.h> makes sure that on a glibc system
   <features.h> is included, which again defines __GLIBC__ */
#include <unistd.h>
#include "linux_reboot.h"

#define USE_LIBC

#ifdef USE_LIBC

/* libc version */
#if defined __GLIBC__ && __GLIBC__ >= 2
#  include <sys/reboot.h>
#  define REBOOT(cmd) reboot(cmd)
#else
extern int reboot(int, int, int);
#  define REBOOT(cmd) reboot(LINUX_REBOOT_MAGIC1,LINUX_REBOOT_MAGIC2,(cmd))
#endif
int
my_reboot(int cmd) {
	return REBOOT(cmd);
}

#else /* no USE_LIBC */

/* direct syscall version */
#include <linux/unistd.h>

#ifdef _syscall3
_syscall3(int,  reboot,  int,  magic, int, magic_too, int, cmd);
#else
/* Let us hope we have a 3-argument reboot here */
extern int reboot(int, int, int);
#endif

int
my_reboot(int cmd) {
	return reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, cmd);
}

#endif
