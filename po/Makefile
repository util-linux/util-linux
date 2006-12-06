PACKAGE = util-linux

include ../make_include
include ../MCONFIG

#
# po2tbl.sed is not taken from /usr/share/gettext/intl
# Often it is not present.
# A RedHat 5.2 installation only has po2tbl.sed.in.
#
PO2TBL = ./po2tbl.sed
INTL = /usr/share/gettext/intl

SHELL = /bin/sh

prefix = $(DESTDIR)/usr
exec_prefix = ${prefix}
datadir = $(prefix)/share
localedir = $(datadir)/locale
gnulocaledir = $(prefix)/share/locale
gettextsrcdir = $(prefix)/share/gettext/po

# Not giving an explicit path improves the chances of finding this
INSTALL = install -c
INSTALL_DATA = ${INSTALL} -m 644

# Not giving an explicit path improves the chances of finding these
GENCAT = gencat
MSGFMT = msgfmt
XGETTEXT = xgettext
MSGMERGE = msgmerge


# ..      for <config.h> (if HAVE_CONFIG_H is set)
# $(INTL) for "libgettext.h"
INCLUDES = -I.. -I$(INTL)

COMPILE = $(CC) -c $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(XCFLAGS)

# Enter here all .po files
POFILES = cs.po de.po fr.po it.po ja.po nl.po pt_BR.po
# the same but with .gmo
GMOFILES = cs.gmo de.gmo fr.gmo it.gmo ja.gmo nl.gmo pt_BR.gmo

CATALOGS = $(GMOFILES)
CATOBJEXT = .gmo
INSTOBJEXT = .mo

.SUFFIXES:
.SUFFIXES: .c .o .po .gmo .mo .msg .cat

.c.o:
	$(COMPILE) $<

.po.mo:
	$(MSGFMT) -o $@ $<

.po.gmo:
	$(MSGFMT) -o $@ $<

.po.cat:
	sed -f $(PO2TBL) < $< > $*.msg \
	  && rm -f $@ && $(GENCAT) $@ $*.msg

all: all-$(HAVE_XGETTEXT)

all-yes: cat-id-tbl.c $(CATALOGS)
all-no:

util-linux.pot:
	$(XGETTEXT) --default-domain=util-linux --directory=.. \
	  --add-comments --keyword=_ --keyword=N_ \
	  --files-from=POTFILES.in $(FOREIGN)
	mv util-linux.po util-linux.pot

cat-id-tbl.c: stamp-cat-id
stamp-cat-id: util-linux.pot
	rm -f cat-id-tbl.tmp
	sed -f $(PO2TBL) util-linux.pot \
		| sed -e "s/@PACKAGE NAME@/util-linux/" > cat-id-tbl.tmp
	if cmp -s cat-id-tbl.tmp cat-id-tbl.c; then \
	  rm cat-id-tbl.tmp; \
	else \
	  echo cat-id-tbl.c changed; \
	  rm -f cat-id-tbl.c; \
	  mv cat-id-tbl.tmp cat-id-tbl.c; \
	fi
	rm -f stamp-cat-id && echo timestamp > stamp-cat-id

install: install-data-$(HAVE_XGETTEXT)
install-data-no: all
install-data-yes: all
	../mkinstalldirs $(datadir);
	@catalogs='$(CATALOGS)'; \
	for cat in $$catalogs; do \
	  case "$$cat" in \
	    *.gmo) destdir=$(gnulocaledir);; \
	    *)     destdir=$(localedir);; \
	  esac; \
	  lang=`echo $$cat | sed 's/$(CATOBJEXT)$$//'`; \
	  dir=$$destdir/$$lang/LC_MESSAGES; \
	  ../mkinstalldirs $$dir; \
	  $(INSTALL_DATA) $$cat $$dir/util-linux$(INSTOBJEXT); \
	  echo "installing $$cat as $$dir/util-linux$(INSTOBJEXT)"; \
	  if test -r $$cat.m; then \
	    $(INSTALL_DATA) $$cat.m $$dir/util-linux$(INSTOBJEXT).m; \
	    echo "installing $$cat.m as $$dir/util-linux$(INSTOBJEXT).m"; \
	  fi; \
	done

uninstall:
	catalogs='$(CATALOGS)'; \
	for cat in $$catalogs; do \
	  lang=`echo $$cat | sed 's/$(CATOBJEXT)$$//'`; \
	  rm -f $(localedir)/$$lang/LC_MESSAGES/util-linux$(INSTOBJEXT); \
	  rm -f $(localedir)/$$lang/LC_MESSAGES/util-linux$(INSTOBJEXT).m; \
	  rm -f $(gnulocaledir)/$$lang/LC_MESSAGES/util-linux$(INSTOBJEXT); \
	  rm -f $(gnulocaledir)/$$lang/LC_MESSAGES/util-linux$(INSTOBJEXT).m; \
	done
	rm -f $(gettextsrcdir)/po-Makefile.in.in

cat-id-tbl.o: $(INTL)/libgettext.h

clean:
	rm -f core core.* *~ *.o util-linux.pot cat-id-tbl.tmp

distclean: clean
	rm -f POTFILES *.gmo *.mo *.msg *.cat *.cat.m

update-po: 
	$(MAKE) util-linux.pot
	catalogs='$(CATALOGS)'; \
	for cat in $$catalogs; do \
	  lang=`echo $$cat | sed 's/$(CATOBJEXT)$$//'`; \
	  mv $$lang.po $$lang.old.po; \
	  echo "$$lang:"; \
	  if $(MSGMERGE) $$lang.old.po util-linux.pot -o $$lang.po; then \
	    rm -f $$lang.old.po; \
	  else \
	    echo "msgmerge for $$cat failed!"; \
	    rm -f $$lang.po; \
	    mv $$lang.old.po $$lang.po; \
	  fi; \
	done

POTFILES: 
	./update-potfiles

