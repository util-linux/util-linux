# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Sat Oct  7 19:24:39 1995 by r.faith@ieee.org
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#

include ../MCONFIG

# Where to put man pages?

MAN1=		arch.1 readprofile.1

MAN8= 		chroot.8 clock.8 ctrlaltdel.8 cytune.8 dmesg.8 \
		ipcrm.8 ipcs.8 kbdrate.8 ramsize.8 rdev.8 renice.8 \
		rootflags.8 setserial.8 setsid.8 swapdev.8 sync.8 tunelp.8 \
		vidmode.8

# Where to put binaries?
# See the "install" rule for the links. . .

SBIN= 		clock ctrlaltdel kbdrate sln

BIN=            arch dmesg setserial sync

USRSBIN=	chroot

USRBIN=		cytune ipcrm ipcs rdev renice readprofile setsid tunelp

# Where to put datebase files?

USRINFO=        ipc.info

all: $(SBIN) $(BIN) $(USRSBIN) $(USRBIN)

sln: sln.c
	$(CC) -static $(CFLAGS) $(LDFLAGS) $< -o $@

sync: sync.S
	$(CC) -nostdlib $(LDFLAGS) $< -o $@

# Rules for everything else

arch: arch.o
chroot: chroot.o
clock: clock.o
ctrlaltdel: ctrlaltdel.o
ipcrm: ipcrm.o
ipcs: ipcs.o
kbdrate: kbdrate.o
rdev: rdev.o
renice: renice.o
readprofile: readprofile.o
setserial: setserial.o
setsid: setsid.o

install: all
	$(INSTALLDIR) $(SBINDIR) $(BINDIR) $(USRSBINDIR) $(USRBINDIR)
	$(INSTALLBIN) $(SBIN) $(SBINDIR)
	$(INSTALLBIN) $(BIN) $(BINDIR)
	$(INSTALLBIN) $(USRSBIN) $(USRSBINDIR)
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
	-rm -f *.o *~ core $(SBIN) $(BIN) $(USRSBIN) $(USRBIN)
