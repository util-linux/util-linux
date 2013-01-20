/* Compat code so unshare and setns can be used with older libcs */
#ifndef UTIL_LINUX_NAMESPACE_H
# define UTIL_LINUX_NAMESPACE_H

# include <sched.h>

# ifndef CLONE_NEWSNS
#  define CLONE_NEWNS 0x00020000
# endif
# ifndef CLONE_NEWUTS
#  define CLONE_NEWUTS 0x04000000
# endif
# ifndef CLONE_NEWIPC
#  define CLONE_NEWIPC 0x08000000
# endif
# ifndef CLONE_NEWNET
#  define CLONE_NEWNET 0x40000000
# endif
# ifndef CLONE_NEWUSER
#  define CLONE_NEWUSER 0x10000000
# endif
# ifndef CLONE_NEWPID
#  define CLONE_NEWPID 0x20000000
# endif

# ifndef HAVE_UNSHARE
#  include <sys/syscall.h>
static inline int unshare(int flags)
{
	return syscall(SYS_unshare, flags);
}
# endif

# ifndef HAVE_SETNS
#  include <sys/syscall.h>
static inline int setns(int fd, int nstype)
{
	return syscall(SYS_setns, fd, nstype);
}
# endif

#endif	/* UTIL_LINUX_NAMESPACE_H */
