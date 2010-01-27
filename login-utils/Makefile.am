include $(top_srcdir)/config/include-Makefile.am

bin_PROGRAMS =
usrbin_exec_PROGRAMS =
sbin_PROGRAMS =
usrsbin_exec_PROGRAMS =
dist_man_MANS =

EXTRA_DIST = README.getty  README.modems-with-agetty  README.poeigl

if BUILD_AGETTY
sbin_PROGRAMS += agetty
dist_man_MANS += agetty.8
endif

if BUILD_INIT

sbin_PROGRAMS += simpleinit shutdown initctl
dist_man_MANS += fastboot.8 fasthalt.8 halt.8 reboot.8 simpleinit.8 shutdown.8 \
	initctl.8

simpleinit_SOURCES = simpleinit.c my_crypt.h simpleinit.h
initctl_SOURCES = initctl.c simpleinit.h

if NEED_LIBCRYPT
simpleinit_LDADD = -lcrypt
endif

endif # BUILD_INIT

if BUILD_LAST
usrbin_exec_PROGRAMS += last
dist_man_MANS += last.1
endif

if BUILD_LOGIN_UTILS

bin_PROGRAMS += login
usrbin_exec_PROGRAMS += chfn chsh newgrp
usrsbin_exec_PROGRAMS += vipw
dist_man_MANS += chfn.1 chsh.1 login.1 newgrp.1 vipw.8 vigr.8

# login, chfn and chsh libs
login_ldadd_common =

chfn_SOURCES = chfn.c $(chfn_chsh_common)
chsh_SOURCES = chsh.c $(chfn_chsh_common)
chfn_chsh_common = islocal.c setpwnam.c islocal.h my_crypt.h setpwnam.h \
	../lib/env.c
login_SOURCES = login.c login.h my_crypt.h ../lib/setproctitle.c
newgrp_SOURCES = newgrp.c my_crypt.h
vipw_SOURCES = vipw.c setpwnam.h

chfn_LDADD = $(login_ldadd_common)
chsh_LDADD = $(login_ldadd_common)
login_LDADD = $(login_ldadd_common)
newgrp_LDADD =
vipw_LDADD =

chfn_CFLAGS = $(SUID_CFLAGS) $(AM_CFLAGS)
chsh_CFLAGS = $(SUID_CFLAGS) $(AM_CFLAGS)
newgrp_CFLAGS = $(SUID_CFLAGS) $(AM_CFLAGS)

chfn_LDFLAGS = $(SUID_LDFLAGS) $(AM_LDFLAGS)
chsh_LDFLAGS = $(SUID_LDFLAGS) $(AM_LDFLAGS)
newgrp_LDFLAGS = $(SUID_LDFLAGS) $(AM_LDFLAGS)

if HAVE_PAM
login_ldadd_common += -lpam -lpam_misc
else
if NEED_LIBCRYPT
login_ldadd_common += -lcrypt
endif
login_SOURCES += checktty.c
endif #! HAVE_PAM

if NEED_LIBCRYPT
newgrp_LDADD += -lcrypt
endif

if HAVE_AUDIT
login_LDADD += -laudit
endif

if HAVE_SELINUX
chfn_SOURCES += selinux_utils.c selinux_utils.h
chsh_SOURCES += selinux_utils.c selinux_utils.h
vipw_LDADD += -lselinux
login_ldadd_common += -lselinux
endif


install-exec-hook::
	cd $(DESTDIR)$(usrsbin_execdir) && ln -sf vipw vigr

endif

if BUILD_MESG
usrbin_exec_PROGRAMS += mesg
dist_man_MANS += mesg.1
endif

if BUILD_WALL
usrbin_exec_PROGRAMS += wall
wall_SOURCES = wall.c ttymsg.c ttymsg.h
dist_man_MANS += wall.1

if USE_TTY_GROUP
if MAKEINSTALL_DO_CHOWN
install-exec-hook::
	chgrp tty $(DESTDIR)$(usrbin_execdir)/wall
	chmod g+s $(DESTDIR)$(usrbin_execdir)/wall
endif
endif
endif

if BUILD_INIT

install-exec-hook::
	cd $(DESTDIR)$(sbindir) && ln -sf shutdown reboot
	cd $(DESTDIR)$(sbindir) && ln -sf shutdown fastboot
	cd $(DESTDIR)$(sbindir) && ln -sf shutdown halt
	cd $(DESTDIR)$(sbindir) && ln -sf shutdown fasthalt
	cd $(DESTDIR)$(sbindir) && ln -sf initctl need
	cd $(DESTDIR)$(sbindir) && ln -sf initctl display-services
	cd $(DESTDIR)$(sbindir) && ln -sf initctl provide

install-data-hook:
	cd $(DESTDIR)$(mandir)/man8 && ln -sf initctl.8 need.8
	cd $(DESTDIR)$(mandir)/man8 && ln -sf initctl.8 display-services.8
	cd $(DESTDIR)$(mandir)/man8 && ln -sf initctl.8 provide.8

endif

noinst_PROGRAMS = checktty_test islocal_test
checktty_test_SOURCES = checktty.c login.h
checktty_test_CPPFLAGS = -DMAIN_TEST_CHECKTTY $(AM_CPPFLAGS)
islocal_test_SOURCES = islocal.c
islocal_test_CPPFLAGS = -DMAIN_TEST_ISLOCAL $(AM_CPPFLAGS)

