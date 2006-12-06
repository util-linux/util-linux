# Makefile for selection utility
# Andrew Haylett, 17th June 1993
# Minor modifications by Rik Faith (faith@cs.unc.edu), Sat Nov 20 09:47:59 1993

# bump .., since we live in historic now
include ../../MCONFIG
BINDIR = $(USRBINDIR)
MANEXT = 1

all:	selection test-mouse

selection:	selection.o mouse.o
	$(CC) $(LDFLAGS) -o selection selection.o mouse.o

test-mouse:	test-mouse.o mouse.o
	$(CC) $(LDFLAGS) -o test-mouse test-mouse.o mouse.o

mouse.o:	mouse.c mouse.h

selection.o:	selection.c mouse.h Makefile

test-mouse.o:	test-mouse.c mouse.h

selection.man:	selection.1
		nroff -man selection.1 > selection.man

install:	selection # selection.man
	install -m 755 selection $(BINDIR)/selection
	install -m 644 selection.1 $(MANDIR)/man$(MANEXT)/selection.$(MANEXT)

DIST = selection-1.5
DATE = 17th June 1993
PATCH = patch-0.99.10
SRC = README Makefile selection.1 mouse.c mouse.h selection.c test-mouse.c
DFILES = $(SRC) selection.man $(PATCH)
DIFF = diff
DFLAGS = -u

patch:
	(cd /usr/src/linux; \
	    $(DIFF) -c0 $(DFLAGS) config.in~ config.in; \
	    $(DIFF) $(DFLAGS) kernel/chr_drv/tty_ioctl.c~ kernel/chr_drv/tty_ioctl.c; \
	    $(DIFF) $(DFLAGS) kernel/chr_drv/console.c~ kernel/chr_drv/console.c) \
		> $(PATCH); true

update:
	perl -pi -e 's/\d+\S+ \S+ 199[3]/$(DATE)/' $(SRC)

dist:	update patch $(DFILES)
	rm -fr $(DIST)
	mkdir $(DIST)
	cp $(DFILES) $(DIST)
	tar cf - $(DIST) | gzip -c > $(DIST).tar.z
	rm -fr $(DIST)

clean:
	rm -f selection.o mouse.o test-mouse.o selection test-mouse \
		selection.man *~
