# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Fri Oct  6 21:02:21 1995 by r.faith@ieee.org
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#

include ../MCONFIG

# Where to put man pages?

MAN8= 		fdformat.8 mkswap.8 setfdprm.8

ifneq "$(CPU)" "sparc"
MAN8:=$(MAN8) fdisk.8 cfdisk.8 sfdisk.8 fsck.minix.8 mkfs.8 mkfs.minix.8
endif

# Where to put binaries?
# See the "install" rule for the links. . .

SBIN= 		mkfs mkswap

ifneq "$(CPU)" "sparc"
SBIN:=$(SBIN) fdisk cfdisk sfdisk fsck.minix mkfs.minix
endif

USRBIN=		fdformat setfdprm

# Where to put datebase files?

ETC=		fdprm

all: $(SBIN) $(USRBIN) 

cfdisk: cfdisk.o llseek.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBCURSES) -lm

# not installed by default
activate: sfdisk
	rm -f activate
	ln -s sfdisk activate

# Rules for everything else

fdformat: fdformat.o
fdisk: fdisk.o fdisklabel.o llseek.o
sfdisk: sfdisk.o
fsck.minix: fsck.minix.o
fsck.minix.o: fsck.minix.c bitops.h
mkfs: mkfs.o
mkfs.minix: mkfs.minix.o
mkfs.minix.o: mkfs.minix.c bitops.h
mkswap: mkswap.o
setfdprm: setfdprm.o
fdisk.o: fdisk.c fdisk.h
fdisklabel.o: fdisklabel.c fdisk.h fdisklabel.h

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
