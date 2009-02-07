include $(top_srcdir)/config/include-Makefile.am

AM_LDFLAGS += -L$(top_builddir)/libs/blkid/src -lblkid
AM_CPPFLAGS += -I$(top_builddir)/libs/blkid/src/

sbin_PROGRAMS = blkid findfs
dist_man_MANS = blkid.8 findfs.8

shell_in_files = blkid.sh.in findfs.sh
noinst_SCRIPTS = $(shell_in_files:.sh.in=.sh)

blkid_SOURCES = blkid.c
findfs_SOURCES = findfs.c

CLEANFILES = $(noinst_SCRIPTS)

%.sh: %.sh.in
	sed -e "s|\@SRCDIR\@|$(abs_top_srcdir)|" $< > $@
	chmod +x $@

