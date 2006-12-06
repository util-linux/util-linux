include ../make_include
include ../MCONFIG

CFLAGS=-I$(LIB) $(OPT)

all: err.o my_reboot.o setproctitle.o env.o

err.o: err.c

my_reboot.o: my_reboot.c linux_reboot.h

env.o: env.h

setproctitle.o: setproctitle.h

.PHONY: clean
clean:
	-rm -f *.o *~ core

install:
