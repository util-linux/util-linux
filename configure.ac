AC_INIT(util-linux, 2.13-pre7, bunk@stusta.de)

AC_PREREQ(2.59)

AC_CONFIG_AUX_DIR(config)

AM_INIT_AUTOMAKE([check-news -Wall foreign 1.9])

AC_CONFIG_SRCDIR(mount/mount.c)

AC_PREFIX_DEFAULT(/)

AC_PROG_CC_STDC
AC_PROG_RANLIB

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

AC_CHECK_LIB(blkid, blkid_known_fstype)
AM_CONDITIONAL(HAVE_BLKID, test x$ac_cv_lib_blkid_blkid_known_fstype = xyes)


AM_GNU_GETTEXT([external])


AC_CHECK_HEADERS(ncurses.h)
AC_CHECK_HEADERS(ncurses/ncurses.h)


if test x$ac_cv_header_ncurses_h = xyes || test x$ac_cv_header_ncurses_ncurses_h = xyes; then
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
  if test x$ac_cv_header_slcurses_h = xyes || test x$ac_cv_header_slang_slcurses_h = xyes; then
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
  i?86-*)  intel=true ;;
  m68*)    m68k=true ;;
  sparc*)  sparc=true ;;
esac

AM_CONDITIONAL(INTEL, test x$intel = xtrue)
AM_CONDITIONAL(M68K,  test x$m68k  = xtrue)
AM_CONDITIONAL(SPARC, test x$sparc = xtrue)


AC_ARG_ENABLE([agetty],
  AC_HELP_STRING([--disable-agetty], [do not build agetty]),
  enable_agetty=$enableval, enable_agetty=yes
)
AM_CONDITIONAL(BUILD_AGETTY, test x$enable_agetty = xyes)


AC_ARG_ENABLE([cramfs],
  AC_HELP_STRING([--disable-cramfs], [do not build fsck.cramfs, mkfs.cramfs]),
  enable_cramfs=$enableval, enable_cramfs=check
)

if test x$enable_cramfs != xno; then
  AC_CHECK_LIB(z, crc32)
  build_cramfs=$ac_cv_lib_z_crc32
  if test x$enable_cramfs = xyes && test x$ac_cv_lib_z_crc32 = xno; then
    AC_MSG_ERROR([cramfs selected but libz not found])
  fi
fi

AM_CONDITIONAL(BUILD_CRAMFS, test x$build_cramfs = xyes)


AC_ARG_ENABLE([elvtune],
  AC_HELP_STRING([--enable-elvtune], [build elvtune (only works with 2.2 and 2.4 kernels)]),
  enable_elvtune=$enableval, enable_elvtune=no
)
AM_CONDITIONAL(BUILD_ELVTUNE, test x$enable_elvtune = xyes)


AC_ARG_ENABLE([init],
  AC_HELP_STRING([--enable-init], [build simpleinit, shutdown, initctl]),
  enable_init=$enableval, enable_init=no
)
AM_CONDITIONAL(BUILD_INIT, test x$enable_init = xyes)


AC_ARG_ENABLE([kill],
  AC_HELP_STRING([--enable-kill], [build kill]),
  enable_kill=$enableval, enable_kill=no
)
AM_CONDITIONAL(BUILD_KILL, test x$enable_kill = xyes)


AC_ARG_ENABLE([last],
  AC_HELP_STRING([--enable-last], [build last]),
  enable_last=$enableval, enable_last=no
)
AM_CONDITIONAL(BUILD_LAST, test x$enable_last = xyes)


AC_ARG_ENABLE([mesg],
  AC_HELP_STRING([--enable-mesg], [build mesg]),
  enable_mesg=$enableval, enable_mesg=no
)
AM_CONDITIONAL(BUILD_MESG, test x$enable_mesg = xyes)


AC_ARG_ENABLE([partx],
  AC_HELP_STRING([--enable-partx], [build addpart, delpart, partx]),
  enable_partx=$enableval, enable_partx=no
)
AM_CONDITIONAL(BUILD_PARTX, test x$enable_partx = xyes)


AC_ARG_ENABLE([raw],
  AC_HELP_STRING([--enable-raw], [build raw]),
  enable_raw=$enableval, enable_raw=no
)
AM_CONDITIONAL(BUILD_RAW, test x$enable_raw = xyes)


AC_ARG_ENABLE([rdev],
  AC_HELP_STRING([--enable-rdev], [build rdev on i386]),
  enable_rdev=$enableval, enable_rdev=no
)
AM_CONDITIONAL(BUILD_RDEV, test x$enable_rdev = xyes)


AC_ARG_ENABLE([rename],
  AC_HELP_STRING([--disable-rename], [do not build rename]),
  enable_agetty=$enableval, enable_rename=yes
)
AM_CONDITIONAL(BUILD_RENAME, test x$enable_rename = xyes)


AC_ARG_ENABLE([reset],
  AC_HELP_STRING([--enable-reset], [build reset]),
  enable_reset=$enableval, enable_reset=no
)
AM_CONDITIONAL(BUILD_RESET, test x$enable_reset = xyes)


AC_ARG_ENABLE([login-utils],
  AC_HELP_STRING([--enable-login-utils], [build chfn, chsh, login, newgrp, vipw]),
  enable_login_utils=$enableval, enable_login_utils=no
)
AM_CONDITIONAL(BUILD_LOGIN_UTILS, test x$enable_login_utils = xyes)

AC_ARG_WITH([pam],
  AC_HELP_STRING([--without-pam], [compile login-utils without PAM support]),
  with_pam=$withval, with_pam=not_checked
)

if test x$enable_login_utils = xyes && test x$with_pam != xno; then
  AC_CHECK_HEADERS(security/pam_misc.h)
  if test x$with_pam = xyes && test x$ac_cv_header_security_pam_misc_h = xno; then
    AC_MSG_ERROR([PAM selected but security/pam_misc.h not found])
  fi
fi
AM_CONDITIONAL(HAVE_PAM, test x$ac_cv_header_security_pam_misc_h = xyes)

AC_ARG_WITH([selinux],
  AC_HELP_STRING([--without-selinux], [compile login-utils without SELinux support]),
  with_selinux=$withval, with_selinux=not_checked
)

if test x$enable_login_utils = xyes && test x$with_selinux != xno; then
  AC_CHECK_LIB(selinux, getprevcon)
  if test x$with_selinux = xyes && test x$ac_cv_lib_selinux_getprevcon = xno; then
    AC_MSG_ERROR([SELinux selected but libselinux not found])
  fi
fi
AM_CONDITIONAL(HAVE_SELINUX, test x$ac_cv_lib_selinux_getprevcon = xyes)



AC_ARG_ENABLE([schedutils],
  AC_HELP_STRING([--disable-schedutils], [do not build chrt, ionice, teskset]),
  enable_schedutils=$enableval, enable_schedutils=yes
)
AM_CONDITIONAL(BUILD_SCHEDUTILS, test x$enable_schedutils = xyes)


AC_ARG_ENABLE([wall],
  AC_HELP_STRING([--disable-wall], [do not build wall]),
  enable_wall=$enableval, enable_wall=yes
)
AM_CONDITIONAL(BUILD_WALL, test x$enable_wall = xyes)


AC_ARG_ENABLE([write],
  AC_HELP_STRING([--enable-write], [build write]),
  enable_write=$enableval, enable_write=no
)
AM_CONDITIONAL(BUILD_WRITE, test x$enable_write = xyes)


AC_ARG_ENABLE([chsh-only-listed],
  AC_HELP_STRING([--disable-chsh-only-listed], [chsh: allow shells not in /etc/shells]),
  enable_login_chsh_only_listed=$enableval, enable_chsh_only_listed=yes
)

if test x$enable_chsh_only_listed = xyes; then
  AC_DEFINE(ONLY_LISTED_SHELLS, 1, [Should chsh allow only shells in /etc/shells?])
fi


AC_ARG_ENABLE([login-chown-vcs],
  AC_HELP_STRING([--enable-login-chown-vcs], [let login chown /dev/vcsN]),
  enable_login_chown_vcs=$enableval, enable_login_chown_vcs=no
)

if test x$enable_login_chown_vcs = xyes; then
  AC_DEFINE(LOGIN_CHOWN_VCS, 1, [Should login chown /dev/vcsN?])
fi


AC_ARG_ENABLE([login-stat-mail],
  AC_HELP_STRING([--enable-login-stat-mail], [let login stat() the mailbox]),
  enable_login_stat_mail=$enableval, enable_login_stat_mail=no
)

if test x$enable_login_stat_mail = xyes; then
  AC_DEFINE(LOGIN_STAT_MAIL, 1, [Should login stat() the mailbox?])
fi


AC_ARG_ENABLE([pg-bell],
  AC_HELP_STRING([--disable-pg-bell], [let pg not ring the bell on invalid keys]),
  enable_pg_bell=$enableval, enable_pg_bell=yes
)

if test x$enable_pg_bell = xyes; then
  AC_DEFINE(PG_BELL, 1, [Should pg ring the bell on invalid keys?])
fi


AC_ARG_ENABLE([require-password],
  AC_HELP_STRING([--disable-require-password], [do not require the user to enter the password in chfn and chsh]),
  enable_require_password=$enableval, enable_require_password=yes
)

if test x$enable_require_password = xyes; then
  AC_DEFINE(REQUIRE_PASSWORD, 1, [Should chfn and chsh require the user to enter the password?])
fi


AC_ARG_ENABLE([use-tty-group],
  AC_HELP_STRING([--disable-use-tty-group], [do not install wall and write setgid tty]),
  enable_use_tty_group=$enableval, enable_use_tty_group=yes
)
AM_CONDITIONAL(USE_TTY_GROUP, test x$enable_use_tty_group = xyes)

if test x$enable_use_tty_group = xyes; then
  AC_DEFINE(USE_TTY_GROUP, 1, [Should wall and write be installed setgid tty?])
fi

CPPFLAGS="-fsigned-char -fomit-frame-pointer $CPPFLAGS"

LIBS=""


AC_CONFIG_HEADERS(config.h)

AC_CONFIG_FILES([Makefile disk-utils/Makefile fdisk/Makefile getopt/Makefile hwclock/Makefile lib/Makefile login-utils/Makefile misc-utils/Makefile mount/Makefile partx/Makefile po/Makefile.in schedutils/Makefile sys-utils/Makefile text-utils/Makefile])

AC_OUTPUT
