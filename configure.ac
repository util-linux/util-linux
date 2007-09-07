AC_INIT(util-linux-ng, 2.14, kzak@redhat.com)

AC_PREREQ(2.60)

AC_CONFIG_AUX_DIR(config)

AM_INIT_AUTOMAKE([check-news -Wall foreign 1.9 dist-bzip2])

AC_CONFIG_SRCDIR(mount/mount.c)

AC_PREFIX_DEFAULT([/usr])

# Check whether exec_prefix=/usr:
case $exec_prefix:$prefix in
NONE:NONE | NONE:/usr | /usr:*)
  AC_MSG_NOTICE([Default --exec-prefix detected.])
  case $bindir in
  '${exec_prefix}/bin') bindir=/bin
    AC_MSG_NOTICE([  --bindir defaults to /bin]) ;;
  esac
  case $sbindir in
  '${exec_prefix}/sbin') sbindir=/sbin
    AC_MSG_NOTICE([  --sbindir defaults to /sbin]) ;;
  esac ;;
esac

AC_PROG_CC_STDC
AC_GNU_SOURCE

AC_PATH_PROG(PERL, perl)
AC_PATH_PROG(BLKID, blkid, [], [$PATH:/sbin])
AC_PATH_PROG(VOLID, vol_id, [], [$PATH:/lib/udev])

AC_SYS_LARGEFILE

AC_CHECK_HEADERS([linux/compiler.h linux/blkpg.h], [], [], [
#ifdef HAVE_LINUX_COMPILER_H
#include <linux/compiler.h>
#endif
])
AC_CHECK_HEADERS(
	[scsi/scsi.h \
	langinfo.h \
	locale.h \
	sys/user.h \
	rpcsvc/nfs_prot.h \
	sys/io.h \
	pty.h])
AC_CHECK_HEADERS([linux/raw.h],
		 [AM_CONDITIONAL([HAVE_RAW], [true])],
		 [AM_CONDITIONAL([HAVE_RAW], [false])])

AC_CHECK_FUNCS(
	[inet_aton \
	fsync \
	getdomainname \
	nanosleep \
	personality \
	updwtmp \
	lchown \
	rpmatch])
AC_FUNC_FSEEKO

dnl UTIL_CHECK_LIB(LIBRARY, FUNCTION)
dnl ---------------------------------
AC_DEFUN([UTIL_CHECK_LIB], [
  dnl The trick is to keep the third argument to AC_CHECK_LIB empty,
  dnl and thus keep the default action.
  have_$1=yes
  AC_CHECK_LIB([$1], [$2], [], [have_$1=no])
  AM_CONDITIONAL(AS_TR_CPP(HAVE_$1), [test $have_$1 = yes])
])

UTIL_CHECK_LIB(uuid, uuid_is_null)
if test $have_uuid = no; then
  AC_MSG_WARN([uuid library is not found; mkswap(8) will not generate UUIDs])
fi

UTIL_CHECK_LIB(util, openpty)
UTIL_CHECK_LIB(termcap, tgetnum)

AC_ARG_WITH([fsprobe],
  [AS_HELP_STRING([--with-fsprobe], [library to guess filesystems (blkid|volume_id), default is blkid])],
  [], [with_fsprobe=blkid]
)

AM_CONDITIONAL(HAVE_BLKID, false)
AM_CONDITIONAL(HAVE_VOLUME_ID, false)

have_blkid=no
have_volume_id=no
if test x$with_fsprobe = xblkid; then
  UTIL_CHECK_LIB(blkid, blkid_known_fstype)
elif test x$with_fsprobe = xvolume_id; then
  UTIL_CHECK_LIB(volume_id, volume_id_encode_string)
fi

if test $have_blkid = no && test $have_volume_id = no; then
  AC_MSG_ERROR([blkid or volume_id is needed to build util-linux-ng.])
fi

AM_GNU_GETTEXT_VERSION([0.14.1])
AM_GNU_GETTEXT([external])
if test -d $srcdir/po
then
    ALL_LINGUAS=`cd $srcdir/po > /dev/null && echo *.po | sed 's/\.po//g'`
else
    ALL_LINGUAS="af am ar as be bg bn_IN bn ca cs cy da de el en_GB es et eu_ES fa fi fr gl gu he hi hr hu hy id is it ja ka kn ko ku lo lt lv mk ml mr ms my nb nl nn no nso or pa pl pt_BR pt ro ru si sk sl sq sr@Latn sr sv ta te th tr uk ur vi zh_CN zh_TW zu"
fi

have_ncurses=no
AC_CHECK_HEADERS([ncurses.h ncurses/ncurses.h], [have_ncurses=yes])
case $have_ncurses in
yes)
     AC_MSG_NOTICE([you have ncurses])
     AC_DEFINE(HAVE_NCURSES, 1, [Do we have -lncurses?])
     ;;
no)
     AC_MSG_NOTICE([you do not have ncurses])
     ;;
esac
AM_CONDITIONAL(HAVE_NCURSES, test x$have_ncurses = xyes)

AC_ARG_WITH([slang],
  AS_HELP_STRING([--with-slang], [compile cfdisk with slang]),
  [], with_slang=no
)

have_tinfo=no
AC_CHECK_LIB(tinfo, tgetent, [have_tinfo=yes])
AM_CONDITIONAL(HAVE_TINFO, test x$have_tinfo = xyes)

use_slang=no
if test x$with_slang = xyes; then
  AC_CHECK_HEADERS([slcurses.h slang/slcurses.h], [use_slang=yes])
  if test $use_slang = no; then
    AC_MSG_ERROR([slang selected but slcurses.h not found])
  fi
fi
AM_CONDITIONAL(USE_SLANG, test $use_slang = yes)


AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#define _XOPEN_SOURCE
#include <unistd.h>
]], [[
char *c = crypt("abc","pw");
]])],[],[
  LIBS="$LIBS -lcrypt"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
  #define _XOPEN_SOURCE
  #include <unistd.h>
  ]], [[
  char *c = crypt("abc","pw");
  ]])],[
  AC_DEFINE(NEED_LIBCRYPT, 1, [Do we need -lcrypt?])
  need_libcrypt=yes
  ],[
  AC_MSG_ERROR([crypt() is not available])
  ])
])

AM_CONDITIONAL(NEED_LIBCRYPT, test x$need_libcrypt = xyes)

AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <stdio.h>
]], [[
printf(__progname);
]])],
[AC_DEFINE(HAVE___PROGNAME, 1, Do we have __progname?)
])


AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <wchar.h>
#include <wctype.h>
#include <stdio.h>
]], [[
  wchar_t wc;
  wint_t w;
  w = fgetwc(stdin);
  if (w == WEOF) exit(1);
  wc = w;
  fputwc(wc,stdout);
]])],
[AC_DEFINE(HAVE_WIDECHAR,1,Do we have wide character support?)
])


dnl UTIL_CHECK_SYSCALL(SYSCALL, FALLBACK, ...)
dnl Only specify FALLBACK if the SYSCALL
dnl you're checking for is a "newish" one
dnl -------------------------------------
AC_DEFUN([UTIL_CHECK_SYSCALL], [
  dnl This macro uses host_cpu.
  AC_REQUIRE([AC_CANONICAL_HOST])
  AC_CACHE_CHECK([for syscall $1],
    [util_cv_syscall_$1],
    [_UTIL_SYSCALL_CHECK_DECL([SYS_$1],
      [syscall=SYS_$1],
      [dnl Our libc failed use, so see if we can get the kernel
      dnl headers to play ball ...
      _UTIL_SYSCALL_CHECK_DECL([_NR_$1],
	[syscall=_NR_$1],
	[
	  syscall=no
	  case $host_cpu in
	    _UTIL_CHECK_SYSCALL_FALLBACK(m4_shift($@))
	  esac
        ])
      ])
    util_cv_syscall_$1=$syscall
    ])
  AM_CONDITIONAL([HAVE_]m4_toupper($1), [test $util_cv_syscall_$1 != no])
  case $util_cv_syscall_$1 in #(
  no) AC_MSG_WARN([Unable to detect syscall $1.]) ;;
  SYS_*) ;;
  *) AC_DEFINE_UNQUOTED([SYS_$1], [$util_cv_syscall_$1],
	[Fallback syscall number for $1]) ;;
  esac
])

dnl _UTIL_SYSCALL_CHECK_DECL(SYMBOL, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)
dnl Check if SYMBOL is declared, using the headers needed for syscall checks.
dnl -------------------------------------
m4_define([_UTIL_SYSCALL_CHECK_DECL],
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/syscall.h>
#include <unistd.h>
]], [[int test = $1;]])],
[$2], [$3])
])

dnl _UTIL_CHECK_SYSCALL_FALLBACK(PATTERN, VALUE, ...)
dnl Helper macro to create the body for the above `case'.
dnl -------------------------------------
m4_define([_UTIL_CHECK_SYSCALL_FALLBACK],
[m4_ifval([$1],
  [#(
  $1) syscall="$2" ;;dnl
  _UTIL_CHECK_SYSCALL_FALLBACK(m4_shiftn(2, $@))])dnl
])


UTIL_CHECK_SYSCALL([pivot_root])
UTIL_CHECK_SYSCALL([sched_getaffinity])
UTIL_CHECK_SYSCALL([ioprio_set],
  [alpha],    [442],
  [i*86],     [289],
  [ia64*],    [1274],
  [powerpc*], [273],
  [s390*],    [282],
  [sparc*],   [196],
  [x86_64*],  [251])
UTIL_CHECK_SYSCALL([ioprio_get],
  [alpha],    [443],
  [i*86],     [290],
  [ia64*],    [1275],
  [powerpc*], [274],
  [s390*],    [283],
  [sparc*],   [218],
  [x86_64*],  [252])


AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <time.h>
#include <unistd.h>
]], [[
    int a = 0;
    struct tm *tm = localtime(0);
    if (a == -1)  /* false */
        sleep(tm->tm_gmtoff);
]])],
[AC_DEFINE(HAVE_TM_GMTOFF,1,[Does struct tm have a field tm_gmtoff?])
])


AC_CHECK_HEADERS([sys/swap.h])

AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[
#ifdef HAVE_SYS_SWAP_H
# include <sys/swap.h>
#endif
#include <unistd.h>
]],
[[swapon("/dev/null", 0);]])],
[AC_DEFINE(SWAPON_HAS_TWO_ARGS, 1, [Is swapon() declared with two parameters?])
],
[AC_MSG_NOTICE([Your libc thinks that swapon has 1 arg only.])
])


dnl UTIL_SET_ARCH(ARCHNAME, PATTERN)
dnl ---------------------------------
AC_DEFUN([UTIL_SET_ARCH], [
  cpu_$1=false
  case "$host" in
   $2) cpu_$1=true ;;
  esac
  AM_CONDITIONAL(AS_TR_CPP(ARCH_$1), [test x$cpu_$1 = xtrue])
])

UTIL_SET_ARCH(I86, i?86-*)
UTIL_SET_ARCH(86_64, x86_64*)
UTIL_SET_ARCH(IA64, ia64*)
UTIL_SET_ARCH(S390, s390*)
UTIL_SET_ARCH(SPARC, sparc*)
UTIL_SET_ARCH(PPC, ppc*|powerpc*)
UTIL_SET_ARCH(M68K, m68*)
UTIL_SET_ARCH(MIPS, mips*)
UTIL_SET_ARCH(HPPA, hppa*)

AC_ARG_ENABLE([arch],
  AS_HELP_STRING([--enable-arch], [do build arch]),
  [], enable_arch=no
)
AM_CONDITIONAL(BUILD_ARCH, test x$enable_arch = xyes)

AC_ARG_ENABLE([agetty],
  AS_HELP_STRING([--disable-agetty], [do not build agetty]),
  [], enable_agetty=yes
)
AM_CONDITIONAL(BUILD_AGETTY, test x$enable_agetty = xyes)

AC_ARG_ENABLE([cramfs],
  AS_HELP_STRING([--disable-cramfs], [do not build fsck.cramfs, mkfs.cramfs]),
  [], enable_cramfs=check
)

if test $enable_cramfs = no; then
  build_cramfs=no
else
  build_cramfs=yes
  dnl Trick: leave the third parameter empty to get the default action.
  AC_CHECK_LIB(z, crc32, [], build_cramfs=no)
  case $enable_cramfs:$build_cramfs in
  yes:no) AC_MSG_ERROR([cramfs selected but libz not found]);;
  esac
fi
AM_CONDITIONAL(BUILD_CRAMFS, test $build_cramfs = yes)


AC_ARG_ENABLE([elvtune],
  AS_HELP_STRING([--enable-elvtune], [build elvtune (only works with 2.2 and 2.4 kernels)]),
  [], enable_elvtune=no
)
AM_CONDITIONAL(BUILD_ELVTUNE, test x$enable_elvtune = xyes)


AC_ARG_ENABLE([init],
  AS_HELP_STRING([--enable-init], [build simpleinit, shutdown, initctl]),
  [], enable_init=no
)
AM_CONDITIONAL(BUILD_INIT, test x$enable_init = xyes)


AC_ARG_ENABLE([kill],
  AS_HELP_STRING([--enable-kill], [build kill]),
  [], enable_kill=no
)
AM_CONDITIONAL(BUILD_KILL, test x$enable_kill = xyes)


AC_ARG_ENABLE([last],
  AS_HELP_STRING([--enable-last], [build last]),
  [], enable_last=no
)
AM_CONDITIONAL(BUILD_LAST, test x$enable_last = xyes)


AC_ARG_ENABLE([mesg],
  AS_HELP_STRING([--enable-mesg], [build mesg]),
  [], enable_mesg=no
)
AM_CONDITIONAL(BUILD_MESG, test x$enable_mesg = xyes)


AC_ARG_ENABLE([partx],
  AS_HELP_STRING([--enable-partx], [build addpart, delpart, partx]),
  [], enable_partx=no
)
AM_CONDITIONAL(BUILD_PARTX, test x$enable_partx = xyes)


AC_ARG_ENABLE([raw],
  AS_HELP_STRING([--enable-raw], [build raw]),
  [], enable_raw=no
)
AM_CONDITIONAL(BUILD_RAW, test x$enable_raw = xyes)


AC_ARG_ENABLE([rdev],
  AS_HELP_STRING([--enable-rdev], [build rdev on i386]),
  [], enable_rdev=no
)
AM_CONDITIONAL(BUILD_RDEV, test x$enable_rdev = xyes)


AC_ARG_ENABLE([rename],
  AS_HELP_STRING([--disable-rename], [do not build rename]),
  [], enable_rename=yes
)
AM_CONDITIONAL(BUILD_RENAME, test x$enable_rename = xyes)


AC_ARG_ENABLE([reset],
  AS_HELP_STRING([--enable-reset], [build reset]),
  [], enable_reset=no
)
AM_CONDITIONAL(BUILD_RESET, test x$enable_reset = xyes)


AC_ARG_ENABLE([login-utils],
  AS_HELP_STRING([--enable-login-utils], [build chfn, chsh, login, newgrp, vipw]),
  [], enable_login_utils=no
)
AM_CONDITIONAL(BUILD_LOGIN_UTILS, test x$enable_login_utils = xyes)

AC_ARG_WITH([pam],
  [AS_HELP_STRING([--without-pam], [compile login-utils without PAM support])])

AM_CONDITIONAL(HAVE_PAM, false)
if test x$enable_login_utils = xyes && test x$with_pam != xno; then
  AC_CHECK_HEADERS([security/pam_misc.h],
    [AM_CONDITIONAL(HAVE_PAM, true)],
    [if test x$with_pam = xyes; then
	AC_MSG_ERROR([PAM selected but security/pam_misc.h not found])
     fi
    ])
fi

AC_ARG_WITH([selinux],
  AS_HELP_STRING([--with-selinux], [compile with SELinux support]),
  [], with_selinux=no
)

if test x$with_selinux = xno; then
  AM_CONDITIONAL(HAVE_SELINUX, false)
else
  UTIL_CHECK_LIB(selinux, getprevcon)
  case $with_selinux:$have_selinux in
  yes:no) AC_MSG_ERROR([SELinux selected but libselinux not found]);;
  esac
fi


AC_ARG_WITH([audit],
  AS_HELP_STRING([--with-audit], [compile with audit support]),
  [], with_audit=no
)

if test x$with_audit = xno; then
  AM_CONDITIONAL(HAVE_AUDIT, false)
else
  UTIL_CHECK_LIB(audit, audit_log_user_message)
  case $with_audit:$have_audit in
  yes:no)
   AC_MSG_ERROR([Audit selected but libaudit not found (or doesn't support audit_log_user_message())])
   ;;
  esac
fi


AC_ARG_ENABLE([schedutils],
  AS_HELP_STRING([--disable-schedutils], [do not build chrt, ionice, teskset]),
  [], enable_schedutils=yes
)
AM_CONDITIONAL(BUILD_SCHEDUTILS, test x$enable_schedutils = xyes)


AC_ARG_ENABLE([wall],
  AS_HELP_STRING([--disable-wall], [do not build wall]),
  [], enable_wall=yes
)
AM_CONDITIONAL(BUILD_WALL, test x$enable_wall = xyes)


AC_ARG_ENABLE([write],
  AS_HELP_STRING([--enable-write], [build write]),
  [], enable_write=no
)
AM_CONDITIONAL(BUILD_WRITE, test x$enable_write = xyes)


AC_ARG_ENABLE([chsh-only-listed],
  AS_HELP_STRING([--disable-chsh-only-listed], [chsh: allow shells not in /etc/shells]),
  [], enable_chsh_only_listed=yes
)

if test x$enable_chsh_only_listed = xyes; then
  AC_DEFINE(ONLY_LISTED_SHELLS, 1, [Should chsh allow only shells in /etc/shells?])
fi


AC_ARG_ENABLE([login-chown-vcs],
  AS_HELP_STRING([--enable-login-chown-vcs], [let login chown /dev/vcsN]),
  [], enable_login_chown_vcs=no
)

if test x$enable_login_chown_vcs = xyes; then
  AC_DEFINE(LOGIN_CHOWN_VCS, 1, [Should login chown /dev/vcsN?])
fi


AC_ARG_ENABLE([login-stat-mail],
  AS_HELP_STRING([--enable-login-stat-mail], [let login stat() the mailbox]),
  [], enable_login_stat_mail=no
)

if test x$enable_login_stat_mail = xyes; then
  AC_DEFINE(LOGIN_STAT_MAIL, 1, [Should login stat() the mailbox?])
fi


AC_ARG_ENABLE([pg-bell],
  AS_HELP_STRING([--disable-pg-bell], [let pg not ring the bell on invalid keys]),
  [], enable_pg_bell=yes
)

if test x$enable_pg_bell = xyes; then
  AC_DEFINE(PG_BELL, 1, [Should pg ring the bell on invalid keys?])
fi


AC_ARG_ENABLE([require-password],
  AS_HELP_STRING([--disable-require-password], [do not require the user to enter the password in chfn and chsh]),
  [], enable_require_password=yes
)

if test x$enable_require_password = xyes; then
  AC_DEFINE(REQUIRE_PASSWORD, 1, [Should chfn and chsh require the user to enter the password?])
fi


AC_ARG_ENABLE([use-tty-group],
  AS_HELP_STRING([--disable-use-tty-group], [do not install wall and write setgid tty]),
  [], enable_use_tty_group=yes
)
AM_CONDITIONAL(USE_TTY_GROUP, test x$enable_use_tty_group = xyes)

if test x$enable_use_tty_group = xyes; then
  AC_DEFINE(USE_TTY_GROUP, 1, [Should wall and write be installed setgid tty?])
fi

AC_ARG_ENABLE([makeinstall-chown],
  AS_HELP_STRING([--disable-makeinstall-chown], [do not do chown-like operations during "make install"]),
  [], enable_makeinstall_chown=yes
)
AM_CONDITIONAL(MAKEINSTALL_DO_CHOWN, test x$enable_makeinstall_chown = xyes)


AC_ARG_VAR([SUID_CFLAGS],
	   [CFLAGS used for binaries which are usually with the suid bit])
AC_ARG_VAR([SUID_LDFLAGS],
	   [LDFLAGS used for binaries which are usually with the suid bit])

LIBS=""


AC_CONFIG_HEADERS(config.h)

AC_CONFIG_FILES([
Makefile
disk-utils/Makefile
fdisk/Makefile
getopt/Makefile
hwclock/Makefile
include/Makefile
login-utils/Makefile
misc-utils/Makefile
mount/Makefile
partx/Makefile
po/Makefile.in
schedutils/Makefile
sys-utils/Makefile
text-utils/Makefile
tests/Makefile
tests/helpers/Makefile
tests/commands.sh
misc-utils/chkdupexe:misc-utils/chkdupexe.pl
misc-utils/scriptreplay:misc-utils/scriptreplay.pl
])

AC_OUTPUT
