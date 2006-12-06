# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Fri Oct  6 21:02:21 1995 by r.faith@ieee.org
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#

include ../MCONFIG

# Where to put man pages?

MAN8= 		fdformat.8 mkswap.8 setfdprm.8

# Where to put binaries?
# See the "install" rule for the links. . .

SBIN= 		mkfs mkswap

USRBIN=		fdformat setfdprm

ETC=		fdprm

ifneq "$(CPU)" "sparc"
# fsck and mkfs will compile, but there is no kernel support on sparc
MAN8:=$(MAN8) fsck.minix.8 mkfs.8 mkfs.minix.8
SBIN:=$(SBIN) fsck.minix mkfs.minix
endif

# Where to put datebase files?

all: $(SBIN) $(USRBIN)

fdformat: fdformat.o
fsck.minix: fsck.minix.o
fsck.minix.o: fsck.minix.c bitops.h
mkfs: mkfs.o
mkfs.minix: mkfs.minix.o
mkfs.minix.o: mkfs.minix.c bitops.h
mkswap: mkswap.o
setfdprm: setfdprm.o

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
