include $(top_srcdir)/config/include-Makefile.am

bin_PROGRAMS =
usrbinexec_PROGRAMS =
sbin_PROGRAMS =
usrsbinexec_PROGRAMS =
man_MANS =

if BUILD_AGETTY
sbin_PROGRAMS += agetty
man_MANS += agetty.8
endif

if BUILD_INIT

sbin_PROGRAMS += simpleinit shutdown initctl
man_MANS += fastboot.8 fasthalt.8 halt.8 reboot.8 simpleinit.8 shutdown.8 \
	initctl.8


if NEED_LIBCRYPT
simpleinit_LDADD = -lcrypt
endif

endif

if BUILD_LAST
usrbinexec_PROGRAMS += last
man_MANS += last.1
endif

if BUILD_LOGIN_UTILS

bin_PROGRAMS += login
usrbinexec_PROGRAMS += chfn chsh newgrp
usrsbinexec_PROGRAMS += vipw
man_MANS += chfn.1 chsh.1 login.1 newgrp.1 vipw.8 vigr.8

chfn_SOURCES = chfn.c islocal.c setpwnam.c
chsh_SOURCES = chsh.c islocal.c setpwnam.c

chfn_LDADD = ../lib/libenv.a
chsh_LDADD = ../lib/libenv.a
login_LDADD = ../lib/libsetproctitle.a
newgrp_LDADD =
vipw_LDADD =

if NEED_LIBCRYPT
chfn_LDADD += -lcrypt
chsh_LDADD += -lcrypt
login_LDADD += -lcrypt
newgrp_LDADD += -lcrypt
endif

if HAVE_PAM
chfn_LDADD += -lpam -lpam_misc
chsh_LDADD += -lpam -lpam_misc
login_LDADD += -lpam -lpam_misc
login_SOURCES = login.c
else
login_SOURCES = login.c checktty.c
endif


if HAVE_SELINUX
chfn_SOURCES += selinux_utils.c
chfn_LDADD += -lselinux
chsh_SOURCES += selinux_utils.c
chsh_LDADD += -lselinux
vipw_LDADD += -lselinux
endif


install-exec-hook::
	cd $(DESTDIR)$(usrsbinexecdir) && ln -sf vipw vigr

endif

if BUILD_MESG
usrbinexec_PROGRAMS += mesg
man_MANS += mesg.1
endif

if BUILD_WALL

usrbinexec_PROGRAMS += wall
wall_SOURCES = wall.c ttymsg.c
man_MANS += wall.1

if USE_TTY_GROUP
install-exec-hook::
	chgrp tty $(DESTDIR)$(usrbinexecdir)/wall
	chmod g+s $(DESTDIR)$(usrbinexecdir)/wall
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
	cd $(DESTDIR)$(mandir)/man8 && ln -sf initctl.8 need.8
	cd $(DESTDIR)$(mandir)/man8 && ln -sf initctl.8 display-services.8
	cd $(DESTDIR)$(mandir)/man8 && ln -sf initctl.8 provide.8

endif
