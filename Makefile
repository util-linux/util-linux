MAKEFLAGS += --no-print-directory

PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man

MKDIR ?= mkdir -p
INSTALL ?= install
CC ?= "gcc"

CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration

OBJS = rfkill.o version.o
ALL = rfkill

ifeq ($(V),1)
Q=
NQ=true
else
Q=@
NQ=echo
endif

all: $(ALL)

VERSION_OBJS := $(filter-out version.o, $(OBJS))

version.c: version.sh $(patsubst %.o,%.c,$(VERSION_OBJS)) rfkill.h Makefile \
		$(wildcard .git/index .git/refs/tags)
	@$(NQ) ' GEN ' $@
	$(Q)./version.sh $@

%.o: %.c rfkill.h
	@$(NQ) ' CC  ' $@
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

rfkill:	$(OBJS)
	@$(NQ) ' CC  ' rfkill
	$(Q)$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o rfkill

check:
	$(Q)$(MAKE) all CC="REAL_CC=$(CC) CHECK=\"sparse -Wall\" cgcc"

%.gz: %
	@$(NQ) ' GZIP' $<
	$(Q)gzip < $< > $@

install: rfkill rfkill.1.gz
	@$(NQ) ' INST rfkill'
	$(Q)$(MKDIR) $(DESTDIR)$(BINDIR)
	$(Q)$(INSTALL) -m 755 -t $(DESTDIR)$(BINDIR) rfkill
	@$(NQ) ' INST rfkill.1'
	$(Q)$(MKDIR) $(DESTDIR)$(MANDIR)/man1/
	$(Q)$(INSTALL) -m 644 -t $(DESTDIR)$(MANDIR)/man1/ rfkill.1.gz

clean:
	$(Q)rm -f rfkill *.o *~ *.gz version.c *-stamp
