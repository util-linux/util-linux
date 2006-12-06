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


hwclock.o: hwclock.c shhopt.h
hwclock.o cmos.o rtc.o kd.o: clock.h
hwclock: hwclock.o shhopt.o cmos.o rtc.o kd.o

install: all
	$(INSTALLDIR) $(SBINDIR) $(BINDIR) $(USRBINDIR)
	$(INSTALLBIN) $(SBIN) $(SBINDIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)

clean:
	-rm -f *.o *~ core $(SBIN)
