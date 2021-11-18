#ifndef UTIL_LINUX_PIDFD_UTILS
#define UTIL_LINUX_PIDFD_UTILS

#ifdef HAVE_SYS_SYSCALL_H
# include <sys/syscall.h>
# if defined(SYS_pidfd_send_signal) && defined(SYS_pidfd_open)
#  include <sys/types.h>

#  ifndef HAVE_PIDFD_SEND_SIGNAL
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
