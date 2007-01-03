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
	text-utils

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = 	config/include-Makefile.am \
		autogen.sh

distclean-local:
	-find . \( -name \*~ -o -name \*.orig -o -name \*.rej \) -exec rm -f {} \;
	rm -rf autom4te.cache

mrproper: distclean
	-find -name Makefile.in -exec rm -f {} \;
	rm -f configure ABOUT-NLS
	rm -rf m4
	cd po; rm -f `cat .gitignore`; cd ..
	cd config; rm -f `cat ../.gitignore`; cd ..

