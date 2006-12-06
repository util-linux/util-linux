# Makefile -- Makefile for syslogd utility for Linux
# Created: Sat Oct  9 10:25:19 1993
# Revised: Fri Mar 10 21:21:51 1995 by faith@cs.unc.edu
# Copyright 1992, 1993 Rickard E. Faith (faith@cs.unc.edu)
#

include ../MCONFIG

LDLIBS=    -lbsd

MAN5=      syslog.conf.5

MAN8=      syslogd.8

ETC=       syslogd

all: $(ETC)

$(ETC):
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

syslogd: syslogd.o ttymsg.o

install: all
	$(INSTALLDIR) $(USRSBINDIR)
	$(INSTALLBIN) $(ETC) $(USRSBINDIR)
	$(INSTALLDIR) $(MAN5DIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN5) $(MAN5DIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)

clean:
	-rm -f $(ETC) *.o *~
