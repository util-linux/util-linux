# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Fri Oct  6 21:02:21 1995 by r.faith@ieee.org
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#

include ../MCONFIG

# Where to put man pages?

MAN8= 		cfdisk.8 fdformat.8 fsck.minix.8 \
		mkfs.8 mkfs.minix.8 mkswap.8 setfdprm.8

MAN8.FDISK=	fdisk.8

# Where to put binaries?
# See the "install" rule for the links. . .

SBIN= 		cfdisk fsck.minix mkfs mkfs.minix mkswap

SBIN.FDISK=	fdisk

USRBIN=		fdformat setfdprm

# Where to put datebase files?

ETC=		fdprm

ifeq "$(HAVE_FDISK)" "no"
SBIN:=$(SBIN) $(SBIN.FDISK)
MAN8:=$(MAN8) $(MAN8.FDISK)
endif

all: $(SBIN) $(USRBIN) 

cfdisk: cfdisk.o llseek.o
	$(CC) $(LDFLAGS) $^ -o $@ -lcurses -ltermcap -lm

# Rules for everything else

fdformat: fdformat.o
fdisk: fdisk.o fdisklabel.o llseek.o
fsck.minix: fsck.minix.o
mkfs: mkfs.o
mkfs.minix: mkfs.minix.o
mkswap: mkswap.o
setfdprm: setfdprm.o
fdisk.o: fdisk.c fdisk.h
fdisklabel.o: fdisklabel.c fdisk.h

install: all
	$(INSTALLDIR) $(SBINDIR) $(USRBINDIR) $(ETCDIR)
	$(INSTALLBIN) $(SBIN) $(SBINDIR)
	$(INSTALLBIN) $(USRBIN) $(USRBINDIR)
	$(INSTALLDAT) $(ETC) $(ETCDIR)
	$(INSTALLDIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)

.PHONY:	clean
clean:
	-rm -f *.o *~ core $(SBIN) $(USRBIN)
