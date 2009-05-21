# from http://autoconf-archive.cryp.to/ax_tls.html
#
# This was licensed under the GPL with the following exception:
#
# As a special exception, the respective Autoconf Macro's copyright
# owner gives unlimited permission to copy, distribute and modify the
# configure scripts that are the output of Autoconf when processing
# the Macro. You need not follow the terms of the GNU General Public
# License when using or distributing such scripts, even though
# portions of the text of the Macro appear in them. The GNU General
# Public License (GPL) does govern all other use of the material that
# constitutes the Autoconf Macro.
#
# This special exception to the GPL applies to versions of the
# Autoconf Macro released by the Autoconf Macro Archive. When you make
# and distribute a modified version of the Autoconf Macro, you may
# extend this special exception to the GPL to apply to your modified
# version as well.
#
AC_DEFUN([AX_TLS], [
  AC_MSG_CHECKING(for thread local storage (TLS) class)
  AC_CACHE_VAL(ac_cv_tls, [
    ax_tls_keywords="__thread __declspec(thread) none"
    for ax_tls_keyword in $ax_tls_keywords; do
       case $ax_tls_keyword in
          none) ac_cv_tls=none ; break ;;
          *)
             AC_TRY_COMPILE(
                [#include <stdlib.h>
                 static void
                 foo(void) {
                 static ] $ax_tls_keyword [ int bar;
                 exit(1);
                 }],
                 [],
                 [ac_cv_tls=$ax_tls_keyword ; break],
                 ac_cv_tls=none
             )
          esac
    done
])

  if test "$ac_cv_tls" != "none"; then
    dnl AC_DEFINE([TLS], [], [If the compiler supports a TLS storage class define it to that here])
    AC_DEFINE_UNQUOTED([TLS], $ac_cv_tls, [If the compiler supports a TLS storage class define it to that here])
  fi
  AC_MSG_RESULT($ac_cv_tls)
])
