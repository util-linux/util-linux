# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Wed Feb  8 20:30:16 1995 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#

include ../MCONFIG

# Where to put man pages?

MAN6= 		banner.6 ddate.6

# Where to put binaries?
# See the "install" rule for the links. . .

USRGAMES=       banner ddate

all: $(USRGAMES)

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

# Rules for everything else

banner: banner.o $(BSD)/getopt.o $(BSD)/err.o
ddate: ddate.o

install: all
	$(INSTALLDIR) $(USRGAMESDIR)
	$(INSTALLBIN) $(USRGAMES) $(USRGAMESDIR)
	$(INSTALLDIR) $(MAN6DIR)
	$(INSTALLMAN) $(MAN6) $(MAN6DIR)

clean:
	-rm -f *.o *~ core $(USRGAMES)
