AC_INIT(util-linux,
	m4_esyscmd([tools/git-version-gen .tarball-version]),
	kzak@redhat.com)

AC_PREREQ(2.60)

AC_CONFIG_AUX_DIR(config)
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([-Wall foreign 1.10 tar-pax dist-bzip2 no-dist-gzip dist-xz -Wno-portability subdir-objects])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])],
			    [AC_SUBST([AM_DEFAULT_VERBOSITY], [1])])

AC_CONFIG_SRCDIR(sys-utils/mount.c)
AC_PREFIX_DEFAULT([/usr])

dnl version details from <major>.<minor>[-<suffix>]
PACKAGE_VERSION_MAJOR=$(echo $PACKAGE_VERSION | awk -F. '{print $1}')
PACKAGE_VERSION_MINOR=$(echo $PACKAGE_VERSION | awk -F. '{print $2}' \
					      | awk -F- '{print $1}')
PACKAGE_VERSION_RELEASE=0

dnl libblkid version
LIBBLKID_VERSION="$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR.$PACKAGE_VERSION_RELEASE"
LIBBLKID_DATE="15-Aug-2012"
LIBBLKID_LT_MAJOR=1
LIBBLKID_LT_MINOR=1
LIBBLKID_LT_MICRO=0
LIBBLKID_VERSION_INFO=`expr $LIBBLKID_LT_MAJOR + $LIBBLKID_LT_MINOR`:$LIBBLKID_LT_MICRO:$LIBBLKID_LT_MINOR

dnl libuuid version
LIBUUID_VERSION="$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR.$PACKAGE_VERSION_RELEASE"
LIBUUID_LT_MAJOR=1
LIBUUID_LT_MINOR=3
LIBUUID_LT_MICRO=0
LIBUUID_VERSION_INFO=`expr $LIBUUID_LT_MAJOR + $LIBUUID_LT_MINOR`:$LIBUUID_LT_MICRO:$LIBUUID_LT_MINOR

dnl libmount version
LIBMOUNT_VERSION="$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR.$PACKAGE_VERSION_RELEASE"
LIBMOUNT_LT_MAJOR=1
LIBMOUNT_LT_MINOR=1
LIBMOUNT_LT_MICRO=0
LIBMOUNT_VERSION_INFO=`expr $LIBMOUNT_LT_MAJOR + $LIBMOUNT_LT_MINOR`:$LIBMOUNT_LT_MICRO:$LIBMOUNT_LT_MINOR

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
  esac
  case $libdir in
  '${exec_prefix}/lib') libdir=/lib
    AC_MSG_NOTICE([  --libdir defaults to /lib]) ;;
  esac ;;
esac

case $prefix:$localstatedir in
  NONE:'${prefix}/var')
    localstatedir=/run
    AC_MSG_NOTICE([  --localstatedir defaults to /run])
    ;;
  *:'${prefix}/var')
    localstatedir=${prefix}/run
    ;;
  NONE:*)
    ;;
  *:*)
    localstatedir=${prefix}/${localstatedir}
    ;;
esac
AC_SUBST([localstatedir])

libdirname=`basename "$libdir"`
AC_SUBST([libdirname])

# The original default values of {bin,sbin,lib}dir
usrbin_execdir='${exec_prefix}/bin'
AC_SUBST([usrbin_execdir])

usrsbin_execdir='${exec_prefix}/sbin'
AC_SUBST([usrsbin_execdir])

usrlib_execdir='${exec_prefix}/'$libdirname
AC_SUBST([usrlib_execdir])


AM_PROG_CC_C_O
AC_PROG_CC_STDC
AC_GNU_SOURCE
AC_CANONICAL_HOST
AC_C_CONST
AC_C_VOLATILE
AC_C_BIGENDIAN

dnl Compiler warnings
UL_WARN_ADD([-fno-common])
UL_WARN_ADD([-Wall])
UL_WARN_ADD([-Werror=sequence-point])
UL_WARN_ADD([-Wextra])
UL_WARN_ADD([-Wmissing-declarations])
UL_WARN_ADD([-Wmissing-parameter-type])
UL_WARN_ADD([-Wmissing-prototypes])
UL_WARN_ADD([-Wno-missing-field-initializers])
UL_WARN_ADD([-Wredundant-decls])
UL_WARN_ADD([-Wsign-compare])
UL_WARN_ADD([-Wtype-limits])
UL_WARN_ADD([-Wuninitialized])
UL_WARN_ADD([-Wunused-but-set-parameter])
UL_WARN_ADD([-Wunused-but-set-variable])
UL_WARN_ADD([-Wunused-parameter])
UL_WARN_ADD([-Wunused-result])
UL_WARN_ADD([-Wunused-variable])
AC_SUBST([WARN_CFLAGS])

dnl libtool-2
LT_INIT

m4_ifndef([PKG_PROG_PKG_CONFIG],
  [m4_fatal([Could not locate the pkg-config autoconf
    macros. These are usually located in /usr/share/aclocal/pkg.m4.
    If your macros are in a different location, try setting the
    environment variable AL_OPTS="-I/other/macro/dir" before running
    ./autogen.sh or autoreconf again.])])
PKG_PROG_PKG_CONFIG

GTK_DOC_CHECK([1.10])
AC_PATH_PROG([XSLTPROC], [xsltproc])

linux_os=no
case ${host_os} in
  *linux*)
    linux_os=yes
    ;;
esac
AM_CONDITIONAL([LINUX], test "x$linux_os" = xyes)

dnl define ARCH_<NAME> conditionals
UL_SET_ARCH(I86, i?86-*)
UL_SET_ARCH(86_64, x86_64*)
UL_SET_ARCH(IA64, ia64*)
UL_SET_ARCH(S390, s390*)
UL_SET_ARCH(SPARC, sparc*)
UL_SET_ARCH(PPC, ppc*|powerpc*)
UL_SET_ARCH(M68K, m68*)
UL_SET_ARCH(MIPS, mips*)
UL_SET_ARCH(HPPA, hppa*)

AC_SYS_LARGEFILE

AM_GNU_GETTEXT_VERSION([0.14.1])
AM_GNU_GETTEXT([external])
if test -d "$srcdir/po"
then
    ALL_LINGUAS=`cd $srcdir/po > /dev/null && echo *.po | sed 's/\.po//g'`
else
    ALL_LINGUAS="af am ar as be bg bn_IN bn ca cs cy da de el en_GB es et eu_ES fa fi fr gl gu he hi hr hu hy id is it ja ka kn ko ku lo lt lv mk ml mr ms my nb nl nn no nso or pa pl pt_BR pt ro ru si sk sl sq sr@Latn sr sv ta te th tr uk ur vi zh_CN zh_TW zu"
fi

AC_CHECK_HEADERS([linux/compiler.h linux/blkpg.h linux/major.h], [], [], [
#ifdef HAVE_LINUX_COMPILER_H
#include <linux/compiler.h>
#endif
])
AC_CHECK_HEADERS([ \
	asm/io.h \
	err.h \
	errno.h \
	fcntl.h \
	getopt.h \
	inttypes.h \
	linux/cdrom.h \
	linux/falloc.h \
	linux/watchdog.h \
	linux/fd.h \
	linux/raw.h \
	linux/tiocl.h \
	linux/version.h \
	locale.h \
	mntent.h \
	net/if.h \
	net/if_dl.h \
	netinet/in.h \
	paths.h \
	pty.h \
	security/pam_misc.h \
	stdint.h \
	stdio_ext.h \
	stdlib.h \
	sys/disk.h \
	sys/disklabel.h \
	sys/file.h \
	sys/io.h \
	sys/ioccom.h \
	sys/ioctl.h \
	sys/mkdev.h \
	sys/prctl.h \
	sys/queue.h \
	sys/resource.h \
	sys/socket.h \
	sys/sockio.h \
	sys/stat.h \
	sys/swap.h \
	sys/syscall.h \
	sys/time.h \
	sys/types.h \
	sys/un.h \
	unistd.h \
])

AC_CHECK_HEADERS([langinfo.h],
		[AM_CONDITIONAL([HAVE_LANGINFO], [true])],
		[AM_CONDITIONAL([HAVE_LANGINFO], [false])])

dnl Convert some ac_cv_header_* variables to have_*
dnl
have_linux_raw_h=$ac_cv_header_linux_raw_h
have_linux_watchdog_h=$ac_cv_header_linux_watchdog_h
have_security_pam_misc_h=$ac_cv_header_security_pam_misc_h


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

AC_CHECK_MEMBERS([struct termios.c_line],,,
    [[#include <termios.h>]])

AC_CHECK_MEMBERS([struct stat.st_mtim.tv_nsec],,,
	[#include <sys/stat.h>])

AC_CHECK_DECLS([
 UNAME26,
 ADDR_NO_RANDOMIZE,
 FDPIC_FUNCPTRS,
 MMAP_PAGE_ZERO,
 ADDR_COMPAT_LAYOUT,
 READ_IMPLIES_EXEC,
 ADDR_LIMIT_32BIT,
 WHOLE_SECONDS,
 STICKY_TIMEOUTS,
 ADDR_LIMIT_3GB], [], [], [#include <linux/personality.h>])

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


AC_CHECK_DECLS([_NL_TIME_WEEK_1STDAY],[],[],[[#include <langinfo.h>]])

AC_CHECK_DECL([llseek],
	[AC_DEFINE(HAVE_LLSEEK_PROTOTYPE, 1,
			[Define to 1 if have llseek prototype])],
	[],
	[#include <unistd.h>])

AC_CHECK_DECL([lseek64],
	[AC_DEFINE(HAVE_LSEEK64_PROTOTYPE, 1,
			[Define to 1 if have lseek64 prototype])],
	[],
	[#define _LARGEFILE_SOURCE
	 #define _LARGEFILE64_SOURCE
	 #include <unistd.h>])

AC_CHECK_DECL([environ],
	[AC_DEFINE(HAVE_ENVIRON_DECL, 1,
		[Define to 1 if have **environ prototype])],
)

AC_CHECK_DECL([strsignal],
	[AC_DEFINE(HAVE_STRSIGNAL_DECL, 1,
		[Define to 1 if have strsignal function prototype])],
)

AC_CHECK_FUNCS([ \
	__fpending \
	__secure_getenv \
	err \
	errx \
	fsync \
	futimens \
	getdomainname \
	getdtablesize \
	getexecname \
	getmntinfo \
	getrlimit \
	inotify_init \
	jrand48 \
	lchown \
	llseek \
	lseek64 \
	nanosleep \
	personality \
	posix_fadvise \
	prctl \
	rpmatch \
	scandirat \
	setresgid \
	setresuid \
	sigqueue \
	srandom \
	strnchr \
	strndup \
	strnlen \
	strtoull \
	sysconf \
	updwtmp \
	usleep \
	warn \
	warnx \
])
AC_FUNC_FSEEKO

AC_CHECK_FUNCS([openat fstatat unlinkat], [have_openat=yes], [have_openat=no])
AC_CHECK_FUNCS([ioperm iopl], [have_io=yes])

AC_CHECK_MEMBER(struct sockaddr.sa_len,
		AC_DEFINE_UNQUOTED(HAVE_SA_LEN,1,[Define if struct sockaddr contains sa_len]),,
	[#include <sys/types.h>
	 #include <sys/socket.h>])

SOCKET_LIBS=
AC_SEARCH_LIBS([gethostbyname], [nsl],
	[if test x"$ac_cv_search_gethostbyname" != x"none required"; then
		SOCKET_LIBS="$SOCKET_LIBS -lnsl";
	 fi])
AC_SEARCH_LIBS([socket], [socket],
	[if test x"$ac_cv_search_socket" != x"none required"; then
		SOCKET_LIBS="$SOCKET_LIBS -lsocket";
	fi])
AC_SUBST([SOCKET_LIBS])


have_dirfd=no
AC_CHECK_FUNCS([dirfd], [have_dirfd=yes], [have_dirfd=no])
if test x"$have_dirfd" = xno ; then
	AC_CHECK_DECLS([dirfd],
		[have_dirfd=yes], [have_dirfd=no],
		[#include <sys/types.h>
		 #include <dirent.h>])
fi

have_ddfd=no
if test x"$have_dirfd" = xno ; then
	AC_CHECK_MEMBERS([DIR.dd_fd],
		[have_ddfd=yes], [have_ddfd=no],
		[#include <sys/types.h>
		 #include <dirent.h>])
fi

case "$have_dirfd:$have_ddfd" in
no:no)
  AC_MSG_ERROR([cannot find a method to get filedescriptor of directory]) ;;
esac


AC_MSG_CHECKING(whether program_invocation_short_name is defined)
AC_TRY_COMPILE([#include <argp.h>],
	       [program_invocation_short_name = "test";],
	       AC_DEFINE(HAVE_PROGRAM_INVOCATION_SHORT_NAME, 1,
			 [Define if program_invocation_short_name is defined])
	       AC_MSG_RESULT(yes),
	       AC_MSG_RESULT(no))

AC_MSG_CHECKING([whether __progname is defined])
AC_LINK_IFELSE([AC_LANG_PROGRAM([extern char *__progname;],
	   [if (*__progname == 0) return;])],
	   AC_DEFINE(HAVE___PROGNAME, 1, [Define if __progname is defined])
	   AC_MSG_RESULT(yes),
	   AC_MSG_RESULT(no))

dnl Static compilation
m4_define([UL_STATIC_PROGRAMS], [losetup, mount, umount, fdisk, sfdisk, blkid])

AC_ARG_ENABLE([static-programs],
  [AS_HELP_STRING([--enable-static-programs=LIST],
      [link static the programs in LIST (comma-separated,
      supported for ]m4_defn([UL_STATIC_PROGRAMS])[)])])

case $enable_static_programs in
yes) enable_static_programs=m4_quote(UL_STATIC_PROGRAMS) ;;
no) enable_static_programs= ;;
esac

dnl Set all the individual AM_CONDITIONALs
m4_foreach([UL_PRG], m4_defn([UL_STATIC_PROGRAMS]), [
  case ,$enable_static_programs, in
   *,UL_PRG,*) static_[]UL_PRG=yes ;;
  esac
  AM_CONDITIONAL([HAVE_STATIC_]m4_toupper(UL_PRG),
    [test "x$static_[]UL_PRG" = xyes])
])

AX_CHECK_TLS

AC_DEFUN([UL_SCANF_TYPE_MODIFIER], [dnl
# include <stdio.h>
int main()
{
	int i;
	char *s;
	i = sscanf("x", $1, &s);
	if (i == 1)
		return 0;
	return 1;
}])

AC_CACHE_VAL([scanf_cv_alloc_modifier],
  AC_RUN_IFELSE([AC_LANG_SOURCE([UL_SCANF_TYPE_MODIFIER(["%ms"])])],
    [scanf_cv_alloc_modifier=ms],
    AC_RUN_IFELSE([AC_LANG_SOURCE([UL_SCANF_TYPE_MODIFIER(["%as"])])],
      [scanf_cv_alloc_modifier=as],
      [scanf_cv_alloc_modifier=no]
    )
    ,

    [AC_COMPILE_IFELSE([AC_LANG_SOURCE([
     #include <stdio.h>

     #ifdef __GLIBC__

     #if !(__GLIBC_PREREQ(2, 7))
     #error %m is not available
     #endif

     #else

     #error Your C-library is not supported.
     #endif
     ])],
    [scanf_cv_alloc_modifier=ms],
    [scanf_cv_alloc_modifier=no])]
  )
)

AC_MSG_CHECKING([scanf string alloc modifiers])
case "$scanf_cv_alloc_modifier" in
ms)
  AC_MSG_RESULT([(%ms) yes])
  AC_DEFINE([HAVE_SCANF_MS_MODIFIER], [1], [scanf %ms modifier])
  have_scanf_alloc_modifier=yes
  ;;
as)
  AC_MSG_RESULT([(%as) yes])
  have_scanf_alloc_modifier=yes
  AC_DEFINE([HAVE_SCANF_AS_MODIFIER], [1], [scanf %as modifier])
  ;;
*)
  AC_MSG_RESULT([no])
  have_scanf_alloc_modifier=no
  ;;
esac

UL_CHECK_LIB(util, openpty)
UL_CHECK_LIB(termcap, tgetnum)

AC_CHECK_TYPES([union semun], [], [], [[
#include <sys/sem.h>
]])
AC_CHECK_TYPES(loff_t)

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

AC_CHECK_TYPES([cpu_set_t], [have_cpu_set_t=yes], [], [[
#include <sched.h>
]])

AM_CONDITIONAL(HAVE_CPU_SET_T, [test "x$have_cpu_set_t" = xyes])

AC_CHECK_DECLS([CPU_ALLOC], [], [], [[
#include <sched.h>
]])

# on Solaris, you can't mix and match standards, since we use c99
# aparently at this stage, XOPEN_SOURCE will conflict.  As workaround,
# check for crypt.h and use that without XOPEN_SOURCE.
AC_CHECK_HEADERS([crypt.h])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#ifdef HAVE_CRYPT_H
#include <crypt.h>
#else
#define _XOPEN_SOURCE
#include <unistd.h>
#endif
]], [[
char *c = crypt("abc","pw");
]])],[],[
  LIBS="$LIBS -lcrypt"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
  #ifdef HAVE_CRYPT_H
  #include <crypt.h>
  #else
  #define _XOPEN_SOURCE
  #include <unistd.h>
  #endif
  ]], [[
  char *c = crypt("abc","pw");
  ]])],[
  AC_DEFINE(HAVE_LIBCRYPT, 1, [Do we need -lcrypt?])
  have_libcrypt=yes
  ],[
  AC_MSG_ERROR([crypt() is not available])
  ])
])
AM_CONDITIONAL(HAVE_LIBCRYPT, test "x$have_libcrypt" = xyes)


AC_ARG_WITH([selinux],
  AS_HELP_STRING([--with-selinux], [compile with SELinux support]),
  [], with_selinux=no
)

if test "x$with_selinux" = xno; then
  AM_CONDITIONAL(HAVE_SELINUX, false)
else
  UL_CHECK_LIB(selinux, getprevcon)
  case "$with_selinux:$have_selinux" in
  yes:no) AC_MSG_ERROR([SELinux selected but libselinux not found]);;
  esac
fi

if test "x$have_selinux" = xyes; then
  SELINUX_LIBS="-lselinux -lsepol"
  SELINUX_LIBS_STATIC="-lselinux -lsepol"
  old_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $SELINUX_LIBS"
  # This function is missing in old libselinux 1.xx versions
  AC_CHECK_FUNCS([security_get_initial_context])
  LDFLAGS="$old_LDFLAGS"
fi
AC_SUBST([SELINUX_LIBS])
AC_SUBST([SELINUX_LIBS_STATIC])

AC_ARG_WITH([audit],
  AS_HELP_STRING([--with-audit], [compile with audit support]),
  [], with_audit=no
)

if test "x$with_audit" = xno; then
  AM_CONDITIONAL(HAVE_AUDIT, false)
else
  UL_CHECK_LIB(audit, audit_log_user_message)
  case "$with_audit:$have_audit" in
  yes:no)
   AC_MSG_ERROR([Audit selected but libaudit not found (or does not support audit_log_user_message())])
   ;;
  esac
fi

AC_ARG_WITH([udev], AS_HELP_STRING([--without-udev], [compile without udev support]),
  [], with_udev=auto
)

if test "x$with_udev" = xno; then
  AM_CONDITIONAL(HAVE_UDEV, false)
else
  UL_CHECK_LIB(udev, udev_new)
  case "$with_udev:$have_udev" in
  yes:no)
   AC_MSG_ERROR([udev selected but libudev not found])
   ;;
  esac
fi

AC_ARG_WITH([ncurses],
  AS_HELP_STRING([--with-ncurses], [build with non-wide ncurses, default is wide version
				    (--without-ncurses disables all ncurses(w) support)]),
  [], with_ncurses=auto
)
AM_CONDITIONAL(HAVE_NCURSES, false)

if test "x$with_ncurses" != xno; then
  have_ncurses=no
  AC_CHECK_HEADERS([ncurses.h ncurses/ncurses.h], [
    if test "x$with_ncurses" = xauto; then
      UL_CHECK_LIB(ncursesw, initscr, ncurses)
      if test "x$have_ncurses" = xyes; then
	AC_CHECK_HEADERS([ncursesw/ncurses.h])
	NCURSES_LIBS="-lncursesw"
      fi
    fi
    if test "x$have_ncurses" = xno; then
      UL_CHECK_LIB(ncurses, initscr)
      if test "x$have_ncurses" = xyes; then
	NCURSES_LIBS="-lncurses"
      fi
    fi
  ])
  if test "x$have_ncurses" = xno; then
    AC_MSG_ERROR([ncurses or ncursesw selected, but library not found (--without-ncurses to disable)])
  fi
fi
AC_SUBST([NCURSES_LIBS])


AC_ARG_WITH([slang],
  AS_HELP_STRING([--with-slang], [compile cfdisk with slang]),
  [], with_slang=no
)
have_slang=no
if test "x$with_slang" = xyes; then
  AC_CHECK_HEADERS([slang.h slang/slang.h])
  AC_CHECK_HEADERS([slcurses.h slang/slcurses.h],
		   [have_slang=yes], [], [
#ifdef HAVE_SLANG_H
#include <slang.h>
#elif defined(HAVE_SLANG_SLANG_H)
#include <slang/slang.h>
#endif
])
  if test "x$have_slang" = xno; then
    AC_MSG_ERROR([slang selected but slcurses.h not found])
  fi
fi
AM_CONDITIONAL(HAVE_SLANG, test "x$have_slang" = xyes)


AM_CONDITIONAL(BUILD_CFDISK, test "x$have_slang" = xyes -o "x$have_ncurses" = xyes)


have_tinfo=no
AC_CHECK_LIB(tinfo, tgetent, [have_tinfo=yes])
AM_CONDITIONAL(HAVE_TINFO, test "x$have_tinfo" = xyes)


AC_ARG_WITH([utempter],
  AS_HELP_STRING([--with-utempter], [compile script(1) with libutempter]),
  [], with_utempter=no
)

if test "x$with_utempter" = xyes; then
  UL_CHECK_LIB(utempter, utempter_add_record)
  if test "x$have_utempter" = xno; then
    AC_MSG_ERROR([utempter selected but libutempter not found])
  fi
else
  AM_CONDITIONAL(HAVE_UTEMPTER, false)
fi


AC_ARG_ENABLE([most-builds],
  AS_HELP_STRING([--enable-most-builds], [build everything other than experimental code]),
  [], enable_most_builds=no
)
if test "x$enable_most_builds" = xyes; then
  enable_arch=check
  enable_ddate=yes
  enable_elvtune=check
  enable_last=yes
  enable_line=yes
  enable_mesg=yes
  enable_reset=yes
  enable_vipw=yes
  enable_chfn_chsh=yes
  enable_newgrp=yes
  enable_write=yes
fi

dnl
dnl libuuid
dnl
AC_ARG_ENABLE([libuuid],
  AS_HELP_STRING([--disable-libuuid], [do not build libuuid and uuid utilities]),
  [], enable_libuuid=yes
)
UL_BUILD_INIT([libuuid])
AC_SUBST([LIBUUID_VERSION])
AC_SUBST([LIBUUID_VERSION_INFO])
AM_CONDITIONAL(BUILD_LIBUUID, test "x$build_libuuid" = xyes)

if test "x$enable_libuuid" = xyes; then
  AC_DEFINE(HAVE_LIBUUID, 1, [Define to 1 if you have the -luuid.])
else
  AC_MSG_WARN([uuid library is not found; mkswap(8) will not generate UUIDs])
fi

dnl
dnl libblkid
dnl
AC_ARG_ENABLE([libblkid],
  AS_HELP_STRING([--disable-libblkid], [do not build libblkid and many related utilities]),
  [], enable_libblkid=yes
)
UL_BUILD_INIT([libblkid])
AC_SUBST([LIBBLKID_DATE])
AC_SUBST([LIBBLKID_VERSION])
AC_SUBST([LIBBLKID_VERSION_INFO])
AC_DEFINE_UNQUOTED(LIBBLKID_VERSION, "$LIBBLKID_VERSION", [libblkid version string])
AC_DEFINE_UNQUOTED(LIBBLKID_DATE, "$LIBBLKID_DATE", [libblkid date string])
if test "x$build_libblkid" = xyes; then
  AC_DEFINE(HAVE_LIBBLKID, 1, [Define to 1 if you have the -lblkid.])
fi
AM_CONDITIONAL(BUILD_LIBBLKID, test "x$build_libblkid" = xyes)
AM_CONDITIONAL(BUILD_LIBBLKID_TESTS, test "x$build_libblkid" = xyes -a "x$enable_static" = xyes)

dnl
dnl libmount
dnl
AC_ARG_ENABLE([libmount],
  AS_HELP_STRING([--disable-libmount], [do not build libmount]),
  [], enable_libmount=check
)
UL_BUILD_INIT([libmount])
UL_REQUIRES_LINUX([libmount])
UL_REQUIRES_BUILD([libmount], [libblkid])
UL_REQUIRES_HAVE([libmount], [scanf_alloc_modifier], [scanf string alloc modifier])
AM_CONDITIONAL(BUILD_LIBMOUNT, test "x$build_libmount" = xyes)
AM_CONDITIONAL(BUILD_LIBMOUNT_TESTS, test "x$build_libmount" = xyes -a "x$enable_static" = xyes)

AC_SUBST([LIBMOUNT_VERSION])
AC_SUBST([LIBMOUNT_VERSION_INFO])
AC_DEFINE_UNQUOTED(LIBMOUNT_VERSION, "$LIBMOUNT_VERSION", [libmount version string])


AC_ARG_ENABLE([deprecated-mount],
  AS_HELP_STRING([--enable-deprecated-mount], [build old mount utilities]),
  [], enable_deprecated_mount=no
)
UL_BUILD_INIT([deprecated_mount])
UL_REQUIRES_LINUX([deprecated_mount])
UL_REQUIRES_BUILD([deprecated_mount], [libblkid])
AM_CONDITIONAL(BUILD_DEPRECATED_MOUNT, test "x$build_deprecated_mount" = xyes)


AC_ARG_ENABLE([mount],
  AS_HELP_STRING([--disable-mount], [do not build mount(8) and umount(8)]),
  [], enable_mount=check
)
UL_BUILD_INIT([mount])
UL_REQUIRES_BUILD([mount], [libmount])
UL_CONFLICTS_BUILD([mount], [deprecated_mount], [old deprecated mount version])
AM_CONDITIONAL(BUILD_MOUNT, test "x$build_mount" = xyes)


AC_ARG_ENABLE([losetup],
  AS_HELP_STRING([--disable-losetup], [do not build losetup]),
  [], enable_losetup=check
)
UL_BUILD_INIT([losetup])
UL_REQUIRES_LINUX([losetup])
AM_CONDITIONAL(BUILD_LOSETUP, test "x$build_losetup" = xyes)


AC_ARG_ENABLE([fsck],
  AS_HELP_STRING([--disable-fsck], [do not build fsck]),
  [], enable_fsck=check
)
UL_BUILD_INIT([fsck])
UL_REQUIRES_BUILD([fsck], [libmount])
AM_CONDITIONAL(BUILD_FSCK, test "x$build_fsck" = xyes)


AC_ARG_ENABLE([partx],
  AS_HELP_STRING([--disable-partx], [do not build addpart, delpart, partx]),
  [], enable_partx=check
)
UL_BUILD_INIT([partx])
UL_REQUIRES_LINUX([partx])
UL_REQUIRES_BUILD([partx], [libblkid])
AM_CONDITIONAL(BUILD_PARTX, test "x$build_partx" = xyes)


AC_ARG_ENABLE([uuidd],
  AS_HELP_STRING([--disable-uuidd], [do not build the uuid daemon]),
  [], enable_uuidd=check
)
UL_BUILD_INIT([uuidd])
UL_REQUIRES_BUILD([uuidd], [libuuid])
if test "x$build_uuidd" = xyes; then
  AC_DEFINE(HAVE_UUIDD, 1, [Define to 1 if you want to use uuid daemon.])
fi
AM_CONDITIONAL(BUILD_UUIDD, test "x$build_uuidd" = xyes)


AC_ARG_ENABLE([mountpoint],
  AS_HELP_STRING([--disable-mountpoint], [do not build mountpoint]),
  [], enable_mountpoint=check
)
UL_BUILD_INIT([mountpoint])
UL_REQUIRES_BUILD([mountpoint], [libmount])
AM_CONDITIONAL(BUILD_MOUNTPOINT, test "x$build_mountpoint" = xyes)


AC_ARG_ENABLE([fallocate],
  AS_HELP_STRING([--disable-fallocate], [do not build fallocate]),
  [], enable_fallocate=check
)
UL_BUILD_INIT([fallocate])
UL_REQUIRES_LINUX([fallocate])
UL_REQUIRES_SYSCALL_CHECK([fallocate], [UL_CHECK_SYSCALL([fallocate])])
AM_CONDITIONAL(BUILD_FALLOCATE, test "x$build_fallocate" = xyes)

if test "x$build_fallocate" = xyes; then
  dnl check for valid fallocate() function
  dnl with 32 bits glibc 2.10, fallocate() exists but not fallocate64()
  dnl when _FILE_OFFSET_BITS==64, fallocate() is redirect to fallocate64()
  dnl and program can't be linked.
  dnl AC_CHECK_FUNC can't catch such errors since it's redefining
  dnl function prototype.
  AC_MSG_CHECKING([for valid fallocate() function])
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_LINUX_FALLOC_H
# include <linux/falloc.h>
#endif
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
]],[[
   long ret;
   ret = fallocate(0, FALLOC_FL_KEEP_SIZE, 0xfffffffful, 0xfffffffful);
   if (ret != 0) {
      return 1;
   }
   ]])],[
  AC_MSG_RESULT([yes])
  AC_DEFINE(HAVE_FALLOCATE,1,[Have valid fallocate() function])],[
  AC_MSG_RESULT([no])])
fi


AC_ARG_ENABLE([unshare],
  AS_HELP_STRING([--disable-unshare], [do not build unshare]),
  [], enable_unshare=check
)
UL_BUILD_INIT([unshare])
UL_REQUIRES_LINUX([unshare])
UL_REQUIRES_SYSCALL_CHECK([unshare], [UL_CHECK_SYSCALL([unshare])])
AM_CONDITIONAL(BUILD_UNSHARE, test "x$build_unshare" = xyes)
if test "x$build_unshare" = xyes; then
  AC_CHECK_FUNCS([unshare])
fi


AC_ARG_ENABLE([arch],
  AS_HELP_STRING([--enable-arch], [do build arch]),
  [], enable_arch=no
)
UL_BUILD_INIT([arch])
UL_REQUIRES_LINUX([arch])
AM_CONDITIONAL(BUILD_ARCH, test "x$build_arch" = xyes)


AC_ARG_ENABLE([ddate],
  AS_HELP_STRING([--enable-ddate], [do build ddate]),
  [], enable_ddate=no
)
UL_BUILD_INIT([ddate])
AM_CONDITIONAL(BUILD_DDATE, test "x$build_ddate" = xyes)
ruman1dir='${mandir}/ru/man1'
AC_SUBST([ruman1dir])


AC_ARG_ENABLE([eject],
  AS_HELP_STRING([--disable-eject], [do not build eject]),
  [], enable_eject=check
)
UL_BUILD_INIT([eject])
UL_REQUIRES_LINUX([eject])
UL_REQUIRES_BUILD([eject], [libmount])
AM_CONDITIONAL(BUILD_EJECT, test "x$build_eject" = xyes)


AC_ARG_ENABLE([agetty],
  AS_HELP_STRING([--disable-agetty], [do not build agetty]),
  [], enable_agetty=yes
)
UL_BUILD_INIT([agetty])
AM_CONDITIONAL(BUILD_AGETTY, test "x$build_agetty" = xyes)


AC_ARG_ENABLE([cramfs],
  AS_HELP_STRING([--disable-cramfs], [do not build fsck.cramfs, mkfs.cramfs]),
  [], enable_cramfs=check
)
UL_BUILD_INIT([cramfs])
AC_CHECK_LIB(z, crc32, [have_z=yes], [have_z=no])
UL_REQUIRES_HAVE([cramfs], [z], [z library])
AM_CONDITIONAL(BUILD_CRAMFS, test "x$build_cramfs" = xyes)


if test "x$have_io" = xyes -o "x$linux_os" = xyes; then
  build_hwclock=yes
fi
AM_CONDITIONAL(BUILD_HWCLOCK, test "x$build_hwclock" = xyes)


UL_BUILD_INIT([swapon], [check])
UL_REQUIRES_LINUX([swapon])
UL_REQUIRES_BUILD([swapon], [libblkid])
UL_REQUIRES_BUILD([swapon], [libmount])
AM_CONDITIONAL(BUILD_SWAPON, test "x$build_swapon" = xyes)


UL_BUILD_INIT([lsblk], [check])
UL_REQUIRES_LINUX([lsblk])
UL_REQUIRES_BUILD([lsblk], [libblkid])
UL_REQUIRES_BUILD([lsblk], [libmount])
AM_CONDITIONAL(BUILD_LSBLK, test "x$build_lsblk" = xyes)


UL_BUILD_INIT([lscpu], [check])
UL_REQUIRES_LINUX([lscpu])
UL_REQUIRES_HAVE([lscpu], [cpu_set_t], [cpu_set_t type])
AM_CONDITIONAL(BUILD_LSCPU, test "x$build_lscpu" = xyes)


UL_BUILD_INIT([chcpu], [check])
UL_REQUIRES_LINUX([chcpu])
UL_REQUIRES_HAVE([chcpu], [cpu_set_t], [cpu_set_t type])
AM_CONDITIONAL(BUILD_CHCPU, test "x$build_chcpu" = xyes)


UL_BUILD_INIT([wdctl], [check])
UL_REQUIRES_LINUX([wdctl])
UL_REQUIRES_HAVE([wdctl], [linux_watchdog_h], [linux/watchdog.h header file])
AM_CONDITIONAL(BUILD_WDCTL, test "x$build_wdctl" = xyes)


UL_BUILD_INIT([swaplabel], [check])
UL_REQUIRES_BUILD([swaplabel], [libblkid])
AM_CONDITIONAL(BUILD_SWAPLABEL, test "x$build_swaplabel" = xyes)


UL_BUILD_INIT([prlimit], [check])
UL_REQUIRES_LINUX([prlimit])
UL_REQUIRES_SYSCALL_CHECK([prlimit], [UL_CHECK_SYSCALL([prlimit64])], [prlimit64])
AM_CONDITIONAL(BUILD_PRLIMIT, test "x$build_prlimit" = xyes)
if test "x$build_prlimit" = xyes; then
  AC_CHECK_FUNCS([prlimit])
fi


UL_BUILD_INIT([lslocks], [check])
UL_REQUIRES_LINUX([lslocks])
AM_CONDITIONAL(BUILD_LSLOCKS, test "x$build_lslocks" = xyes)


AC_ARG_ENABLE([switch_root],
  AS_HELP_STRING([--disable-switch_root], [do not build switch_root]),
  [], enable_switch_root=check
)
UL_BUILD_INIT([switch_root])
UL_REQUIRES_LINUX([switch_root])
UL_REQUIRES_HAVE([switch_root], [openat], [openat function])
AM_CONDITIONAL(BUILD_SWITCH_ROOT, test "x$build_switch_root" = xyes)


AC_ARG_ENABLE([pivot_root],
  AS_HELP_STRING([--disable-pivot_root], [do not build pivot_root]),
  [], enable_pivot_root=check
)
UL_BUILD_INIT([pivot_root])
UL_REQUIRES_LINUX([switch_root])
UL_REQUIRES_SYSCALL_CHECK([pivot_root], [UL_CHECK_SYSCALL([pivot_root])])
AM_CONDITIONAL(BUILD_PIVOT_ROOT, test "x$build_pivot_root" = xyes)


AC_ARG_ENABLE([elvtune],
  AS_HELP_STRING([--enable-elvtune], [build elvtune (only works with 2.2 and 2.4 kernels)]),
  [], enable_elvtune=no
)
UL_BUILD_INIT([elvtune])
UL_REQUIRES_LINUX([elvtune])
AM_CONDITIONAL(BUILD_ELVTUNE, test "x$build_elvtune" = xyes)


AC_ARG_ENABLE([kill],
  AS_HELP_STRING([--disable-kill], [do not build kill]),
  [], enable_kill=check
)
UL_BUILD_INIT([kill])
UL_REQUIRES_LINUX([kill])
AM_CONDITIONAL(BUILD_KILL, test "x$build_kill" = xyes)


AC_ARG_ENABLE([last],
  AS_HELP_STRING([--enable-last], [build last]),
  [], enable_last=no
)
UL_BUILD_INIT([last])
AM_CONDITIONAL(BUILD_LAST, test "x$build_last" = xyes)


AC_ARG_ENABLE([utmpdump],
  AS_HELP_STRING([--disable-utmpdump], [do not build utmpdump]),
  [], enable_utmpdump=yes
)
UL_BUILD_INIT([utmpdump])
AM_CONDITIONAL(BUILD_UTMPDUMP, test "x$build_utmpdump" = xyes)


AC_ARG_ENABLE([line],
  AS_HELP_STRING([--enable-line], [build line]),
  [], enable_line=no
)
UL_BUILD_INIT([line])
AM_CONDITIONAL(BUILD_LINE, test "x$build_line" = xyes)


AC_ARG_ENABLE([mesg],
  AS_HELP_STRING([--enable-mesg], [build mesg]),
  [], enable_mesg=no
)
UL_BUILD_INIT([mesg])
AM_CONDITIONAL(BUILD_MESG, test "x$build_mesg" = xyes)


AC_ARG_ENABLE([raw],
  AS_HELP_STRING([--enable-raw], [build raw]),
  [], enable_raw=check
)
UL_BUILD_INIT([raw])
UL_REQUIRES_LINUX([raw])
UL_REQUIRES_HAVE([raw], [linux_raw_h], [raw.h header file])
AM_CONDITIONAL(BUILD_RAW, test "x$build_raw" = xyes)


AC_ARG_ENABLE([rename],
  AS_HELP_STRING([--disable-rename], [do not build rename]),
  [], enable_rename=yes
)
UL_BUILD_INIT([rename])
AM_CONDITIONAL(BUILD_RENAME, test "x$build_rename" = xyes)


AC_ARG_ENABLE([reset],
  AS_HELP_STRING([--enable-reset], [build reset]),
  [], enable_reset=no
)
UL_BUILD_INIT([reset])
AM_CONDITIONAL(BUILD_RESET, test "x$build_reset" = xyes)


AC_ARG_ENABLE([vipw],
  AS_HELP_STRING([--enable-vipw], [build vipw]),
  [], enable_vipw=no
)
UL_BUILD_INIT([vipw])
AM_CONDITIONAL(BUILD_VIPW, test "x$build_vipw" = xyes)


AC_ARG_ENABLE([newgrp],
  AS_HELP_STRING([--enable-newgrp], [build newgrp]),
  [], enable_newgrp=no
)
UL_BUILD_INIT([newgrp])
AM_CONDITIONAL(BUILD_NEWGRP, test "x$build_newgrp" = xyes)


AC_ARG_ENABLE([chfn-chsh],
  AS_HELP_STRING([--enable-chfn-chsh], [build chfn and chsh]),
  [], enable_chfn_chsh=no
)
UL_BUILD_INIT([chfn_chsh])
UL_REQUIRES_HAVE([chfn_chsh], [security_pam_misc_h], [PAM header file])
AM_CONDITIONAL(BUILD_CHFN_CHSH, test "x$build_chfn_chsh" = xyes)

AC_ARG_ENABLE([chsh-only-listed],
AS_HELP_STRING([--disable-chsh-only-listed], [chsh: allow shells not in /etc/shells]),
[], enable_chsh_only_listed=yes
)

if test "x$enable_chsh_only_listed" = xyes; then
AC_DEFINE(ONLY_LISTED_SHELLS, 1, [Should chsh allow only shells in /etc/shells?])
fi


AC_ARG_ENABLE([login],
  AS_HELP_STRING([--disable-login], [do not build login]),
  [], enable_login=yes
)
UL_BUILD_INIT([login])
UL_REQUIRES_HAVE([login], [security_pam_misc_h], [PAM header file])
AM_CONDITIONAL(BUILD_LOGIN, test "x$build_login" = xyes)

AC_ARG_ENABLE([login-chown-vcs],
  AS_HELP_STRING([--enable-login-chown-vcs], [let login chown /dev/vcsN]),
  [], enable_login_chown_vcs=no
)

if test "x$enable_login_chown_vcs" = xyes; then
  AC_DEFINE(LOGIN_CHOWN_VCS, 1, [Should login chown /dev/vcsN?])
fi

AC_ARG_ENABLE([login-stat-mail],
  AS_HELP_STRING([--enable-login-stat-mail], [let login stat() the mailbox]),
  [], enable_login_stat_mail=no
)

if test "x$enable_login_stat_mail" = xyes; then
  AC_DEFINE(LOGIN_STAT_MAIL, 1, [Should login stat() the mailbox?])
fi


AC_ARG_ENABLE([sulogin],
  AS_HELP_STRING([--disable-sulogin], [do not build sulogin]),
  [], enable_sulogin=yes
)
UL_BUILD_INIT([sulogin])
AM_CONDITIONAL(BUILD_SULOGIN, test "x$build_sulogin" = xyes)


AC_ARG_ENABLE([su],
  AS_HELP_STRING([--disable-su], [do not build su]),
  [], enable_su=yes
)
UL_BUILD_INIT([su])
UL_REQUIRES_HAVE([su], [security_pam_misc_h], [PAM header file])
AM_CONDITIONAL(BUILD_SU, test "x$build_su" = xyes)


AC_ARG_ENABLE([schedutils],
  AS_HELP_STRING([--disable-schedutils], [do not build chrt, ionice, teskset]),
  [], enable_schedutils=yes
)
UL_BUILD_INIT([schedutils])
AM_CONDITIONAL(BUILD_SCHEDUTILS, test "x$build_schedutils" = xyes)

UL_BUILD_INIT([ionice], [check])
UL_REQUIRES_BUILD([ionice], [schedutils])
UL_REQUIRES_SYSCALL_CHECK([ionice],
	[UL_CHECK_SYSCALL([ioprio_set],
	  [alpha],	[442],
	  [i*86],	[289],
	  [ia64*],	[1274],
	  [powerpc*],	[273],
	  [s390*],	[282],
	  [sparc*],	[196],
	  [sh*],	[288],
	  [x86_64*],	[251])],
	[ioprio_set])

UL_REQUIRES_SYSCALL_CHECK([ionice],
	[UL_CHECK_SYSCALL([ioprio_get],
	  [alpha],	[443],
	  [i*86],	[290],
	  [ia64*],	[1275],
	  [powerpc*],	[274],
	  [s390*],	[283],
	  [sparc*],	[218],
	  [sh*],	[289],
	  [x86_64*],	[252])],
	[ioprio_get])

AM_CONDITIONAL(BUILD_IONICE, test "x$build_ionice" = xyes)

UL_BUILD_INIT([taskset], [check])
UL_REQUIRES_BUILD([taskset], [schedutils])
UL_REQUIRES_SYSCALL_CHECK([taskset],
	[UL_CHECK_SYSCALL([sched_getaffinity])],
	[sched_getaffinity])
AM_CONDITIONAL(BUILD_TASKSET, test "x$build_taskset" = xyes)


AC_ARG_ENABLE([wall],
  AS_HELP_STRING([--disable-wall], [do not build wall]),
  [], enable_wall=yes
)
UL_BUILD_INIT([wall])
AM_CONDITIONAL(BUILD_WALL, test "x$build_wall" = xyes)


AC_ARG_ENABLE([write],
  AS_HELP_STRING([--enable-write], [build write]),
  [], enable_write=no
)
UL_BUILD_INIT([write])
AM_CONDITIONAL(BUILD_WRITE, test "x$build_write" = xyes)


AC_ARG_ENABLE([chkdupexe],
  AS_HELP_STRING([--enable-chkdupexe], [build deprecated chkdupexe]),
  [], enable_chkdupexe=no
)
UL_BUILD_INIT([chkdupexe])
AM_CONDITIONAL(BUILD_CHKDUPEXE, test "x$build_chkdupexe" = xyes)

if test "x$build_chkdupexe" = xyes; then
	AC_PATH_PROG(PERL, perl)
	AC_CONFIG_FILES([misc-utils/chkdupexe:misc-utils/chkdupexe.pl])
fi


AC_ARG_ENABLE([socket-activation],
  AS_HELP_STRING([--enable-socket-activation], [build uuidd with support for systemd socket activation]),
  [], enable_socket_activation=no
)

AM_CONDITIONAL(USE_SOCKET_ACTIVATION, test "x$enable_socket_activation" = xyes)

if test "x$enable_socket_activation" = xyes; then
  AC_DEFINE(USE_SOCKET_ACTIVATION, 1, [Should uuidd support socket activation?])
fi

AC_ARG_WITH([systemdsystemunitdir],
  AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [directory for systemd service files]),
  [], [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])

if test "x$with_systemdsystemunitdir" != "xno"; then
  AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
fi

AM_CONDITIONAL(HAVE_SYSTEMD, [test -n "$with_systemdsystemunitdir" -a "x$with_systemdsystemunitdir" != "xno" ])


AC_ARG_ENABLE([pg-bell],
  AS_HELP_STRING([--disable-pg-bell], [let pg not ring the bell on invalid keys]),
  [], enable_pg_bell=yes
)

if test "x$enable_pg_bell" = xyes; then
  AC_DEFINE(PG_BELL, 1, [Should pg ring the bell on invalid keys?])
fi


AC_ARG_ENABLE([require-password],
  AS_HELP_STRING([--disable-require-password], [do not require the user to enter the password in chfn and chsh]),
  [], enable_require_password=yes
)

if test "x$enable_require_password" = xyes; then
  AC_DEFINE(REQUIRE_PASSWORD, 1, [Should chfn and chsh require the user to enter the password?])
fi


AC_DEFUN([FS_PATHS_DEFAULT], [/sbin:/sbin/fs.d:/sbin/fs])
AC_ARG_ENABLE([fs-paths-default],
  AS_HELP_STRING([--enable-fs-paths-default=paths], [default search path for fs helpers @<:@FS_PATHS_DEFAULT@:>@]),
  [case "$enableval" in
     yes) fs_paths_defaults="FS_PATHS_DEFAULT" ;;
      no) fs_paths_defaults="" ;;
       *) fs_paths_defaults="$enableval" ;;
   esac],
  [fs_paths_defaults="FS_PATHS_DEFAULT"]
)
AC_ARG_ENABLE([fs-paths-extra],
  AS_HELP_STRING([--enable-fs-paths-extra=paths], [additional search paths for fs helpers]),
  [case "$enableval" in
     yes|no) fs_paths_extra="" ;;
	  *) fs_paths_extra="$enableval" ;;
    esac],
  [fs_paths_extra=""]
)
fs_paths="$fs_paths_defaults"
if test "x$fs_paths_extra" != "x"; then
  if test "x$fs_paths" != "x"; then
    fs_paths="${fs_paths}:"
  fi
  fs_paths="${fs_paths}${fs_paths_extra}"
fi
AC_DEFINE_UNQUOTED([FS_SEARCH_PATH], "$fs_paths", [search path for fs helpers])


AC_ARG_ENABLE([use-tty-group],
  AS_HELP_STRING([--disable-use-tty-group], [do not install wall and write setgid tty]),
  [], enable_use_tty_group=yes
)
AM_CONDITIONAL(USE_TTY_GROUP, test "x$enable_use_tty_group" = xyes)

if test "x$enable_use_tty_group" = xyes; then
  AC_DEFINE(USE_TTY_GROUP, 1, [Should wall and write be installed setgid tty?])
fi

AC_ARG_ENABLE([makeinstall-chown],
  AS_HELP_STRING([--disable-makeinstall-chown], [do not do chown-like operations during "make install"]),
  [], enable_makeinstall_chown=yes
)
AM_CONDITIONAL(MAKEINSTALL_DO_CHOWN, test "x$enable_makeinstall_chown" = xyes)

AC_ARG_ENABLE([makeinstall-setuid],
  AS_HELP_STRING([--disable-makeinstall-setuid], [do not do setuid chmod operations during "make install"]),
  [], enable_makeinstall_setuid=yes
)
AM_CONDITIONAL(MAKEINSTALL_DO_SETUID, test "x$enable_makeinstall_setuid" = xyes)


AC_ARG_VAR([SUID_CFLAGS],
	   [CFLAGS used for binaries which are usually with the suid bit])
AC_ARG_VAR([SUID_LDFLAGS],
	   [LDFLAGS used for binaries which are usually with the suid bit])

LIBS=""


AC_CONFIG_HEADERS(config.h)

#
# Don't use configure.ac to replace install paths! See Makefile PATHFILES for
# more details.
#
AC_CONFIG_FILES([
Makefile
libblkid/docs/Makefile
libblkid/docs/version.xml
libblkid/src/blkid.h
libmount/docs/Makefile
libmount/docs/version.xml
libmount/src/libmount.h
po/Makefile.in
])


AC_OUTPUT
