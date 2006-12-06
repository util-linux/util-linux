# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)


include ../make_include
include ../MCONFIG

# Where to put man pages?

MAN8= 	blockdev.8 fdformat.8 isosize.8 mkfs.8 mkswap.8 elvtune.8 \
	fsck.minix.8 mkfs.minix.8 mkfs.bfs.8

# Where to put binaries?
# See the "install" rule for the links. . .

SBIN= 	mkfs mkswap blockdev elvtune fsck.minix mkfs.minix mkfs.bfs

USRBIN=	fdformat isosize

ETC=	fdprm

MAYBE=  setfdprm raw fsck.cramfs mkfs.cramfs

ifneq "$(HAVE_FDUTILS)" "yes"
USRBIN:=$(USRBIN) setfdprm
MAN8:=$(MAN8) setfdprm.8
endif

ifeq "$(HAVE_RAW_H)" "yes"
USRBIN:=$(USRBIN) raw
MAN8:=$(MAN8) raw.8
endif

ifeq "$(HAVE_ZLIB)" "yes"
SBIN:=$(SBIN) fsck.cramfs mkfs.cramfs
endif

ifeq "$(HAVE_UUID)" "yes"
MKSWAP_LIBS=-luuid
endif

all: $(SBIN) $(USRBIN)

fsck.cramfs: fsck.cramfs.o
	$(CC) $(LDFLAGS) -o fsck.cramfs fsck.cramfs.o -lz

mkfs.cramfs: mkfs.cramfs.o $(LIB)/md5.o
	$(CC) $(LDFLAGS) -o mkfs.cramfs mkfs.cramfs.o $(LIB)/md5.o -lz

mkfs.cramfs.o: $(LIB)/md5.h

fsck.cramfs.o mkfs.cramfs.o: cramfs.h

fsck.minix.o mkfs.minix.o: bitops.h minix.h

mkswap: mkswap.o $(LIB)/xstrncpy.o
	$(CC) $(LDFLAGS) -o $@ $^ $(MKSWAP_LIBS)

install: all
	$(INSTALLDIR) $(SBINDIR) $(USRBINDIR) $(ETCDIR)
	$(INSTALLBIN) $(SBIN) $(SBINDIR)
	$(INSTALLBIN) $(USRBIN) $(USRBINDIR)
ifneq "$(HAVE_FDUTILS)" "yes"
	$(INSTALLDAT) $(ETC) $(ETCDIR)
endif
	$(INSTALLDIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)

.PHONY:	clean
clean:
	-rm -f *.o *~ core $(SBIN) $(USRBIN) $(MAYBE)
