include ../make_include
include ../MCONFIG

all: err.o my_reboot.o setproctitle.o env.o carefulputc.o xstrncpy.o md5.o

err.o: err.c

my_reboot.o: my_reboot.c linux_reboot.h

env.o: env.h

setproctitle.o: setproctitle.h

carefulputc.o: carefulputc.h

xstrncpy.o: xstrncpy.h

md5.o: md5.c md5.h

.PHONY: clean
clean:
	-rm -f *.o *~ core

install:
