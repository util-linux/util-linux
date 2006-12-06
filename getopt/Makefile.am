include $(top_srcdir)/config/include-Makefile.am

usrbinexec_PROGRAMS = getopt

getopt_CFLAGS = -DLIBCGETOPT=1

man_MANS = getopt.1

exampledir = $(prefix)/usr/share/getopt/
example_DATA = getopt-parse.bash getopt-parse.tcsh \
	getopt-test.bash getopt-test.tcsh
