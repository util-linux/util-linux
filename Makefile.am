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

EXTRA_DIST = \
		autogen.sh \
		README.devel \
		README.licensing \
		DEPRECATED \
		licenses \
		example.files \
		po/update-potfiles \
		tools \
		docs


distclean-local:
	-find . -name \*~ -o -name \*.orig -o -name \*.rej | xargs rm -f
	rm -rf autom4te.cache

ENABLE_ALL = --enable-elvtune --enable-init --enable-kill --enable-last \
 --enable-mesg --enable-partx --enable-raw --enable-rdev --enable-reset \
 --enable-login-utils --enable-write
DISTCHECK_CONFIGURE_FLAGS = --disable-use-tty-group $(ENABLE_ALL)
