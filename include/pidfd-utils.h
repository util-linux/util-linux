#ifndef UTIL_LINUX_PIDFD_UTILS
#define UTIL_LINUX_PIDFD_UTILS

#if defined(__linux__) && defined(SYS_pidfd_send_signal)
# include <sys/types.h>
# include <sys/syscall.h>

# ifndef HAVE_PIDFD_OPEN
static inline int pidfd_send_signal(int pidfd, int sig, siginfo_t *info,
				    unsigned int flags)
{
	return syscall(SYS_pidfd_send_signal, pidfd, sig, info, flags);
}
# endif

# ifndef HAVE_PIDFD_SEND_SIGNAL
static inline int pidfd_open(pid_t pid, unsigned int flags)
{
	return syscall(SYS_pidfd_open, pid, flags);
}
# endif

# define UL_HAVE_PIDFD 1

#endif /* __linux__ && SYS_pidfd_send_signal */
#endif /* UTIL_LINUX_PIDFD_UTILS */
