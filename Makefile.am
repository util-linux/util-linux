include $(top_srcdir)/config/include-Makefile.am

pkgconfigdir = $(usrlib_execdir)/pkgconfig

dist_noinst_HEADERS =
noinst_PROGRAMS =
noinst_LTLIBRARIES =
usrbin_exec_PROGRAMS =
usrsbin_exec_PROGRAMS =
dist_man_MANS =
man_MANS =
pkgconfig_DATA =
usrlib_exec_LTLIBRARIES =
bin_PROGRAMS =
sbin_PROGRAMS =
dist_usrbin_exec_SCRIPTS =

INSTALL_EXEC_HOOKS =
UNINSTALL_HOOKS =

INSTALL_EXEC_HOOKS =
UNINSTALL_HOOKS =
INSTALL_DATA_HOOKS =

EXTRA_DIST =
CLEANFILES =

MAN_DIRS = man/ru

SUBDIRS = \
	$(MAN_DIRS) \
	disk-utils \
	fdisk \
	getopt \
	misc-utils \
	po \
	sys-utils \
	tests

RCS_FIND_IGNORE = \( -name SCCS -o -name BitKeeper -o -name .svn -o \
		-name CVS -o -name .pc -o -name .hg -o -name .git \) -prune -o

if BUILD_HWCLOCK
SUBDIRS += hwclock
endif

if BUILD_PARTX
SUBDIRS += partx
endif

AUTOMAKE_OPTIONS = gnu
ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST += \
	.version \
	autogen.sh \
	Documentation \
	po/update-potfiles \
	README.licensing


include include/Makemodule.am
include lib/Makemodule.am
include libuuid/Makemodule.am
include libblkid/Makemodule.am
include libmount/Makemodule.am

include schedutils/Makemodule.am
include text-utils/Makemodule.am
include term-utils/Makemodule.am
include login-utils/Makemodule.am
include mount-deprecated/Makemodule.am

# Arrange so that .tarball-version appears only in the distribution
# tarball, and never in a checked-out repository.
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version
# Set ChangeLog tag reference.
	sed -i "s/log/log;h=$(VERSION)/" $(distdir)/ChangeLog

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

checkmans:
	@$(top_srcdir)/tools/checkmans.sh

checkxalloc:
	@ $(top_srcdir)/tools/checkxalloc.sh

checkdecl:
	@ $(top_srcdir)/tools/checkdecl.sh

ENABLE_ALL = --enable-static-programs --enable-most-builds
DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc --disable-use-tty-group \
			    --disable-silent-rules $(ENABLE_ALL) \
			    --with-systemdsystemunitdir=$$dc_install_base/$(systemdsystemunitdir)


BUILT_SOURCES = .version
.version:
	echo $(VERSION) > $@-t && mv $@-t $@

CHANGELOG_FILE = v$(VERSION)-ChangeLog

$(CHANGELOG_FILE):
	@ if test "x$$PREVIOUS_VERSION" == "x"; then \
		echo "Usage make changelog PREVIOUS_VERSION=<version>"; \
		exit 1; \
	fi
	@ echo "Changes between v$(PREVIOUS_VERSION) and v$(VERSION)" > $(CHANGELOG_FILE)
	@ echo "--------------------------------------------" >> $(CHANGELOG_FILE)
	@ echo >> $(CHANGELOG_FILE)
	$(AM_V_GEN) git log --stat v$(PREVIOUS_VERSION)..HEAD >> $(CHANGELOG_FILE)

changelog: $(CHANGELOG_FILE)

install-exec-hook: $(INSTALL_EXEC_HOOKS)

uninstall-hook: $(UNINSTALL_HOOKS)

install-data-hook: $(INSTALL_DATA_HOOKS)

