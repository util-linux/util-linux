include $(top_srcdir)/config/include-Makefile.am

SUBDIRS = \
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
		README.* \
		licenses/* \
		example.files/* \
		po/Makevars.template \
		po/update-potfiles


distclean-local:
	-find . \( -name \*~ -o -name \*.orig -o -name \*.rej \) -exec rm -f {} \;
	rm -rf autom4te.cache

mrproper: distclean
	if [ -f ".gitignore" ]; then \
		find -name Makefile.in -exec rm -f {} \; ; \
		rm -f configure ABOUT-NLS ; \
		rm -rf m4 ; \
		cd config; rm -f `cat ../.gitignore`; cd .. ; \
		cd po; rm -f `cat .gitignore`; cd .. ; \
	fi
