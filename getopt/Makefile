.SUFFIXES:

include ../MCONFIG

prefix=$(DESTDIR)/usr
bindir=$(USRBINDIR)
mandir=$(MANDIR)
man1dir=$(MAN1DIR)/man1
getoptdir=$(USRLIBDIR)/getopt

# Define this to 0 to use the getopt(3) routines in this package.
LIBCGETOPT=1

SHELL=/bin/sh

LD=ld
RM=rm -f
INSTALL=install

CPPFLAGS=-DLIBCGETOPT=$(LIBCGETOPT)
ifeq ($(LIBCGETOPT),0)
CPPFLAGS+=-I./gnu
endif
WARNINGS=-Wall \
         -W -Wshadow -Wpointer-arith -Wbad-function-cast -Wcast-qual \
         -Wcast-align -Wmissing-declarations \
         -Wwrite-strings -Wstrict-prototypes -Wmissing-prototypes \
         -Wnested-externs -Winline
CFLAGS=$(WARNINGS) $(OPT)

sources=getopt.c
ifeq ($(LIBCGETOPT),0)
sources+=gnu/getopt.c gnu/getopt1.c
endif

objects=$(sources:.c=.o)

binaries=getopt

.PHONY: all clean realclean 
all: $(binaries)

clean:
	-$(RM) $(objects) $(binaries) 

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $*.c -o $*.o

getopt: $(objects)
	$(CC) $(LDFLAGS) -o $@ $(objects)

install: getopt
	$(INSTALL) -m 755 getopt $(bindir)
	$(INSTALL) -m 644 getopt.1 $(man1dir)
	$(INSTALL) -m 755 -d $(getoptdir)
	$(INSTALL) -m 754 parse.bash parse.tcsh test.bash test.tcsh $(getoptdir)
