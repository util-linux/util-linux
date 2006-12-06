# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Fri Oct  6 20:27:07 1995 by r.faith@ieee.org
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
# May be distirbuted under the GPL
#

include ../MCONFIG

# Where to put man pages?

MAN1= 		cal.1 chkdupexe.1 ddate.1 dnsdomainname.1 domainname.1 \
		dsplit.1 hostid.1 hostname.1 kill.1 logger.1 look.1 mcookie.1 \
		namei.1 reset.1 script.1 setterm.1 tsort.1 \
		whereis.1 write.1

ifeq "$(HAVE_CLEAR)" "no"
MAN1:=$(MAN1) clear.1
endif

# Where to put binaries?
# See the "install" rule for the links. . .

BIN=            domainname hostname kill

USRBIN=		cal chkdupexe ddate dsplit hostid logger look mcookie \
		namei reset script setterm tsort whereis write

ifeq "$(HAVE_CLEAR)" "no"
USRBIN:=$(USRBIN) clear
endif

# Programs requiring special compilation

NEEDS_TERMCAP=  setterm

all: $(BIN) $(USRBIN) $(USRBIN.NONSHADOW) $(USRGAMES) getoptprog

$(NEEDS_TERMCAP):
	$(CC) $(LDFLAGS) $^ -o $@ -ltermcap

%: %.sh
	cp $@.sh $@
	chmod 755 $@

%: %.pl
	cp $@.pl $@
	chmod 755 $@

# Rules for everything else

cal: cal.o $(BSD)/getopt.o $(BSD)/err.o
chkdupexe: chkdupexe.pl
clear: clear.sh
domainname: domainname.o
dsplit: dsplit.o
getoptprog: getoptprog.o $(BSD)/getopt.o
hostid: hostid.o
hostname: hostname.o
kill: kill.o procs.o
logger: logger.o $(BSD)/getopt.o
mcookie: mcookie.o md5.o
md5sum: md5sum.o md5.o
md5.o: md5.h
namei: namei.o
reset: reset.sh
script: script.o
setterm: setterm.o
tsort: tsort.o

install: all
	$(INSTALLDIR) $(BINDIR) $(USRBINDIR)
	$(INSTALLBIN) $(BIN) $(BINDIR)
	$(INSTALLBIN) $(USRBIN) $(USRBINDIR)
	$(INSTALLBIN) getoptprog $(USRBINDIR)/getopt
	(cd $(BINDIR); ln -sf hostname dnsdomainname)
	$(INSTALLDIR) $(MAN1DIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN1) $(MAN1DIR)
	$(INSTALLMAN) getoptprog.1 $(MAN1DIR)/getopt.1
ifeq "$(USE_TTY_GROUP)" "yes"
	chgrp tty $(USRBINDIR)/write
	chmod g+s $(USRBINDIR)/write
endif

.PHONY:	clean
clean:
	-rm -f *.o *~ core $(BIN) $(USRBIN) getoptprog
