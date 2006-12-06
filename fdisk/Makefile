# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Fri Oct  6 21:02:21 1995 by r.faith@ieee.org
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#
include ../make_include
include ../MCONFIG

MAN8=
SBIN=
CFDISK=cfdisk
NOTMADE=

ifneq "$(HAVE_SLANG)" "yes"
ifneq "$(HAVE_NCURSES)" "yes"
CFDISK=
NOTMADE=cfdisk
endif
endif

SBIN:=$(SBIN) fdisk
MAN8:=$(MAN8) fdisk.8

ifneq "$(ARCH)" "sparc"
SBIN:=$(SBIN) $(CFDISK) sfdisk
MAN8:=$(MAN8) cfdisk.8 sfdisk.8
endif

all: $(SBIN) $(NOTMADE)

cfdisk.o: cfdisk.c ../MCONFIG
ifeq "$(HAVE_SLANG)" "yes"
	$(CC) -c $(CFLAGS) -DSLCURSES=1 $< -o $@
else
ifeq "$(HAVE_NCURSES)" "yes"
	$(CC) -c $(CFLAGS) $< -o $@
else
	:
endif
endif

cfdisk: cfdisk.o llseek.o disksize.o i386_sys_types.o $(LIB)/xstrncpy.o
ifeq "$(HAVE_SLANG)" "yes"
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBSLANG)
else
ifeq "$(HAVE_NCURSES)" "yes"
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBCURSES)
else
	@echo $@ not made since it requires ncurses or slang
endif
endif

# not installed by default
activate: sfdisk
	rm -f activate
	ln -s sfdisk activate

fdisk: fdisk.o llseek.o disksize.o fdiskbsdlabel.o fdisksgilabel.o \
	fdisksunlabel.o fdiskaixlabel.o i386_sys_types.o partname.o
fdisk.o: fdisk.c fdisk.h
fdiskbsdlabel.o: fdiskbsdlabel.c fdisk.h fdiskbsdlabel.h
fdisksunlabel.o: fdisksunlabel.c fdisksunlabel.h fdisk.h
fdiskaixlabel.o: fdiskaixlabel.c fdiskaixlabel.h fdisk.h
fdisk.o cfdisk.o sfdisk.o fdiskbsdlabel.o fdisksunlabel.o \
	fdisksgilabel.o fdiskaixlabel.o i386_sys_types.o partname.o: common.h
sfdisk: sfdisk.o disksize.o i386_sys_types.o partname.o

install: all
	$(INSTALLDIR) $(SBINDIR)
	$(INSTALLBIN) $(SBIN) $(SBINDIR)
	$(INSTALLDIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)

.PHONY:	clean
clean:
	-rm -f *.o *~ core $(SBIN) partitiontype
