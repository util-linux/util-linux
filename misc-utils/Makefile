# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Mon Nov 11 10:12:16 1996 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
# May be distirbuted under the GPL
#

include ../MCONFIG

# Where to put man pages?

MAN1= 		cal.1 chkdupexe.1 ddate.1 hostid.1 kill.1 \
		logger.1 look.1 mcookie.1 namei.1 script.1 \
		setterm.1 tsort.1 whereis.1 write.1

# Where to put binaries?
# See the "install" rule for the links. . .

BIN=            kill

USRBIN=		cal chkdupexe ddate hostid logger look mcookie \
		namei script setterm tsort whereis write

ifeq "$(HAVE_CLEAR)" "no"
USRBIN:=$(USRBIN) clear
MAN1:=$(MAN1) clear.1
endif

ifeq "$(HAVE_RESET)" "no"
USRBIN:=$(USRBIN) reset
MAN1:=$(MAN1) reset.1
endif

# ifeq "$(HAVE_SYSVINIT)" "no"
# USRBIN:=$(USRBIN) pidof
# endif

# Programs requiring special compilation

NEEDS_CURSES=  setterm

all: $(BIN) $(USRBIN) $(USRBIN.NONSHADOW) $(USRGAMES) getoptprog

$(NEEDS_CURSES):
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBCURSES)

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
getoptprog: getoptprog.o $(BSD)/getopt.o
hostid: hostid.o
kill: kill.o procs.o
logger: logger.o $(BSD)/getopt.o
mcookie: mcookie.o md5.o
mcookie.o: mcookie.c md5.h
md5.o: md5.c md5.h
namei: namei.o
reset: reset.sh
script: script.o
setterm: setterm.o
tsort: tsort.o
# pidof: pidof.o procs.o

install: all
	$(INSTALLDIR) $(BINDIR) $(USRBINDIR)
	$(INSTALLBIN) $(BIN) $(BINDIR)
	$(INSTALLBIN) $(USRBIN) $(USRBINDIR)
	$(INSTALLBIN) getoptprog $(USRBINDIR)/getopt
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
