include $(top_srcdir)/config/include-Makefile.am

usrbinexec_PROGRAMS = getopt

getopt_CPPFLAGS = -DLIBCGETOPT=1 $(AM_CPPFLAGS)

man_MANS = getopt.1

exampledir = $(datadir)/getopt/
dist_example_SCRIPTS = getopt-parse.bash getopt-parse.tcsh \
	getopt-test.bash getopt-test.tcsh

EXTRA_DIST = README Changelog COPYING

