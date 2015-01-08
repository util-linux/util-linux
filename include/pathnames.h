/*
 * Vaguely based on
 *	@(#)pathnames.h	5.3 (Berkeley) 5/9/89
 * This code is in the public domain.
 */
#ifndef PATHNAMES_H
#define PATHNAMES_H

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif

#ifndef __STDC__
# error "we need an ANSI compiler"
#endif

/* used by kernel in /proc (e.g. /proc/swaps) for deleted files */
#define PATH_DELETED_SUFFIX	"\\040(deleted)"
#define PATH_DELETED_SUFFIX_SZ	(sizeof(PATH_DELETED_SUFFIX) - 1)

/* DEFPATHs from <paths.h> don't include /usr/local */
#undef _PATH_DEFPATH
#define	_PATH_DEFPATH	        "/usr/local/bin:/bin:/usr/bin"

#undef _PATH_DEFPATH_ROOT
#define	_PATH_DEFPATH_ROOT	"/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin"

#define _PATH_SECURETTY		"/etc/securetty"
#define _PATH_WTMPLOCK		"/etc/wtmplock"

#define	_PATH_HUSHLOGIN		".hushlogin"
#define	_PATH_HUSHLOGINS	"/etc/hushlogins"

#define _PATH_NOLOGIN_TXT	"/etc/nologin.txt"

#ifndef _PATH_MAILDIR
#define	_PATH_MAILDIR		"/var/spool/mail"
#endif
#define	_PATH_MOTDFILE		"/etc/motd"
#define	_PATH_NOLOGIN		"/etc/nologin"
#define	_PATH_VAR_NOLOGIN	"/var/run/nologin"

#define _PATH_LOGIN		"/bin/login"
#define _PATH_INITTAB		"/etc/inittab"
#define _PATH_RC		"/etc/rc"
#define _PATH_REBOOT		"/sbin/reboot"
#define _PATH_SHUTDOWN		"/sbin/shutdown"
#define _PATH_SINGLE		"/etc/singleboot"
#define _PATH_SHUTDOWN_CONF	"/etc/shutdown.conf"

#define _PATH_SECURE		"/etc/securesingle"
#define _PATH_USERTTY           "/etc/usertty"

#define _PATH_TERMCOLORS_DIRNAME "terminal-colors.d"
#define _PATH_TERMCOLORS_DIR	"/etc/" _PATH_TERMCOLORS_DIRNAME

/* used in login-utils/shutdown.c */

/* used in login-utils/setpwnam.h and login-utils/islocal.c */
#define _PATH_PASSWD		"/etc/passwd"

/* used in login-utils/newgrp and login-utils/setpwnam.h*/
#define _PATH_GSHADOW		"/etc/gshadow"

/* used in login-utils/setpwnam.h */
#define _PATH_GROUP		"/etc/group"
#define _PATH_SHADOW_PASSWD	"/etc/shadow"
#define _PATH_SHELLS		"/etc/shells"

/* used in term-utils/agetty.c */
#define _PATH_ISSUE		"/etc/issue"
#define _PATH_OS_RELEASE	"/etc/os-release"
#define _PATH_NUMLOCK_ON	_PATH_LOCALSTATEDIR "/numlock-on"

#define _PATH_LOGINDEFS		"/etc/login.defs"

/* used in misc-utils/look.c */
#define _PATH_WORDS             "/usr/share/dict/words"
#define _PATH_WORDS_ALT         "/usr/share/dict/web2"

/* mount paths */
#define _PATH_UMOUNT		"/bin/umount"

#define _PATH_FILESYSTEMS	"/etc/filesystems"
#define _PATH_PROC_SWAPS	"/proc/swaps"
#define _PATH_PROC_FILESYSTEMS	"/proc/filesystems"
#define _PATH_PROC_MOUNTS	"/proc/mounts"
#define _PATH_PROC_PARTITIONS	"/proc/partitions"
#define _PATH_PROC_DEVICES	"/proc/devices"
#define _PATH_PROC_MOUNTINFO	"/proc/self/mountinfo"
#define _PATH_PROC_LOCKS        "/proc/locks"
#define _PATH_PROC_CDROMINFO	"/proc/sys/dev/cdrom/info"

#define _PATH_PROC_UIDMAP	"/proc/self/uid_map"
#define _PATH_PROC_GIDMAP	"/proc/self/gid_map"

#define _PATH_PROC_ATTR_CURRENT	"/proc/self/attr/current"
#define _PATH_PROC_ATTR_EXEC	"/proc/self/attr/exec"
#define _PATH_PROC_CAPLASTCAP	"/proc/sys/kernel/cap_last_cap"


#define _PATH_SYS_BLOCK		"/sys/block"
#define _PATH_SYS_DEVBLOCK	"/sys/dev/block"
#define _PATH_SYS_CLASS		"/sys/class"
#define _PATH_SYS_SCSI		"/sys/bus/scsi"

#define _PATH_SYS_SELINUX	"/sys/fs/selinux"
#define _PATH_SYS_APPARMOR	"/sys/kernel/security/apparmor"

#ifndef _PATH_MOUNTED
# ifdef MOUNTED					/* deprecated */
#  define _PATH_MOUNTED		MOUNTED
# else
#  define _PATH_MOUNTED		"/etc/mtab"
# endif
#endif

#ifndef _PATH_MNTTAB
# ifdef MNTTAB					/* deprecated */
#  define _PATH_MNTTAB		MNTTAB
# else
#  define _PATH_MNTTAB		"/etc/fstab"
# endif
#endif

#define _PATH_MNTTAB_DIR	_PATH_MNTTAB ".d"

#define _PATH_MOUNTED_LOCK	_PATH_MOUNTED "~"
#define _PATH_MOUNTED_TMP	_PATH_MOUNTED ".tmp"

#ifndef _PATH_DEV
  /*
   * The tailing '/' in _PATH_DEV is there for compatibility with libc.
   */
# define _PATH_DEV		"/dev/"
#endif

#define _PATH_DEV_MEM		"/dev/mem"

#define _PATH_DEV_LOOP		"/dev/loop"
#define _PATH_DEV_LOOPCTL	"/dev/loop-control"
#define _PATH_DEV_TTY		"/dev/tty"


/* udev paths */
#define _PATH_DEV_BYLABEL	"/dev/disk/by-label"
#define _PATH_DEV_BYUUID	"/dev/disk/by-uuid"
#define _PATH_DEV_BYID		"/dev/disk/by-id"
#define _PATH_DEV_BYPATH	"/dev/disk/by-path"
#define _PATH_DEV_BYPARTLABEL	"/dev/disk/by-partlabel"
#define _PATH_DEV_BYPARTUUID	"/dev/disk/by-partuuid"

/* hwclock paths */
#ifdef CONFIG_ADJTIME_PATH
# define _PATH_ADJTIME		CONFIG_ADJTIME_PATH
#else
# define _PATH_ADJTIME		"/etc/adjtime"
#endif

#define _PATH_LASTDATE		"/var/lib/lastdate"
#ifdef __ia64__
# define _PATH_RTC_DEV		"/dev/efirtc"
#else
# define _PATH_RTC_DEV		"/dev/rtc"
#endif

#ifndef _PATH_BTMP
#define _PATH_BTMP		"/var/log/btmp"
#endif

/* raw paths*/
#define _PATH_RAWDEVDIR		"/dev/raw/"
#define _PATH_RAWDEVCTL		_PATH_RAWDEVDIR "rawctl"
/* deprecated */
#define _PATH_RAWDEVCTL_OLD	"/dev/rawctl"

/* wdctl path */
#define _PATH_WATCHDOG_DEV	"/dev/watchdog"

/* ipc paths */
#define _PATH_PROC_SYSV_MSG	"/proc/sysvipc/msg"
#define _PATH_PROC_SYSV_SEM	"/proc/sysvipc/sem"
#define _PATH_PROC_SYSV_SHM	"/proc/sysvipc/shm"
#define _PATH_PROC_IPC_MSGMAX	"/proc/sys/kernel/msgmax"
#define _PATH_PROC_IPC_MSGMNB	"/proc/sys/kernel/msgmnb"
#define _PATH_PROC_IPC_MSGMNI	"/proc/sys/kernel/msgmni"
#define _PATH_PROC_IPC_SEM	"/proc/sys/kernel/sem"
#define _PATH_PROC_IPC_SHMALL	"/proc/sys/kernel/shmall"
#define _PATH_PROC_IPC_SHMMAX	"/proc/sys/kernel/shmmax"
#define _PATH_PROC_IPC_SHMMNI	"/proc/sys/kernel/shmmni"

/* kernel command line */
#define _PATH_PROC_CMDLINE	"/proc/cmdline"

#endif /* PATHNAMES_H */

