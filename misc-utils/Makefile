# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Mon Nov 11 10:12:16 1996 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
# May be distirbuted under the GPL
#
include ../make_include
include ../MCONFIG

# Where to put man pages?

MAN1= 		cal.1 chkdupexe.1 ddate.1 kill.1 \
		logger.1 look.1 mcookie.1 namei.1 rename.1 script.1 \
		whereis.1 write.1

# Where to put binaries?
# See the "install" rule for the links. . .

BIN=            kill

USRBIN=		cal chkdupexe ddate logger look mcookie \
		namei rename script whereis write

MAYBE=		reset setterm

ifeq "$(HAVE_RESET)" "no"
USRBIN:=$(USRBIN) reset
MAN1:=$(MAN1) reset.1
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

all: $(BIN) $(USRBIN) $(USRBIN.NONSHADOW) $(USRGAMES) 

$(NEEDS_CURSES):
ifeq "$(HAVE_NCURSES)" "yes"
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBCURSES)
else
	@echo $@ not made since it requires ncurses
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
kill: kill.o procs.o
logger: logger.o
mcookie: mcookie.o md5.o
mcookie.o: mcookie.c md5.h
md5.o: md5.c md5.h
reset: reset.sh
script: script.o
write.o: $(LIB)/carefulputc.h
write: write.o $(LIB)/carefulputc.o

ifeq "$(HAVE_NCURSES)" "yes"
setterm: setterm.o
endif

install: all
	$(INSTALLDIR) $(BINDIR) $(USRBINDIR)
	$(INSTALLBIN) $(BIN) $(BINDIR)
	$(INSTALLBIN) $(USRBIN) $(USRBINDIR)
	$(INSTALLDIR) $(MAN1DIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN1) $(MAN1DIR)
ifeq "$(USE_TTY_GROUP)" "yes"
	chgrp tty $(USRBINDIR)/write
	chmod g+s $(USRBINDIR)/write
endif

.PHONY:	clean
clean:
	-rm -f *.o *~ core $(BIN) $(USRBIN) $(MAYBE)
