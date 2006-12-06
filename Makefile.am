include $(top_srcdir)/config/include-Makefile.am

SUBDIRS = lib \
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

EXTRA_DIST = config/mkinstalldirs config/include-Makefile.am

distclean-local:
	-for i in `find . -name \*~ -o -name \*.orig -o -name \*.rej`; do rm $$i; done
	rm -rf autom4te.cache
