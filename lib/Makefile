include ../MCONFIG

CFLAGS=-I$(LIB) $(OPT)

all: err.o my_reboot.o setproctitle.o

err.o: err.c

my_reboot.o: my_reboot.c linux_reboot.h

setproctitle.o: setproctitle.h

.PHONY: clean
clean:
	-rm -f *.o *~ core

install:
