# Makefile -- Makefile for util-linux Linux utilities
#
include ../make_include
include ../MCONFIG

# Where to put man pages?

MAN8= 		hwclock.8

# Where to put binaries?
# See the "install" rule for the links. . .

SBIN= 		hwclock


all: $(SBIN)


CFLAGS = -s -Wall -Wwrite-strings -Wstrict-prototypes -Winline $(CDEBUG)

hwclock: hwclock.o rtc.o directio.o kd.o util.o shhopt.o
hwclock.o: shhopt.h
hwclock.o rtc.o directio.o kd.o util.o: hwclock.h

install: all
	$(INSTALLDIR) $(SBINDIR) $(BINDIR) $(USRBINDIR)
	$(INSTALLBIN) $(SBIN) $(SBINDIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)

clean:
	-rm -f *.o *~ core $(SBIN)
