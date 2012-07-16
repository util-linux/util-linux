dnl Copyright (C) 2008-2011 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl From Simon Josefsson
dnl -- derivated from coreutils m4/warnings.m4

# UL_AS_VAR_APPEND(VAR, VALUE)
# ----------------------------
# Provide the functionality of AS_VAR_APPEND if Autoconf does not have it.
m4_ifdef([AS_VAR_APPEND],
[m4_copy([AS_VAR_APPEND], [UL_AS_VAR_APPEND])],
[m4_define([UL_AS_VAR_APPEND],
[AS_VAR_SET([$1], [AS_VAR_GET([$1])$2])])])

# UL_WARN_ADD(PARAMETER, [VARIABLE = WARN_CFLAGS])
# ------------------------------------------------
# Adds parameter to WARN_CFLAGS if the compiler supports it.  For example,
# UL_WARN_ADD([-Wparentheses]).
AC_DEFUN([UL_WARN_ADD],
dnl FIXME: ul_Warn must be used unquoted until we can assume
dnl autoconf 2.64 or newer.
[AS_VAR_PUSHDEF([ul_Warn], [ul_cv_warn_$1])dnl
AC_CACHE_CHECK([whether compiler handles $1], m4_defn([ul_Warn]), [
  ul_save_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="${CPPFLAGS} $1"
  AC_PREPROC_IFELSE([AC_LANG_PROGRAM([])],
                    [AS_VAR_SET(ul_Warn, [yes])],
                    [AS_VAR_SET(ul_Warn, [no])])
  CPPFLAGS="$ul_save_CPPFLAGS"
])
AS_VAR_IF(ul_Warn, [yes],
  [UL_AS_VAR_APPEND(m4_if([$2], [], [[WARN_CFLAGS]], [[$2]]), [" $1"])])
AS_VAR_POPDEF([ul_Warn])dnl
m4_ifval([$2], [AS_LITERAL_IF([$2], [AC_SUBST([$2])], [])])dnl
])


# UL_PROG_CLANG
# -------------
# Checks if compiler is clang, defines compiler_clang=yes if yes
AC_DEFUN([UL_PROG_CLANG], [
  AC_REQUIRE([AC_PROG_CC])
  AC_CACHE_CHECK([whether clang is in use], [ul_cv_clang], [
    case ${CC} in #(
      *gcc*)   ul_cv_clang=no ;;
      *clang*) ul_cv_clang=yes ;;
      *) AC_COMPILE_IFELSE([AC_LANG_SOURCE([int main() {
           #ifdef __clang__
               return 0;
             #else
               #error The __clang__ was not defined
             #endif
           }])],
           [ul_cv_clang=yes],
           [ul_cv_clang=no]
         ) ;;
    esac
  ])
  compiler_clang=$ul_cv_clang
])
