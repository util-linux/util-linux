# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Mon Aug 19 20:12:33 1996 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#
include ../make_include
include ../MCONFIG

# Where to put man pages?

MAN1=		arch.1 flock.1 readprofile.1

MAN8= 		ctrlaltdel.8 cytune.8 dmesg.8 \
		ipcrm.8 ipcs.8 renice.8 \
		setsid.8 sln.8 tunelp.8

# Where to put binaries?
# See the "install" rule for the links. . .

BIN=            arch dmesg

USRBIN=		cytune flock ipcrm ipcs renice setsid

USRSBIN=	readprofile tunelp

SBIN= 		ctrlaltdel

NOTMADE=

CWFLAGS := $(subst -Wmissing-prototypes,,$(CFLAGS))

ifeq "$(HAVE_SLN)" "no"
ifeq "$(CAN_DO_STATIC)" "no"
NOTMADE=nosln
else
SBIN:=$(SBIN) sln
endif
endif

ifeq "$(ARCH)" "intel"
MAN8:=$(MAN8) rdev.8 ramsize.8 rootflags.8 vidmode.8
USRSBIN:=$(USRSBIN) rdev
endif

# Where to put datebase files?

USRINFO=        ipc.info

all: $(SBIN) $(BIN) $(USRBIN) $(USRSBIN) $(NOTMADE)

# Sometimes indirectly include <asm/bitops.h>
cytune.o: cytune.c
	$(CC) $(CWFLAGS) -c $< -o $@

ipcs.o: ipcs.c
	$(CC) $(CWFLAGS) -c $< -o $@

ipcrm.o: ipcrm.c
	$(CC) $(CWFLAGS) -c $< -o $@

sln: sln.c
	$(CC) -static $(CFLAGS) $(LDFLAGS) $< -o $@

nosln:
	@echo sln not made since static compilation fails here

# Rules for everything else

arch: arch.o
ctrlaltdel.o: ctrlaltdel.c $(LIB)/linux_reboot.h
ctrlaltdel: ctrlaltdel.o $(LIB)/my_reboot.o
cytune.o: cytune.c cyclades.h
cytune: cytune.o
ipcrm: ipcrm.o
ipcs: ipcs.o
rdev: rdev.o
renice: renice.o
readprofile: readprofile.o
setsid: setsid.o

ipc.info: ipc.texi
	makeinfo ipc.texi

install: all
	$(INSTALLDIR) $(SBINDIR) $(BINDIR) $(USRBINDIR) $(USRSBINDIR)
	$(INSTALLBIN) $(SBIN) $(SBINDIR)
	$(INSTALLBIN) $(BIN) $(BINDIR)
	$(INSTALLBIN) $(USRBIN) $(USRBINDIR)
	$(INSTALLBIN) $(USRSBIN) $(USRSBINDIR)
ifeq "$(ARCH)" "intel"
	(cd $(USRSBINDIR); ln -sf rdev ramsize)
	(cd $(USRSBINDIR); ln -sf rdev vidmode)
	(cd $(USRSBINDIR); ln -sf rdev rootflags)
endif
	$(INSTALLDIR) $(MAN1DIR) $(MAN8DIR) $(INFODIR)
	$(INSTALLMAN) $(MAN1) $(MAN1DIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)
	$(INSTALLMAN) $(USRINFO) $(INFODIR)

clean:
	-rm -f *.o *~ core $(SBIN) $(BIN) $(USRBIN) $(USRSBIN)
