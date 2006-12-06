# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Sat Feb 11 17:52:09 1995 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#

include ../MCONFIG

# Where to put man pages?

MAN1=		arch.1 readprofile.1

MAN8= 		MAKEDEV.8 chroot.8 clock.8 ctrlaltdel.8 dmesg.8 ipcrm.8 \
		ipcs.8 kbdrate.8 lpcntl.8 ramsize.8 rdev.8 renice.8 \
		rootflags.8 setserial.8 setsid.8 swapdev.8 sync.8 tunelp.8 \
		update_state.8 vidmode.8

# Where to put binaries?
# See the "install" rule for the links. . .

DEV=            MAKEDEV

SBIN= 		clock kbdrate sln

BIN=            arch dmesg setserial sync

USRSBIN=	chroot ctrlaltdel update_state

USRBIN=		ipcrm ipcs lpcntl rdev renice readprofile setsid tunelp

# Where to put datebase files?

USRINFO=        ipc.info

SCRIPTS= 	reset update_state

all: $(SBIN) $(BIN) $(USRSBIN) $(USRBIN)

sln: sln.c
	$(CC) -static $(CFLAGS) $(LDFLAGS) $< -o $@

sync: sync.S
	/lib/cpp sync.S > sync.s
	as -o sync.o sync.s
	ld -s -N -e _main sync.o -o sync
	-rm sync.s

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

$(SCRIPTS):
	cp $@.sh $@

# Rules for everything else

arch: arch.o
chroot: chroot.o
clock: clock.o
ctrlaltdel: ctrlaltdel.o
ipcrm: ipcrm.o
ipcs: ipcs.o
kbdrate: kbdrate.o
lpcntl: lpcntl.o
rdev: rdev.o
renice: renice.o
readprofile: readprofile.o
setserial: setserial.o
setsid: setsid.o
update_state: update_state.sh

install: all
	$(INSTALLDIR) $(DEVDIR) $(SBINDIR) $(BINDIR) $(USRSBINDIR) $(USRBINDIR)
	$(INSTALLBIN) $(DEV) $(DEVDIR)
	$(INSTALLBIN) $(SBIN) $(SBINDIR)
	$(INSTALLBIN) $(BIN) $(BINDIR)
	$(INSTALLBIN) $(USRSBIN) $(USRSBINDIR)
	$(INSTALLBIN) $(USRBIN) $(USRBINDIR)
	(cd $(RDEVDIR); ln -sf rdev swapdev)
	(cd $(RDEVDIR); ln -sf rdev ramsize)
	(cd $(RDEVDIR); ln -sf rdev vidmode)
	(cd $(RDEVDIR); ln -sf rdev rootflags)
	$(INSTALLDIR) $(MAN1DIR) $(MAN8DIR) $(INFODIR)
	$(INSTALLMAN) $(MAN1) $(MAN1DIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)
	$(INSTALLMAN) $(USRINFO) $(INFODIR)

clean:
	-rm -f *.o *~ core $(SBIN) $(BIN) $(USRSBIN) $(USRBIN)
