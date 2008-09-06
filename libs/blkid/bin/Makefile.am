include $(top_srcdir)/config/include-Makefile.am

AM_LDFLAGS += -L$(top_builddir)/libs/bkid/src -lblkid
AM_CPPFLAGS += -I$(top_builddir)/libs/blkid/src/

sbin_PROGRAMS = blkid

shell_in_files = blkid.sh.in
noinst_SCRIPTS = $(shell_in_files:.sh.in=.sh)

blkid_SOURCES = blkid.c

CLEANFILES = $(noinst_SCRIPTS)

%.sh: %.sh.in
	sed -e "s|\@SRCDIR\@|$(abs_top_srcdir)|" $< > $@
	chmod +x $@

