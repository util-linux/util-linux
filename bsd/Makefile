# Makefile -- Makefile for util-linux Linux utilities
# Created: Sat Dec 26 20:09:40 1992
# Revised: Fri Mar 10 21:20:41 1995 by faith@cs.unc.edu
# Copyright 1992, 1993, 1994, 1995 Rickard E. Faith (faith@cs.unc.edu)
#

include ../MCONFIG

all: err.o getopt.o
err.o: err.c
getopt.o: getopt.c

.PHONY: clean distclean
clean:
	-rm -f *.o *~ core

install install.shadow:
