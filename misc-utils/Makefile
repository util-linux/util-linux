# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Mon Nov 11 10:12:16 1996 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
# May be distirbuted under the GPL
#
include ../make_include
include ../MCONFIG

# scriptreplay not added yet

# Where to put man pages?

MAN1= 		cal.1 chkdupexe.1 ddate.1 logger.1 look.1 mcookie.1 \
		namei.1 rename.1 script.1 whereis.1

# Where to put binaries?
# See the "install" rule for the links. . .

BIN=

USRBIN=		cal chkdupexe ddate logger look mcookie \
		namei rename script whereis

MAYBE=		reset setterm

ifeq "$(HAVE_RESET)" "no"
USRBIN:=$(USRBIN) reset
MAN1:=$(MAN1) reset.1
endif

ifeq "$(HAVE_WRITE)" "no"
USRBIN:=$(USRBIN) write
MAN1:=$(MAN1) write.1
endif

ifeq "$(HAVE_KILL)" "no"
BIN:=$(BIN) kill
MAN1:=$(MAN1) kill.1
endif

ifeq "$(HAVE_NCURSES)" "yes"
USRBIN:=$(USRBIN) setterm
MAN1:=$(MAN1) setterm.1
endif

# For script only
LIBPTY=
ifeq "$(HAVE_OPENPTY)" "yes"
LIBPTY:=$(LIBPTY) -lutil
endif

# Programs requiring special compilation
NEEDS_CURSES=  setterm
NEEDS_OPENPTY= script

all: $(BIN) $(USRBIN) $(USRBIN.NONSHADOW)

$(NEEDS_CURSES):
ifeq "$(HAVE_NCURSES)" "yes"
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBCURSES)
else
	@echo $@ not made since it requires ncurses
endif

ifeq "$(HAVE_NCURSES)" "yes"
cal:
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBCURSES)
else
ifeq "$(HAVE_TERMCAP)" "yes"
cal:
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBTERMCAP)
endif
endif

$(NEEDS_OPENPTY):
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBPTY)

%: %.sh
	cp $@.sh $@
	chmod 755 $@

%: %.pl
	cp $@.pl $@
	chmod 755 $@

# Rules for everything else

cal.o: $(LIB)/errs.h
cal: cal.o $(ERR_O)
chkdupexe: chkdupexe.pl
ddate: ddate.o
kill: kill.o procs.o
logger: logger.o
mcookie: mcookie.o $(LIB)/md5.o
mcookie.o: mcookie.c $(LIB)/md5.h
reset: reset.sh
script: script.o
write.o: $(LIB)/carefulputc.h
write: write.o $(LIB)/carefulputc.o

ifeq "$(HAVE_NCURSES)" "yes"
setterm: setterm.o
endif

install: all
	$(INSTALLDIR) $(BINDIR) $(USRBINDIR)
ifneq "$(BIN)" ""
	$(INSTALLBIN) $(BIN) $(BINDIR)
endif
	$(INSTALLBIN) $(USRBIN) $(USRBINDIR)
	$(INSTALLDIR) $(MAN1DIR)
	$(INSTALLMAN) $(MAN1) $(MAN1DIR)
ifeq "$(HAVE_WRITE)" "no"
ifeq "$(USE_TTY_GROUP)" "yes"
	chgrp tty $(USRBINDIR)/write
	chmod g+s $(USRBINDIR)/write
endif
endif

.PHONY:	clean
clean:
	-rm -f *.o *~ core $(BIN) $(USRBIN) $(MAYBE)
