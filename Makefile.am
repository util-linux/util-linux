include $(top_srcdir)/config/include-Makefile.am

SUBDIRS = \
	include \
	disk-utils \
	fdisk \
	getopt \
	lib \
	shlibs \
	login-utils \
	misc-utils \
	po \
	schedutils \
	sys-utils \
	text-utils \
	tests


RCS_FIND_IGNORE := \( -name SCCS -o -name BitKeeper -o -name .svn -o \
		-name CVS -o -name .pc -o -name .hg -o -name .git \) -prune -o

if LINUX
SUBDIRS += \
	hwclock \
	partx
endif

if BUILD_MOUNT
SUBDIRS += mount
endif

if BUILD_FSCK
SUBDIRS += fsck
endif

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


checkincludes:
	@find * $(RCS_FIND_IGNORE) \
		-name '*.[hcS]' -type f -print | sort -u \
		| xargs $(top_srcdir)/tools/checkincludes.pl

checkconfig:
	@find * $(RCS_FIND_IGNORE) \
		-name '*.[hcS]' -type f -print | sort -u \
		| xargs $(top_srcdir)/tools/checkconfig.sh $(top_srcdir)


ENABLE_ALL = --enable-static-programs \
 --enable-elvtune --enable-init --enable-kill --enable-last \
 --enable-mesg --enable-partx --enable-raw --enable-rdev --enable-reset \
 --enable-login-utils --enable-write --enable-arch --enable-mount

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc --disable-use-tty-group $(ENABLE_ALL)
