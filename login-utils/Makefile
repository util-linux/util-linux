# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Thu Oct 12 10:10:32 1995 by r.faith@ieee.org
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#
# Suggested changed from Bauke Jan Douma <bjdouma@xs4all.nl> have been
# implemented to handle shadow and sysvinit systems 

include ../MCONFIG

# Where to put man pages?

MAN1.MISC=	last.1 mesg.1 wall.1

MAN1.PUTILS= 	chfn.1 chsh.1 login.1 newgrp.1
MAN1.PASSWD=    passwd.1

MAN8.GETTY=	agetty.8

MAN8.INIT=	fastboot.8 fasthalt.8 halt.8 reboot.8 simpleinit.8 shutdown.8

MAN8.PUTILS=	vipw.8

# Where to put binaries?
# See the "install" rule for the links. . .

SBIN.GETTY=	agetty

SBIN.INIT=	simpleinit shutdown

BIN.PUTILS=	login

USRBIN.MISC=	last mesg wall

USRBIN.PUTILS=	chfn chsh newgrp
USRBIN.PASSWD=	passwd

USRSBIN.PUTILS=	vipw

ifeq "$(HAVE_SHADOW)" "no"
ifeq "$(HAVE_PASSWD)" "no"
WHAT_TO_BUILD:=$(WHAT_TO_BUILD) all-passwd all-putils
WHAT_TO_INSTALL:=$(WHAT_TO_INSTALL) install-passwd install-putils
else
WHAT_TO_BUILD:=$(WHAT_TO_BUILD) all-putils
WHAT_TO_INSTALL:=$(WHAT_TO_INSTALL) install-putils
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

agetty.o: $(BSD)/pathnames.h
agetty: agetty.o
chfn: chfn.o setpwnam.o
chsh: chsh.o setpwnam.o
last.o: $(BSD)/pathnames.h
last: last.o $(BSD)/getopt.o
login: login.o checktty.o
mesg: mesg.o $(BSD)/getopt.o $(BSD)/err.o
newgrp.o: $(BSD)/pathnames.h
newgrp: newgrp.o
setpwnam.o: $(BSD)/pathnames.h
shutdown.o: $(BSD)/pathnames.h
shutdown: shutdown.o
simpleinit.o: $(BSD)/pathnames.h
simpleinit: simpleinit.o
vipw.o: $(BSD)/pathnames.h
vipw: vipw.o
wall: wall.o ttymsg.o

ifeq "$(USE_TTY_GROUP)" "yes"
login.o: login.c $(BSD)/pathnames.h
	$(CC) -c $(CFLAGS) -DUSE_TTY_GROUP login.c
mesg.o: mesg.c $(BSD)/err.h
	$(CC) -c $(CFLAGS) -DUSE_TTY_GROUP mesg.c
else
login.o: $(BSD)/pathnames.h
mesg.o: $(BSD)/err.h
endif

passwd: passwd.o islocal.o setpwnam.o
passwd.o: passwd.c
	$(CC) -c $(CFLAGS) -DUSE_SETPWNAM passwd.c

ifeq "$(REQUIRE_PASSWORD)" "yes"
CHSH_FLAGS:=$(CHSH_FLAGS) -DREQUIRE_PASSWORD
endif

ifeq "$(ONLY_LISTED_SHELLS)" "yes"
CHSH_FLAGS:=$(CHSH_FLAGS) -DONLY_LISTED_SHELLS
endif

chsh.o: chsh.c
	$(CC) -c $(CFLAGS) $(CHSH_FLAGS) chsh.c

chfn.o: chfn.c
	$(CC) -c $(CFLAGS) $(CHSH_FLAGS) chfn.c

install: all $(WHAT_TO_INSTALL)

install-putils: $(BIN.PUTILS) $(USRBIN.PUTILS) $(USRSBIN.PUTILS)
	$(INSTALLDIR) $(BINDIR) $(USRBINDIR) $(USRSBINDIR)
	$(INSTALLSUID) $(BIN.PUTILS) $(BINDIR)
	$(INSTALLSUID) $(USRBIN.PUTILS) $(USRBINDIR)
	$(INSTALLBIN) $(USRSBIN.PUTILS) $(USRSBINDIR)
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

.PHONY: clean
clean:
	-rm -f *.o *~ core $(BIN.PASSWD) $(SBIN.GETTY) $(SBIN.INIT) \
		$(USRBIN.MISC) $(USRBIN.PASSWD) $(USRBIN.PUTILS) \
		$(USRSBIN.PUTILS) $(BIN.PUTILS)
