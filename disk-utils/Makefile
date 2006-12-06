# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Wed Feb 22 16:09:35 1995 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#

include ../MCONFIG

# Where to put man pages?

MAN8= 		cfdisk.8 fdformat.8 fdisk.8 frag.8 fsck.minix.8 \
		mkfs.8 mkfs.minix.8 mkswap.8 setfdprm.8 

# Where to put binaries?
# See the "install" rule for the links. . .

SBIN= 		cfdisk fdisk fsck.minix mkfs mkfs.minix mkswap 

USRSBIN=	frag

USRBIN=		fdformat setfdprm

# Where to put datebase files?

ETC=		fdprm

all: $(SBIN) $(USRSBIN) $(USRBIN)

cfdisk: cfdisk.c llseek.o
	$(CC) $(CFLAGS) $(LDFLAGS) $< llseek.o -o $@ -lcurses -ltermcap -lm

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

# Rules for everything else

fdformat: fdformat.o
fdisk: fdisk.o llseek.o
frag: frag.o
fsck.minix: fsck.minix.o
mkfs: mkfs.o
mkfs.minix: mkfs.minix.o
mkswap: mkswap.o
setfdprm: setfdprm.o

install: all
	$(INSTALLDIR) $(SBINDIR) $(USRSBINDIR) $(ETCDIR)
	$(INSTALLBIN) $(SBIN) $(SBINDIR)
	$(INSTALLBIN) $(USRSBIN) $(USRSBINDIR)
	$(INSTALLBIN) $(USRBIN) $(USRBINDIR)
	$(INSTALLDAT) $(ETC) $(ETCDIR)
	$(INSTALLDIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)

.PHONY:	clean
clean:
	-rm -f *.o *~ core $(SBIN) $(USRSBIN) $(USRBIN)
