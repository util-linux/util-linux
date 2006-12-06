.SUFFIXES:

include ../make_include
include ../MCONFIG

GETOPTDIR=$(USRSHAREMISCDIR)/getopt

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

# -Wcast-align causes problems with the identifier stderr on alpha's
# with an old glibc.
# -Wbad-function-cast and -Wmissing-declarations are unknown for gcc 2.5.8.
WARNINGS=-Wall \
         -W -Wshadow -Wpointer-arith -Wcast-qual \
         -Wwrite-strings -Wstrict-prototypes -Wmissing-prototypes \
         -Wnested-externs -Winline
# CFLAGS= $(WARNINGS) $(OPT)

SOURCES=getopt.c
ifeq ($(LIBCGETOPT),0)
SOURCES+=gnu/getopt.c gnu/getopt1.c
endif

OBJECTS=$(SOURCES:.c=.o)

BINARIES=getopt

.PHONY: all clean realclean 
all: $(BINARIES)

clean:
	-$(RM) $(OBJECTS) $(BINARIES) 

getopt: $(OBJECTS)
	$(CC) $(LDFLAGS) $< -o $@

install: getopt
	$(INSTALLDIR) $(USRBINDIR) $(MAN1DIR) $(GETOPTDIR)
	$(INSTALLBIN) getopt $(USRBINDIR)
	$(INSTALLMAN) getopt.1 $(MAN1DIR)
	$(INSTALLBIN) getopt-parse.bash getopt-parse.tcsh \
	              getopt-test.bash getopt-test.tcsh $(GETOPTDIR)
