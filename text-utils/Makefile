# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Mon Aug 19 20:11:15 1996 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#
include ../make_include
include ../MCONFIG

# Where to put man pages?

MAN1= 		col.1 colcrt.1 colrm.1 column.1 hexdump.1 more.1 rev.1

# Where to put binaries?
# See the "install" rule for the links. . .

BIN=            more

USRBIN=		col colcrt colrm column hexdump rev

ifeq "$(HAVE_NCURSES)" "yes"
USRBIN:=$(USRBIN) ul
MAN1:=$(MAN1) ul.1
endif

# Where to put datebase files?

USRLIB=		more.help

all: $(BIN) $(USRBIN)

# Programs requiring special compilation

NEEDS_NCURSES=  ul

ifeq "$(HAVE_TERMCAP)" "no"
NEEDS_NCURSES:=$(NEEDS_NCURSES) more
else
more:
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LIBTERMCAP)
endif

$(NEEDS_NCURSES):
ifeq "$(HAVE_NCURSES)" "yes"
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LIBCURSES)
else
	@echo $@ not made since it requires ncurses
endif


# Rules for hexdump

hexdump: hexdump.o conv.o display.o hexsyntax.o odsyntax.o parse.o
hexdump.o: hexdump.c hexdump.h
conv.o: conv.c hexdump.h
display.o: display.c hexdump.h
hexsyntax.o: hexsyntax.c hexdump.h
odsyntax.o: odsyntax.c hexdump.h
parse.o: parse.c hexdump.h

# Rules for everything else

col: col.o
colcrt: colcrt.o
colrm: colrm.o
column: column.o $(ERR_O)
more: more.o
more.o: more.c $(LIB)/pathnames.h
rev: rev.o

ifeq "$(HAVE_NCURSES)" "yes"
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
