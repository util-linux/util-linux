# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Mon Aug 19 20:11:15 1996 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#
include ../make_include
include ../MCONFIG

MAN1= 		col.1 colcrt.1 colrm.1 column.1 hexdump.1 rev.1 line.1 tailf.1

USRBIN=		col colcrt colrm column hexdump rev line tailf

BIN=

MAYBE=		more pg ul

ifeq "$(HAVE_NCURSES)" "yes"
BIN:=$(BIN) more
USRBIN:=$(USRBIN) ul pg
MAN1:=$(MAN1) ul.1 more.1 pg.1
else
ifeq "$(HAVE_TERMCAP)" "yes"
BIN:=$(BIN) more
MAN1:=$(MAN1) more.1
endif
endif

# Is pg silent?
ifeq "$(SILENT_PG)" "yes"
CFLAGS:=$(CFLAGS) -DPGNOBELL
endif

all: $(BIN) $(USRBIN)

# more and pg and ul use curses - maybe we can't compile them
ifeq "$(HAVE_NCURSES)" "yes"
# Have ncurses - make more and pg and ul
more pg ul:
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LIBCURSES)
more: more.o $(LIB)/xstrncpy.o
pg: pg.o
ul: ul.o
else
# Do not have ncurses - give up on pg and ul
pg ul:
	@echo $@ not made since it requires ncurses
# For more we can also try termcap
ifeq "$(HAVE_TERMCAP)" "yes"
more: more.o $(LIB)/xstrncpy.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LIBTERMCAP)
else
more:
	@echo $@ not made since it requires ncurses or termcap
endif
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
column.o: $(LIB)/errs.h
column: column.o $(ERR_O)
more.o: more.c $(LIB)/pathnames.h
rev: rev.o
line: line.o
tailf: tailf.o

colcrt.o colrm.o column.o rev.o ul.o: $(LIB)/widechar.h

install install.shadow install.text-utils: all
	$(INSTALLDIR) $(BINDIR) $(USRBINDIR) $(MAN1DIR)
ifneq "$(BIN)" ""
	$(INSTALLBIN) $(BIN) $(BINDIR)
endif
	$(INSTALLBIN) $(USRBIN) $(USRBINDIR)
	$(INSTALLMAN) $(MAN1) $(MAN1DIR)

.PHONY: clean distclean
clean:
	-rm -f *.o *~ core $(BIN) $(USRBIN) $(MAYBE)

distclean: clean
