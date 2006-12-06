# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Mon Aug 19 20:12:33 1996 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
# Copyright 1996 Nicolai Langfeldt (janl@math.uio.no)??
#

include ../MCONFIG

# Where to put man pages?

MAN1=		arch.1 readprofile.1

MAN8= 		ctrlaltdel.8 cytune.8 dmesg.8 hwclock.8 \
		ipcrm.8 ipcs.8 kbdrate.8 ramsize.8 renice.8 \
		rootflags.8 setsid.8 sln.8 swapdev.8 tunelp.8 \
		vidmode.8 

# Where to put binaries?
# See the "install" rule for the links. . .

BIN=            arch dmesg

USRBIN=		cytune ipcrm ipcs renice readprofile setsid tunelp

SBIN= 		ctrlaltdel hwclock kbdrate

ifeq "$(HAVE_SLN)" "no"
SBIN:=$(SBIN) sln
endif

ifeq "$(CPU)" "intel"
MAN8:=$(MAN8) rdev.8
USRBIN:=$(USRBIN) rdev
endif

# Where to put datebase files?

USRINFO=        ipc.info

all: $(SBIN) $(BIN) $(USRBIN)

sln: sln.c
	$(CC) -static $(CFLAGS) $(LDFLAGS) $< -o $@

# Rules for everything else

arch: arch.o
hwclock.o: hwclock.c shhopt.h
hwclock: hwclock.o shhopt.o
ctrlaltdel.o: ctrlaltdel.c $(LIB)/linux_reboot.h
ctrlaltdel: ctrlaltdel.o $(LIB)/my_reboot.o
ipcrm: ipcrm.o
ipcs: ipcs.o
kbdrate: kbdrate.o
rdev: rdev.o
renice: renice.o
readprofile: readprofile.o
setsid: setsid.o

install: all
	$(INSTALLDIR) $(SBINDIR) $(BINDIR) $(USRBINDIR)
	$(INSTALLBIN) $(SBIN) $(SBINDIR)
	$(INSTALLBIN) $(BIN) $(BINDIR)
	$(INSTALLBIN) $(USRBIN) $(USRBINDIR)
	(cd $(USRBINDIR); ln -sf rdev swapdev)
	(cd $(USRBINDIR); ln -sf rdev ramsize)
	(cd $(USRBINDIR); ln -sf rdev vidmode)
	(cd $(USRBINDIR); ln -sf rdev rootflags)
	$(INSTALLDIR) $(MAN1DIR) $(MAN8DIR) $(INFODIR)
	$(INSTALLMAN) $(MAN1) $(MAN1DIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)
	$(INSTALLMAN) $(USRINFO) $(INFODIR)

clean:
	-rm -f *.o *~ core $(SBIN) $(BIN) $(USRBIN)
