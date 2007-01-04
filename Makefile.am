include $(top_srcdir)/config/include-Makefile.am

SUBDIRS = lib \
	include \
	disk-utils \
	fdisk \
	getopt \
	hwclock \
	login-utils \
	misc-utils \
	mount \
        partx \
	po \
	schedutils \
	sys-utils \
	text-utils \
	tests

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = 	config/include-Makefile.am \
		autogen.sh \
		licenses/COPYING.GPL \
		licenses/COPYING.UCB

distclean-local:
	-find . \( -name \*~ -o -name \*.orig -o -name \*.rej \) -exec rm -f {} \;
	rm -rf autom4te.cache

mrproper: distclean
	-find -name Makefile.in -exec rm -f {} \;
	rm -f configure ABOUT-NLS
	rm -rf m4
	cd po; rm -f `cat .gitignore`; cd ..
	cd config; rm -f `cat ../.gitignore`; cd ..

# The automake is very stupid for work with man pages. The files from man_MANS are
# not included to the distribution. A solution is use dist_man_MANS or
# EXTRA_DIST = $(man_MANS) -- but these solutions are ugly in case that there
# is a lot of "if <cond>", because the result in EXTRA_DIST or dist_man_MANS
# depends on the condition ;-(
dist-hook:
	-find -regex "\./[a-z\-]*/[a-z\.\-\_]*\.[1-9]" -exec cp {} $(distdir)/{} \;

