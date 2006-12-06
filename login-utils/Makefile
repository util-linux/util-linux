# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Wed Feb 22 16:09:31 1995 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#
# Suggested changed from Bauke Jan Douma <bjdouma@xs4all.nl> have been
# implemented to handle shadow and sysvinit systems 

include ../MCONFIG

# Where to put man pages?

MAN1= 		last.1 mesg.1 wall.1

MAN1.NONSHADOW= chfn.1 chsh.1 login.1 newgrp.1 passwd.1

MAN8= 		agetty.8 fastboot.8 fasthalt.8 halt.8 reboot.8 simpleinit.8 \
		shutdown.8

MAN8.NONSHADOW= vipw.8

# Where to put binaries?
# See the "install" rule for the links. . .

SBIN= 		agetty simpleinit shutdown

BIN.NONSHADOW=  login

USRBIN=		last mesg wall

USRBIN.NONSHADOW= chfn chsh newgrp passwd

USRSBIN.NONSHADOW= vipw

PASSWDDIR=	/usr/bin

ifeq "$(HAVE_SHADOW)" "no"
WHAT_TO_BUILD:=$(WHAT_TO_BUILD) all-nonshadow
WHAT_TO_INSTALL:=$(WHAT_TO_INSTALL) install-nonshadow
endif

ifeq "$(HAVE_SYSVINIT)" "no"
WHAT_TO_BUILD:=$(WHAT_TO_BUILD) all-nonsysvinit
WHAT_TO_INSTALL:=$(WHAT_TO_INSTALL) install-nonsysvinit
endif

all: $(WHAT_TO_BUILD)
all-nonshadow: $(BIN.NONSHADOW) $(USRBIN.NONSHADOW) $(USRSBIN.NONSHADOW)
all-nonsysvinit: $(USRBIN) $(SBIN)

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

# Rules for everything else

agetty.o: $(BSD)/pathnames.h
agetty: agetty.o
chfn: chfn.o setpwnam.o
chsh: chsh.o setpwnam.o
last.o: $(BSD)/pathnames.h
last: last.o $(BSD)/getopt.o
login.o: $(BSD)/pathnames.h
login: login.o
mesg: mesg.o
newgrp.o: $(BSD)/pathnames.h
newgrp: newgrp.o
passwd: passwd.o islocal.o
shutdown.o: $(BSD)/pathnames.h
shutdown: shutdown.o
simpleinit.o: $(BSD)/pathnames.h
simpleinit: simpleinit.o
vipw.o: $(BSD)/pathnames.h
vipw: vipw.o
wall: wall.o ttymsg.o

install: all $(WHAT_TO_INSTALL)

install-nonshadow:
	$(INSTALLDIR) $(SBINDIR) $(BINDIR) $(USRBINDIR)
	$(INSTALLBIN) $(BIN.NONSHADOW) $(BINDIR)
	$(INSTALLBIN) $(USRBIN.NONSHADOW) $(USRBINDIR)
	$(INSTALLBIN) $(USRSBIN.NONSHADOW) $(USRSBINDIR)
	$(INSTALLDIR) $(MAN1DIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN1.NONSHADOW) $(MAN1DIR)
	$(INSTALLMAN) $(MAN8.NONSHADOW) $(MAN8DIR)
	chown root $(USRBINDIR)/chsh
	chmod u+s $(USRBINDIR)/chsh
	chown root $(USRBINDIR)/chfn
	chmod u+s $(USRBINDIR)/chfn
	chown root $(USRBINDIR)/newgrp
	chmod u+s $(USRBINDIR)/newgrp
	chown root $(PASSWDDIR)/passwd
	chmod u+s $(PASSWDDIR)/passwd
	chown root $(BINDIR)/login
	chmod u+s $(BINDIR)/login

install-nonsysvinit:
	$(INSTALLDIR) $(SBINDIR) $(BINDIR) $(USRBINDIR)
	$(INSTALLBIN) $(SBIN) $(SBINDIR)
	(cd $(SHUTDOWNDIR); ln -sf shutdown reboot)
	(cd $(SHUTDOWNDIR); ln -sf shutdown fastboot)
	(cd $(SHUTDOWNDIR); ln -sf shutdown halt)
	(cd $(SHUTDOWNDIR); ln -sf shutdown fasthalt)
	$(INSTALLBIN) $(USRBIN) $(USRBINDIR)
	$(INSTALLDIR) $(MAN1DIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN1) $(MAN1DIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)

.PHONY: clean
clean:
	-rm -f *.o *~ core $(SBIN) $(BIN) $(BIN.NONSHADOW) $(USRBIN) \
		$(USRBIN.NONSHADOW) $(USRSBIN.NONSHADOW)
