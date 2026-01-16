/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_PIDFD_UTILS
#define UTIL_LINUX_PIDFD_UTILS

#include <sys/types.h>
#include <sys/wait.h>

#ifdef HAVE_SYS_PIDFD_H
# include <sys/pidfd.h>
#endif

/*
 * pidfd ioctls
 *
 * All added by commit to kernel 6.11, commit 5b08bd408534bfb3a7cf5778da5b27d4e4fffe12.
 */
#ifndef PIDFS_IOCTL_MAGIC
# define PIDFS_IOCTL_MAGIC 0xFF
# define PIDFD_GET_CGROUP_NAMESPACE            _IO(PIDFS_IOCTL_MAGIC, 1)
# define PIDFD_GET_IPC_NAMESPACE               _IO(PIDFS_IOCTL_MAGIC, 2)
# define PIDFD_GET_MNT_NAMESPACE               _IO(PIDFS_IOCTL_MAGIC, 3)
# define PIDFD_GET_NET_NAMESPACE               _IO(PIDFS_IOCTL_MAGIC, 4)
# define PIDFD_GET_PID_NAMESPACE               _IO(PIDFS_IOCTL_MAGIC, 5)
# define PIDFD_GET_PID_FOR_CHILDREN_NAMESPACE  _IO(PIDFS_IOCTL_MAGIC, 6)
# define PIDFD_GET_TIME_NAMESPACE              _IO(PIDFS_IOCTL_MAGIC, 7)
# define PIDFD_GET_TIME_FOR_CHILDREN_NAMESPACE _IO(PIDFS_IOCTL_MAGIC, 8)
# define PIDFD_GET_USER_NAMESPACE              _IO(PIDFS_IOCTL_MAGIC, 9)
# define PIDFD_GET_UTS_NAMESPACE               _IO(PIDFS_IOCTL_MAGIC, 10)
#endif


#ifdef HAVE_SYS_SYSCALL_H
# include <sys/syscall.h>
# include <unistd.h>

# if !defined(HAVE_PIDFD_SEND_SIGNAL) && defined(SYS_pidfd_send_signal)
static inline int pidfd_send_signal(int pidfd, int sig, siginfo_t *info,
				    unsigned int flags)
{
	return syscall(SYS_pidfd_send_signal, pidfd, sig, info, flags);
}
# endif

# if !defined(HAVE_PIDFD_OPEN) && defined(SYS_pidfd_open)
static inline int pidfd_open(pid_t pid, unsigned int flags)
{
	return syscall(SYS_pidfd_open, pid, flags);
}
# endif

# if !defined(HAVE_PIDFD_GETFD) && defined(SYS_pidfd_getfd)
static inline int pidfd_getfd(int pidfd, int targetfd, unsigned int flags)
{
	return syscall(SYS_pidfd_getfd, pidfd, targetfd, flags);
}
# endif
#endif /* HAVE_SYS_SYSCALL_H */


/*
 * Dummy fallbacks for cases when #ifdef HAVE_PIDFD_* makes the code too complex.
 */

#if !defined(HAVE_PIDFD_SEND_SIGNAL) && !defined(SYS_pidfd_send_signal)
static inline int pidfd_send_signal(int pidfd __attribute__((unused)),
				    int sig __attribute__((unused)),
				    siginfo_t *info __attribute__((unused)),
				    unsigned int flags __attribute__((unused)))
{
	errno = ENOSYS;
	return -1;
}
#endif

#if !defined(HAVE_PIDFD_OPEN) && !defined(SYS_pidfd_open)
static inline int pidfd_open(pid_t pid __attribute__((unused)),
			     unsigned int flags __attribute__((unused)))
{
	errno = ENOSYS;
	return -1;
}
#endif

# if !defined(HAVE_PIDFD_GETFD) && !defined(SYS_pidfd_getfd)
static inline int pidfd_getfd(int pidfd __attribute__((unused)),
			      int targetfd __attribute__((unused)),
			      unsigned int flags __attribute__((unused)))
{
	errno = ENOSYS;
	return -1;
}
#endif

#endif /* UTIL_LINUX_PIDFD_UTILS */
