include $(top_srcdir)/config/include-Makefile.am

bin_PROGRAMS =
usrbin_PROGRAMS =
sbin_PROGRAMS =
usrsbin_PROGRAMS =
man_MANS =

if BUILD_AGETTY
sbin_PROGRAMS += agetty
man_MANS += agetty
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
usrbin_PROGRAMS += last
man_MANS += last.1
endif

if BUILD_LOGIN_UTILS

bin_PROGRAMS += login
usrbin_PROGRAMS += chfn chsh
usrsbin_PROGRAMS += vipw
man_MANS += chfn.1 chsh.1 login.1 vipw.8 vigr.8

chfn_SOURCES = chfn.c islocal.c setpwnam.c
chsh_SOURCES = chsh.c islocal.c setpwnam.c

chfn_LDADD = ../lib/libenv.la
chsh_LDADD = ../lib/libenv.la
login_LDADD = ../lib/libsetproctitle.la
newgrp_LDADD =
vipw_LDADD =

if NEED_LIBCRYPT
usrbin_PROGRAMS += newgrp
man_MANS += newgrp.1
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


install-data-hook::
	cd $(DESTDIR)$(usrsbindir) && ln -sf vipw vigr

endif

if BUILD_MESG
usrbin_PROGRAMS += mesg
man_MANS += mesg.1
endif

if BUILD_WALL

usrbin_PROGRAMS += wall
wall_SOURCES = wall.c ttymsg.c
man_MANS += wall.1

if USE_TTY_GROUP
install-data-hook::
	chgrp tty $(DESTDIR)$(usrbindir)/wall
	chmod g+s $(DESTDIR)$(usrbindir)/wall
endif

endif


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

