# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Feb 11 13:47:13 1995
# Revised: Wed Feb 22 16:09:38 1995 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#

include ../MCONFIG

# Where to put man pages?

MAN5= 		devinfo.5 makedev.cfg.5
MAN8=		MAKEDEV-C.8

# Where to put binaries?
# See the "install" rule for the links. . .

DEV=		MAKEDEV-C

# Where to put data?

FILES=		devinfo makedev.cfg

all: $(DEV)

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

# Rules for everything else

MAKEDEV-C: makedev.o
	$(CC) $(LDFLAGS) $< -o $@

install: all
	$(INSTALLDIR) $(DEVDIR) $(ETCDIR)
	$(INSTALLDAT) $(FILES) $(ETCDIR)
	$(INSTALLBIN) $(DEV) $(DEVDIR)
	$(INSTALLDIR) $(MAN5DIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN5) $(MAN5DIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)

.PHONY:	clean
clean:
	-rm -f *.o *~ core $(DEV)
