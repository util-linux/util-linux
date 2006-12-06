AC_INIT(util-linux, 2.13-pre1, bunk@stusta.de)

AC_CONFIG_AUX_DIR(config)

AM_INIT_AUTOMAKE([check-news std-options -Wall foreign])

AC_CONFIG_SRCDIR(mount/mount.c)

AC_PREFIX_DEFAULT(/)

AC_PROG_CC_STDC

AC_DISABLE_SHARED
AC_PROG_LIBTOOL

AC_PATH_PROG(PERL, perl)

AC_SYS_LARGEFILE

AC_CHECK_HEADERS(scsi/scsi.h)
AC_CHECK_HEADERS(linux/compiler.h)
AC_CHECK_HEADERS(linux/blkpg.h,,,[
#ifdef HAVE_LINUX_COMPILER_H
#include <linux/compiler.h>
#endif
])
AC_CHECK_HEADERS(langinfo.h)
AC_CHECK_HEADERS(sys/user.h)
AC_CHECK_HEADERS(asm/page.h)
AC_CHECK_HEADERS(rpcsvc/nfs_prot.h)
AC_CHECK_HEADERS(sys/io.h)
AC_CHECK_HEADERS(pty.h)

AC_CHECK_HEADERS(linux/raw.h)
AM_CONDITIONAL(HAVE_RAW, test x$ac_cv_header_linux_raw_h = xyes)

AC_CHECK_HEADERS(security/pam_misc.h)
AM_CONDITIONAL(HAVE_PAM, test x$ac_cv_header_security_pam_misc_h = xyes)



AC_CHECK_FUNCS(inet_aton)
AC_CHECK_FUNCS(fsync)
AC_CHECK_FUNCS(getdomainname)
AC_CHECK_FUNCS(nanosleep)
AC_CHECK_FUNCS(personality)
AC_CHECK_FUNCS(updwtmp)
AC_FUNC_FSEEKO
AC_CHECK_FUNCS(lchown)
AC_CHECK_FUNCS(rpmatch)


AC_CHECK_LIB(uuid, uuid_is_null)
AM_CONDITIONAL(HAVE_UUID, test x$ac_cv_lib_uuid_uuid_is_null = xyes)

AC_CHECK_LIB(util, openpty)
AM_CONDITIONAL(HAVE_LIBUTIL, test x$ac_cv_lib_util_openpty = xyes)

AC_CHECK_LIB(termcap, tgetnum)
AM_CONDITIONAL(HAVE_TERMCAP, test x$ac_cv_lib_termcap_tgetnum = xyes)

AC_CHECK_LIB(z, crc32)
AM_CONDITIONAL(HAVE_ZLIB, test x$ac_cv_lib_z_crc32 = xyes)

AC_CHECK_LIB(blkid, blkid_known_fstype)
AM_CONDITIONAL(HAVE_BLKID, test x$ac_cv_lib_blkid_blkid_known_fstype = xyes)

AC_CHECK_LIB(selinux, getprevcon)
AM_CONDITIONAL(HAVE_SELINUX, test x$ac_cv_lib_selinux_getprevcon = xyes)

LIBS=""


AM_GNU_GETTEXT([external])


AC_CHECK_HEADERS(ncurses.h)
AC_CHECK_HEADERS(ncurses/ncurses.h)


if test x$ac_cv_header_ncurses_h = xyes -o x$ac_cv_header_ncurses_ncurses_h = xyes; then
  have_ncurses=yes
  AC_MSG_NOTICE([you have ncurses])
else
  AC_MSG_NOTICE([you do not have ncurses])
fi

AM_CONDITIONAL(HAVE_NCURSES, test x$have_ncurses = xyes)


AC_ARG_WITH([slang],
  AC_HELP_STRING([--with-slang], [compile cfdisk with slang]),
  with_slang=$withval, with_slang=no
)

if test x$with_slang = xyes; then
  AC_CHECK_HEADERS(slcurses.h)
  AC_CHECK_HEADERS(slang/slcurses.h)
  if test x$ac_cv_header_slcurses_h = xyes -o x$ac_cv_header_slang_slcurses_h = xyes; then
    use_slang=yes
  else
    AC_MSG_ERROR([slang selected but slcurses.h not found])
  fi
fi

AM_CONDITIONAL(USE_SLANG, test x$use_slang = xyes)


AC_TRY_LINK([
#define _XOPEN_SOURCE
#include <unistd.h>
],[
char *c = crypt("abc","pw");
],,[
  my_LIBS="$LIBS"
  LIBS="$LIBS -lcrypt"
  AC_TRY_COMPILE([
  #define _XOPEN_SOURCE
  #include <unistd.h>
  ],[
  char *c = crypt("abc","pw");
  ],[
  AC_DEFINE(NEED_LIBCRYPT, 1, [Do we need -lcrypt?])
  need_libcrypt=yes
  ],[
  AC_MSG_ERROR([crypt() is not available])
  ])
  LIBS="$my_LIBS"
])

AM_CONDITIONAL(NEED_LIBCRYPT, test x$need_libcrypt = xyes)

AC_TRY_COMPILE([
#include <stdio.h>
],[
printf(__progname);
],AC_DEFINE(HAVE___PROGNAME, 1, [Do we have __progname?])
)


AC_TRY_COMPILE([
#include <wchar.h>
#include <wctype.h>
#include <stdio.h>
],[
  wchar_t wc;
  wint_t w;
  w = fgetwc(stdin);
  if (w == WEOF) exit(1);
  wc = w;
  fputwc(wc,stdout);
],AC_DEFINE(HAVE_WIDECHAR,1,[Do we have wide character support?]))


AC_TRY_COMPILE([
#include <sys/syscall.h>
#include <unistd.h>
],[
int test = SYS_pivot_root;
],have_pivot_root=true)

AM_CONDITIONAL(HAVE_PIVOT_ROOT, test x$have_pivot_root = xtrue)


AC_TRY_COMPILE([
#include <time.h>
#include <unistd.h>
],[
    int a = 0;
    struct tm *tm = localtime(0);
    if (a == -1)  /* false */
        sleep(tm->tm_gmtoff);
],[AC_DEFINE(HAVE_TM_GMTOFF,1,[Does struct tm have a field tm_gmtoff?])])


case "$host" in
  i?86-*) intel=true ;;
  sparc*)  sparc=true ;;
esac

AM_CONDITIONAL(INTEL, test x$intel = xtrue)
AM_CONDITIONAL(SPARC, test x$sparc = xtrue)


AC_ARG_ENABLE([agetty],
  AC_HELP_STRING([--disable-agetty], [do not build agetty]),
  with_agetty=$withval, with_agetty=yes
)
AM_CONDITIONAL(BUILD_AGETTY, test x$with_agetty = xyes)


AC_ARG_ENABLE([elvtune],
  AC_HELP_STRING([--enable-elvtune], [build elvtune (only works with 2.2 and 2.4 kernels)]),
  with_elvtune=$withval, with_elvtune=no
)
AM_CONDITIONAL(BUILD_ELVTUNE, test x$with_elvtune = xyes)


AC_ARG_ENABLE([init],
  AC_HELP_STRING([--enable-init], [build simpleinit, shutdown, initctl]),
  with_init=$withval, with_init=no
)
AM_CONDITIONAL(BUILD_INIT, test x$with_init = xyes)


AC_ARG_ENABLE([kill],
  AC_HELP_STRING([--enable-kill], [build kill]),
  with_kill=$withval, with_kill=no
)
AM_CONDITIONAL(BUILD_KILL, test x$with_kill = xyes)


AC_ARG_ENABLE([last],
  AC_HELP_STRING([--enable-last], [build last]),
  with_last=$withval, with_last=no
)
AM_CONDITIONAL(BUILD_LAST, test x$with_last = xyes)


AC_ARG_ENABLE([mesg],
  AC_HELP_STRING([--enable-mesg], [build mesg]),
  with_mesg=$withval, with_mesg=no
)
AM_CONDITIONAL(BUILD_MESG, test x$with_mesg = xyes)


AC_ARG_ENABLE([partx],
  AC_HELP_STRING([--enable-partx], [build addpart, delpart, partx]),
  with_partx=$withval, with_partx=no
)
AM_CONDITIONAL(BUILD_PARTX, test x$with_partx = xyes)


AC_ARG_ENABLE([raw],
  AC_HELP_STRING([--enable-raw], [build raw]),
  with_raw=$withval, with_raw=no
)
AM_CONDITIONAL(BUILD_RAW, test x$with_raw = xyes)


AC_ARG_ENABLE([rdev],
  AC_HELP_STRING([--enable-rdev], [build rdev on i386]),
  with_rdev=$withval, with_rdev=no
)
AM_CONDITIONAL(BUILD_RDEV, test x$with_rdev = xyes)


AC_ARG_ENABLE([rename],
  AC_HELP_STRING([--disable-rename], [do not build rename]),
  with_agetty=$withval, with_rename=yes
)
AM_CONDITIONAL(BUILD_RENAME, test x$with_rename = xyes)


AC_ARG_ENABLE([reset],
  AC_HELP_STRING([--enable-reset], [build reset]),
  with_reset=$withval, with_reset=no
)
AM_CONDITIONAL(BUILD_RESET, test x$with_reset = xyes)


AC_ARG_ENABLE([login-utils],
  AC_HELP_STRING([--enable-login-utils], [build chfn, chsh, login, newgrp, vipw]),
  with_login_utils=$withval, with_login_utils=no
)
AM_CONDITIONAL(BUILD_LOGIN_UTILS, test x$with_login_utils = xyes)


AC_ARG_ENABLE([schedutils],
  AC_HELP_STRING([--disable-schedutils], [do not build chrt, ionice, teskset]),
  with_schedutils=$withval, with_schedutils=yes
)
AM_CONDITIONAL(BUILD_SCHEDUTILS, test x$with_schedutils = xyes)


AC_ARG_ENABLE([wall],
  AC_HELP_STRING([--disable-wall], [do not build wall]),
  with_wall=$withval, with_wall=yes
)
AM_CONDITIONAL(BUILD_WALL, test x$with_wall = xyes)


AC_ARG_ENABLE([write],
  AC_HELP_STRING([--enable-write], [build write]),
  with_write=$withval, with_write=no
)
AM_CONDITIONAL(BUILD_WRITE, test x$with_write = xyes)


AC_ARG_ENABLE([chsh-only-listed],
  AC_HELP_STRING([--disable-chsh-only-listed], [chsh: allow shells not in /etc/shells]),
  with_login_chsh_only_listed=$withval, with_chsh_only_listed=yes
)

if test x$with_chsh_only_listed = xyes; then
  AC_DEFINE(ONLY_LISTED_SHELLS, 1, [Should chsh allow only shells in /etc/shells?])
fi


AC_ARG_ENABLE([login-chown-vcs],
  AC_HELP_STRING([--enable-login-chown-vcs], [let login chown /dev/vcsN]),
  with_login_chown_vcs=$withval, with_login_chown_vcs=no
)

if test x$with_login_chown_vcs = xyes; then
  AC_DEFINE(LOGIN_CHOWN_VCS, 1, [Should login chown /dev/vcsN?])
fi


AC_ARG_ENABLE([login-stat-mail],
  AC_HELP_STRING([--enable-login-stat-mail], [let login stat() the mailbox]),
  with_login_stat_mail=$withval, with_login_stat_mail=no
)

if test x$with_login_stat_mail = xyes; then
  AC_DEFINE(LOGIN_STAT_MAIL, 1, [Should login stat() the mailbox?])
fi


AC_ARG_ENABLE([pg-bell],
  AC_HELP_STRING([--disable-pg-bell], [let pg not ring the bell on invalid keys]),
  with_pg_bell=$withval, with_pg_bell=yes
)

if test x$with_pg_bell = xyes; then
  AC_DEFINE(PG_BELL, 1, [Should pg ring the bell on invalid keys?])
fi


AC_ARG_ENABLE([require-password],
  AC_HELP_STRING([--disable-require-password], [do not require the user to enter the password in chfn and chsh]),
  with_require_password=$withval, with_require_password=yes
)

if test x$with_require_password = xyes; then
  AC_DEFINE(REQUIRE_PASSWORD, 1, [Should chfn and chsh require the user to enter the password?])
fi


AC_ARG_ENABLE([use-tty-group],
  AC_HELP_STRING([--disable-use-tty-group], [do not install wall and write setgid tty]),
  with_use_tty_group=$withval, with_use_tty_group=yes
)
AM_CONDITIONAL(USE_TTY_GROUP, test x$with_use_tty_group = xyes)

if test x$with_use_tty_group = xyes; then
  AC_DEFINE(USE_TTY_GROUP, 1, [Should wall and write be installed setgid tty?])
fi



CPPFLAGS="-fsigned-char -Wall -Wmissing-prototypes -Wstrict-prototypes -Wundef -Werror-implicit-function-declaration $CPPFLAGS"


AC_CONFIG_HEADERS(config.h)

AC_CONFIG_FILES([Makefile disk-utils/Makefile fdisk/Makefile getopt/Makefile hwclock/Makefile lib/Makefile login-utils/Makefile misc-utils/Makefile mount/Makefile partx/Makefile po/Makefile.in schedutils/Makefile sys-utils/Makefile text-utils/Makefile])

AC_OUTPUT
