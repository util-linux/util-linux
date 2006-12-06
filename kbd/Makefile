# Makefile -- Makefile for util-linux Linux utilities
#
include ../make_include
include ../MCONFIG

# Where to put man pages?

MAN8= 		kbdrate.8

# Where to put binaries?
# See the "install" rule for the links. . .

SBIN= 		kbdrate

all: $(SBIN)

kbdrate: kbdrate.o

install: all
	$(INSTALLDIR) $(SBINDIR) $(MAN8DIR)
	$(INSTALLBIN) $(SBIN) $(SBINDIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)

clean:
	-rm -f *.o *~ core $(SBIN)
