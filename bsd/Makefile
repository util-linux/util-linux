# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Sat Feb  4 19:35:59 1995 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#

include ../MCONFIG

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

all: err.o getopt.o
err.o: err.c
getopt.o: getopt.c

.PHONY: clean distclean
clean:
	-rm -f *.o *~ core

install install.shadow:
