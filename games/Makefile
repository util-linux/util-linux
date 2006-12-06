# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Fri Oct  6 20:27:05 1995 by r.faith@ieee.org
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
# May be distributed under the GPL
#
include ../make_include
include ../MCONFIG

# Where to put man pages?

MAN6= 		banner.6

# Where to put binaries?
# See the "install" rule for the links. . .

USRGAMES=       banner

all: $(USRGAMES)

# Rules for everything else

banner.o: $(LIB)/errs.h
banner: banner.o $(ERR_O)
ddate: ddate.o

install: all
	$(INSTALLDIR) $(USRGAMESDIR)
	$(INSTALLBIN) $(USRGAMES) $(USRGAMESDIR)
	$(INSTALLDIR) $(MAN6DIR)
	$(INSTALLMAN) $(MAN6) $(MAN6DIR)

clean:
	-rm -f *.o *~ core $(USRGAMES)
