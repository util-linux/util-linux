
dnl UL_PKG_STATIC(VARIABLE, MODULES)
dnl
dnl Calls pkg-config --static
dnl
AC_DEFUN([UL_PKG_STATIC], [
  if AC_RUN_LOG([pkg-config --exists --print-errors "$2"]); then
    $1=`pkg-config --libs --static "$2"`
  else
    AC_MSG_ERROR([pkg-config description of $2, needed for static build, is not available])
  fi
])

dnl UL_CHECK_LIB(LIBRARY, FUNCTION, [VARSUFFIX = $1]))
dnl
dnl The VARSUFFIX is optional and overrides the default behaviour. For example:
dnl     UL_CHECK_LIB(yyy, func, xxx) generates have_xxx and HAVE_LIBXXX
dnl     UL_CHECK_LIB(yyy, func)      generates have_yyy and HAVE_LIBYYY
dnl
AC_DEFUN([UL_CHECK_LIB], [
  m4_define([suffix], m4_default([$3],$1))
  [have_]suffix=yes
  m4_ifdef([$3],
    [AC_CHECK_LIB([$1], [$2], [AC_DEFINE(AS_TR_CPP([HAVE_LIB]suffix), 1)], [[have_]suffix=no])],
    [AC_CHECK_LIB([$1], [$2], [], [[have_]suffix=no])])
  AM_CONDITIONAL(AS_TR_CPP([HAVE_]suffix), [test [$have_]suffix = yes])
])


dnl UL_SET_ARCH(ARCHNAME, PATTERN)
dnl
dnl Define ARCH_<archname> condition if the pattern match with the current
dnl architecture
dnl
AC_DEFUN([UL_SET_ARCH], [
  cpu_$1=false
  case "$host" in
   $2) cpu_$1=true ;;
  esac
  AM_CONDITIONAL(AS_TR_CPP(ARCH_$1), [test "x$cpu_$1" = xtrue])
])


dnl UL_SET_FLAGS(CFLAGS, CPPFLAGS, LDFLAGS)
dnl
dnl Sets new global CFLAGS, CPPFLAGS and LDFLAG, the original
dnl setting could be restored by UL_RESTORE_FLAGS()
dnl
AC_DEFUN([UL_SET_FLAGS], [
  old_CFLAGS="$CFLAGS"
  old_CPPFLAGS="$CPPFLAGS"
  old_LDFLAGS="$LDFLAGS"
  CFLAGS="$CFLAGS $1"
  CPPFLAGS="$CPPFLAGS $2"
  LDFLAGS="$LDFLAGS $3"
])

dnl UL_RESTORE_FLAGS()
dnl
dnl Restores CFLAGS, CPPFLAGS and LDFLAG previously saved by UL_SET_FLAGS()
dnl
AC_DEFUN([UL_RESTORE_FLAGS], [
  CFLAGS="$old_CFLAGS"
  CPPFLAGS="$old_CPPFLAGS"
  LDFLAGS="$old_LDFLAGS"
])


dnl UL_CHECK_SYSCALL(SYSCALL, FALLBACK, ...)
dnl
dnl Only specify FALLBACK if the SYSCALL you're checking for is a "newish" one
dnl
AC_DEFUN([UL_CHECK_SYSCALL], [
  dnl This macro uses host_cpu.
  AC_REQUIRE([AC_CANONICAL_HOST])
  AC_CACHE_CHECK([for syscall $1],
    [ul_cv_syscall_$1],
    [_UL_SYSCALL_CHECK_DECL([SYS_$1],
      [syscall=SYS_$1],
      [dnl Our libc failed use, so see if we can get the kernel
      dnl headers to play ball ...
      _UL_SYSCALL_CHECK_DECL([_NR_$1],
	[syscall=_NR_$1],
	[
	  syscall=no
	  if test "x$linux_os" = xyes; then
	    case $host_cpu in
	      _UL_CHECK_SYSCALL_FALLBACK(m4_shift($@))
	    esac
	  fi
        ])
      ])
    ul_cv_syscall_$1=$syscall
    ])
  AM_CONDITIONAL([HAVE_]m4_toupper($1), [test "x$ul_cv_syscall_$1" != xno])
  case $ul_cv_syscall_$1 in #(
  no) AC_MSG_WARN([Unable to detect syscall $1.]) ;;
  SYS_*) ;;
  *) AC_DEFINE_UNQUOTED([SYS_$1], [$ul_cv_syscall_$1],
	[Fallback syscall number for $1]) ;;
  esac
])


dnl _UL_SYSCALL_CHECK_DECL(SYMBOL, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)
dnl
dnl Check if SYMBOL is declared, using the headers needed for syscall checks.
dnl
m4_define([_UL_SYSCALL_CHECK_DECL],
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/syscall.h>
#include <unistd.h>
]], [[int test = $1;]])],
[$2], [$3])
])

dnl _UL_CHECK_SYSCALL_FALLBACK(PATTERN, VALUE, ...)
dnl
dnl Helper macro to create the body for the above `case'.
dnl
m4_define([_UL_CHECK_SYSCALL_FALLBACK],
[m4_ifval([$1],
  [#(
  $1) syscall="$2" ;;dnl
  _UL_CHECK_SYSCALL_FALLBACK(m4_shiftn(2, $@))])dnl
])
