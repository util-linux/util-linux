# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Sun Feb 26 17:29:25 1995 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#

VERSION=2.2
include MCONFIG

SUBDIRS= bsd \
	disk-utils \
	games \
	login-utils \
	makedev-1.4.1 \
	misc-utils \
	mount \
	sys-utils \
	syslogd \
	text-utils \
	time


.PHONEY: all install clean
all:
	@for subdir in $(SUBDIRS); do \
		(cd $$subdir && $(MAKE) $@) || exit 1; \
	done

install:
	@if [ "`whoami`" = "root" ]; then umask 022; fi
	@for subdir in $(SUBDIRS); do \
		(cd $$subdir && $(MAKE) $@) || exit 1; \
	done

clean:
	-rm -f *.o *~ core poe.diffs
	@for subdir in $(SUBDIRS) historic/selection; do \
		(cd $$subdir && $(MAKE) $@) || exit 1; \
	done

dist:
	(cd /tmp; \
	rm -rf /tmp/util-linux-$(VERSION); \
	cvs export -fNd util-linux-$(VERSION) -r HEAD util-linux; \
	cd util-linux-$(VERSION); \
	ln -s README util-linux-$(VERSION).bin.Notes; \
	find -type d | xargs chmod 755; \
	find -type f | xargs chmod 644; \
	find -type d | xargs chown root:root; \
	find -type f | xargs chown root:root; \
	cd ..; \
	tar zcvvf util-linux-$(VERSION).tar.gz util-linux-$(VERSION); \
	cp -p util-linux-$(VERSION)/README util-linux-$(VERSION).bin.Notes; \
	echo Done.)
