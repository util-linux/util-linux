# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Mon Aug 19 20:11:15 1996 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#

include ../MCONFIG

# Where to put man pages?

MAN1= 		col.1 colcrt.1 colrm.1 column.1 hexdump.1 more.1 rev.1 \
		ul.1

ifeq "$(HAVE_STRINGS)" "no"
MAN1:=$(MAN1) strings.1
endif

# Where to put binaries?
# See the "install" rule for the links. . .

BIN=            more

USRBIN=		col colcrt colrm column hexdump rev ul

ifeq "$(HAVE_STRINGS)" "no"
USRBIN:=$(USRBIN) strings
endif

# Where to put datebase files?

USRLIB=		more.help

# Programs requiring special compilation

NEEDS_NCURSES=  more ul

all: $(BIN) $(USRBIN)

$(NEEDS_NCURSES):
ifeq "$(HAVE_NCURSES)" "yes"
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBCURSES)
else
	@echo $@ not made since it requires ncurses
endif

# Rules for hexdump

hexdump: hexdump.o conv.o display.o hexsyntax.o odsyntax.o parse.o \
	$(BSD)/getopt.o
hexdump.o: hexdump.c hexdump.h
conv.o: conv.c hexdump.h
display.o: display.c hexdump.h
hexsyntax.o: hexsyntax.c hexdump.h
odsyntax.o: odsyntax.c hexdump.h
parse.o: parse.c hexdump.h

# Rules for everything else

col: col.o $(BSD)/getopt.o
colcrt: colcrt.o
colrm: colrm.o
column: column.o $(BSD)/err.o
more.o: more.c $(BSD)/pathnames.h
rev: rev.o
strings: strings.o $(BSD)/getopt.o
ifeq "$(HAVE_NCURSES)" "yes"
more: more.o
ul: ul.o
endif

install install.shadow install.text-utils: all
	$(INSTALLDIR) $(BINDIR) $(USRBINDIR) $(USRLIBDIR) $(MAN1DIR)
	$(INSTALLBIN) $(BIN) $(BINDIR)
	$(INSTALLBIN) $(USRBIN) $(USRBINDIR)
	$(INSTALLDAT) $(USRLIB) $(USRLIBDIR)
	$(INSTALLMAN) $(MAN1) $(MAN1DIR)

.PHONY: clean distclean
clean:
	-rm -f *.o *~ core $(BIN) $(USRBIN)

distclean: clean
