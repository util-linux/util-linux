# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)


include ../make_include
include ../MCONFIG

# Where to put man pages?

MAN8= 		fdformat.8 mkfs.8 mkswap.8

# Where to put binaries?
# See the "install" rule for the links. . .

SBIN= 		mkfs mkswap blockdev

USRBIN=		fdformat

ETC=		fdprm

ifneq "$(CPU)" "sparc"
# fsck and mkfs will compile, but there is no kernel support on sparc
MAN8:=$(MAN8) fsck.minix.8 mkfs.minix.8 mkfs.bfs.8
SBIN:=$(SBIN) fsck.minix mkfs.minix mkfs.bfs
endif

ifneq "$(HAVE_FDUTILS)" "yes"
USRBIN:=$(USRBIN) setfdprm
MAN8:=$(MAN8) setfdprm.8
endif

# raw.c does not compile on 2.2.*.
# find out later where it does and add tests to config
ifeq "$(ADD_RAW)" "yes"
USRBIN:=$(USRBIN) raw
MAN8:=$(MAN8) raw.8
endif

all: $(SBIN) $(USRBIN)

fsck.minix.o mkfs.minix.o: bitops.h

install: all
	$(INSTALLDIR) $(SBINDIR) $(USRBINDIR) $(ETCDIR)
	$(INSTALLBIN) $(SBIN) $(SBINDIR)
	$(INSTALLBIN) $(USRBIN) $(USRBINDIR)
ifneq "$(HAVE_FDUTILS)" "yes"
	$(INSTALLDAT) $(ETC) $(ETCDIR)
endif
	$(INSTALLDIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)

.PHONY:	clean
clean:
	-rm -f *.o *~ core $(SBIN) $(USRBIN)
