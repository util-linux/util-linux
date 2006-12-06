# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Sun Nov 10 20:28:43 1996 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#
# Suggested changes from Bauke Jan Douma <bjdouma@xs4all.nl> have been
# implemented to handle shadow and sysvinit systems 

include ../make_include
include ../MCONFIG

# Where to put man pages?

MAN1.MISC=	last.1 mesg.1 wall.1

MAN1.PUTILS= 	chfn.1 chsh.1 login.1 newgrp.1
MAN1.PASSWD=    passwd.1

MAN8.GETTY=	agetty.8

MAN8.INIT=	fastboot.8 fasthalt.8 halt.8 reboot.8 simpleinit.8 shutdown.8 \
		initctl.8

MAN8.PUTILS=	vipw.8 vigr.8

# Where to put binaries?
# See the "install" rule for the links. . .

SBIN.GETTY=	agetty

SBIN.INIT=	simpleinit shutdown initctl

BIN.PUTILS=	login

USRBIN.MISC=	last mesg wall

USRBIN.PUTILS=	chfn chsh newgrp
USRBIN.PASSWD=	passwd

USRSBIN.PUTILS=	vipw

ifeq "$(NEED_LIBCRYPT)" "yes"
CRYPT=-lcrypt
endif

ifeq "$(HAVE_PAM)" "yes"
PAM=-lpam -ldl -lpam_misc
PAMFL=-DUSE_PAM=1
endif

ifeq "$(HAVE_SELINUX)" "yes"
CFLAGS += -DWITH_SELINUX=1 -g
SELINUXLLIB=-lselinux 
SELINUXOBJS=selinux_utils.o
endif

ifeq "$(HAVE_SHADOW)" "no"
WHAT_TO_BUILD:=$(WHAT_TO_BUILD) all-putils
WHAT_TO_INSTALL:=$(WHAT_TO_INSTALL) install-putils
ifeq "$(HAVE_PAM)" "no"
ifeq "$(HAVE_PASSWD)" "no"
WHAT_TO_BUILD:=$(WHAT_TO_BUILD) all-passwd
WHAT_TO_INSTALL:=$(WHAT_TO_INSTALL) install-passwd
endif
endif
endif

ifeq "$(HAVE_SYSVINIT)" "no"
WHAT_TO_BUILD:=$(WHAT_TO_BUILD) all-init
WHAT_TO_INSTALL:=$(WHAT_TO_INSTALL) install-init
endif

ifeq "$(HAVE_SYSVINIT_UTILS)" "no"
WHAT_TO_BUILD:=$(WHAT_TO_BUILD) all-misc
WHAT_TO_INSTALL:=$(WHAT_TO_INSTALL) install-misc
endif

ifeq "$(HAVE_GETTY)" "no"
WHAT_TO_BUILD:=$(WHAT_TO_BUILD) all-getty
WHAT_TO_INSTALL:=$(WHAT_TO_INSTALL) install-getty
endif

all: $(WHAT_TO_BUILD)
all-passwd: $(USRBIN.PASSWD)
all-putils: $(BIN.PUTILS) $(USRBIN.PUTILS) $(USRSBIN.PUTILS)
all-init: $(SBIN.INIT)
all-getty: $(SBIN.GETTY)
all-misc: $(USRBIN.MISC)

# Rules for everything else
login.o: ../MCONFIG
checktty.o login.o: login.h
cryptocard.o login.o: cryptocard.h
chfn.o chsh.o islocal.o passwd.o: islocal.h
chfn.o chsh.o passwd.o setpwnam.o vipw.o: setpwnam.h
chfn.o chsh.o login.o newgrp.o passwd.o simpleinit.o: my_crypt.h
initctl.o simpleinit.o: simpleinit.h
agetty.o islocal.o last.o setpwnam.o shutdown.o simpleinit.o \
	vipw.o: $(LIB)/pathnames.h
shutdown.o simpleinit.o: $(LIB)/linux_reboot.h
wall.o: ttymsg.h $(LIB)/carefulputc.h

agetty: agetty.o $(LIB)/xstrncpy.o
chfn: chfn.o islocal.o setpwnam.o $(SELINUXOBJS) $(LIB)/env.o $(LIB)/xstrncpy.o
	$(CC) $(LDFLAGS) -o $@ $^ $(CRYPT) $(PAM) $(SELINUXLLIB)
chsh: chsh.o islocal.o setpwnam.o $(SELINUXOBJS) $(LIB)/env.o
	$(CC) $(LDFLAGS) -o $@ $^ $(CRYPT) $(PAM) $(SELINUXLLIB)
last: last.o

ifeq "$(HAVE_PAM)" "yes"
login: login.o $(LIB)/setproctitle.o $(LIB)/xstrncpy.o
	$(CC) $(LDFLAGS) -o $@ $^ $(CRYPT) $(PAM) $(SELINUXLLIB)
else
login: login.o $(LIB)/xstrncpy.o $(LIB)/setproctitle.o checktty.o 
	$(CC) $(LDFLAGS) -o $@ $^ $(CRYPT) $(SELINUXLLIB)
endif

mesg: mesg.o $(ERR_O)

newgrp: newgrp.o 
	$(CC) $(LDFLAGS) -o $@ $^ $(CRYPT) $(PAM)

shutdown: shutdown.o $(LIB)/my_reboot.o $(LIB)/xstrncpy.o

simpleinit: simpleinit.o $(LIB)/my_reboot.o $(LIB)/xstrncpy.o
	$(CC) $(LDFLAGS) -o $@ $^ $(CRYPT)

initctl: initctl.o
	$(CC) $(LDFLAGS) -o $@ $^

vipw: vipw.o $(LIB)/xstrncpy.o
	$(CC) $(LDFLAGS) -o $@ $^ $(SELINUXLLIB)

newgrp.o: $(LIB)/pathnames.h
	$(CC) -c $(CFLAGS) $(PAMFL) newgrp.c 

wall: wall.o ttymsg.o $(LIB)/carefulputc.o $(LIB)/xstrncpy.o

LOGINFLAGS=
ifeq "$(USE_TTY_GROUP)" "yes"
	LOGINFLAGS += -DUSE_TTY_GROUP
endif
ifeq "$(ALLOW_VCS_USE)" "yes"
	LOGINFLAGS += -DCHOWNVCS
endif
ifeq "$(DO_STAT_MAIL)" "yes"
	LOGINFLAGS += -DDO_STAT_MAIL
endif

login.o: login.c $(LIB)/pathnames.h $(LIB)/setproctitle.c $(LIB)/setproctitle.h
	$(CC) -c $(CFLAGS) $(PAMFL) $(LOGINFLAGS) login.c

# LOGINFLAGS here only for -DUSE_TTY_GROUP
mesg.o: mesg.c $(LIB)/errs.h
	$(CC) -c $(CFLAGS) $(LOGINFLAGS) mesg.c

passwd: passwd.o islocal.o setpwnam.o $(LIB)/env.o $(LIB)/xstrncpy.o
	$(CC) $(LDFLAGS) -o $@ $^ $(CRYPT)

ifeq "$(REQUIRE_PASSWORD)" "yes"
CHSH_FLAGS:=$(CHSH_FLAGS) -DREQUIRE_PASSWORD
endif

ifeq "$(ONLY_LISTED_SHELLS)" "yes"
CHSH_FLAGS:=$(CHSH_FLAGS) -DONLY_LISTED_SHELLS
endif

chsh.o: chsh.c
	$(CC) -c $(CFLAGS) $(PAMFL) $(CHSH_FLAGS) chsh.c

chfn.o: chfn.c
	$(CC) -c $(CFLAGS) $(PAMFL) $(CHSH_FLAGS) chfn.c

install: all $(WHAT_TO_INSTALL)

install-putils: $(BIN.PUTILS) $(USRBIN.PUTILS) $(USRSBIN.PUTILS)
	$(INSTALLDIR) $(BINDIR) $(USRBINDIR) $(USRSBINDIR)
	$(INSTALLBIN) $(BIN.PUTILS) $(BINDIR)
	$(INSTALLSUID) $(USRBIN.PUTILS) $(USRBINDIR)
	$(INSTALLBIN) $(USRSBIN.PUTILS) $(USRSBINDIR)
	(cd $(USRSBINDIR); ln -sf vipw vigr)
	$(INSTALLDIR) $(MAN1DIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN1.PUTILS) $(MAN1DIR)
	$(INSTALLMAN) $(MAN8.PUTILS) $(MAN8DIR)

install-passwd: $(USRBIN.PASSWD)
	$(INSTALLDIR) $(USRBINDIR)
	$(INSTALLSUID) $(USRBIN.PASSWD) $(USRBINDIR)
	$(INSTALLDIR) $(MAN1DIR)
	$(INSTALLMAN) $(MAN1.PASSWD) $(MAN1DIR)

install-init: $(SBIN.INIT)
	$(INSTALLDIR) $(SBINDIR)
	$(INSTALLBIN) $(SBIN.INIT) $(SBINDIR)
	$(INSTALLDIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN8.INIT) $(MAN8DIR)
	# Make *relative* links for these
	(cd $(SHUTDOWNDIR); ln -sf shutdown reboot)
	(cd $(SHUTDOWNDIR); ln -sf shutdown fastboot)
	(cd $(SHUTDOWNDIR); ln -sf shutdown halt)
	(cd $(SHUTDOWNDIR); ln -sf shutdown fasthalt)
	(cd $(SHUTDOWNDIR); ln -sf initctl need)
	(cd $(SHUTDOWNDIR); ln -sf initctl display-services)
	(cd $(SHUTDOWNDIR); ln -sf initctl provide)
	(cd $(MAN8DIR);     ln -sf initctl.8 need.8)
	(cd $(MAN8DIR);     ln -sf initctl.8 display-services.8)
	(cd $(MAN8DIR);     ln -sf initctl.8 provide.8)


install-getty: $(SBIN.GETTY)
	$(INSTALLDIR) $(SBINDIR)
	$(INSTALLBIN) $(SBIN.GETTY) $(SBINDIR)
	$(INSTALLDIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN8.GETTY) $(MAN8DIR)

install-misc: $(USRBIN.MISC)
	$(INSTALLDIR) $(USRBINDIR)
	$(INSTALLBIN) $(USRBIN.MISC) $(USRBINDIR)
	$(INSTALLDIR) $(MAN1DIR)
	$(INSTALLMAN) $(MAN1.MISC) $(MAN1DIR)
ifeq "$(USE_TTY_GROUP)" "yes"
	chgrp tty $(USRBINDIR)/wall
	chmod g+s $(USRBINDIR)/wall
endif

.PHONY: clean distclean
clean distclean:
	-rm -f *.o *~ core $(BIN.PASSWD) $(SBIN.GETTY) $(SBIN.INIT) \
		$(USRBIN.MISC) $(USRBIN.PASSWD) $(USRBIN.PUTILS) \
		$(USRSBIN.PUTILS) $(BIN.PUTILS)
