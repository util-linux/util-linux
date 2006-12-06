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


hwclock.o: hwclock.c
hwclock.o cmos.o kd.o: ../defines.h
hwclock.o cmos.o rtc.o kd.o: clock.h
hwclock: hwclock.o cmos.o rtc.o kd.o

CWFLAGS := $(subst -Wmissing-prototypes,,$(CFLAGS))

cmos.o: cmos.c
	$(CC) $(CWFLAGS) -c cmos.c -o $@
# $< expands to clock.h on some systems

install: all
	$(INSTALLDIR) $(SBINDIR) $(BINDIR) $(USRBINDIR)
	$(INSTALLBIN) $(SBIN) $(SBINDIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)

clean:
	-rm -f *.o *~ core $(SBIN)
