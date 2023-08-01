/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_PIDFD_UTILS
#define UTIL_LINUX_PIDFD_UTILS

#ifdef HAVE_SYS_SYSCALL_H
# include <sys/syscall.h>

/*
 * If the kernel headers are too old to provide the syscall numbers, let's
 * define them ourselves. This can be helpful while cross-compiling.
 */
#ifndef __NR_pidfd_send_signal
#define __NR_pidfd_send_signal 424
#define SYS_pidfd_send_signal __NR_pidfd_send_signal
#endif
#ifndef __NR_pidfd_open
#define __NR_pidfd_open 434
#define SYS_pidfd_open __NR_pidfd_open
#endif

# if defined(SYS_pidfd_send_signal) && defined(SYS_pidfd_open)
#  ifdef HAVE_SYS_PIDFD_H
#   include <sys/pidfd.h>
#  endif
#  include <sys/types.h>
#  ifndef HAVE_PIDFD_SEND_SIGNAL
#   include <sys/wait.h>
static inline int pidfd_send_signal(int pidfd, int sig, siginfo_t *info,
				    unsigned int flags)
{
	return syscall(SYS_pidfd_send_signal, pidfd, sig, info, flags);
}
#  endif

#  ifndef HAVE_PIDFD_OPEN
static inline int pidfd_open(pid_t pid, unsigned int flags)
{
	return syscall(SYS_pidfd_open, pid, flags);
}
#  endif

#  define UL_HAVE_PIDFD 1

# endif	/* SYS_pidfd_send_signal */
#endif /* HAVE_SYS_SYSCALL_H */
#endif /* UTIL_LINUX_PIDFD_UTILS */
