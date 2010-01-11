include $(top_srcdir)/config/include-Makefile.am

AM_CPPFLAGS += -I$(ul_libmount_incdir) \
	       -I$(ul_libmount_srcdir) \
	       -I$(ul_libblkid_incdir)

# includes
mountincdir = $(includedir)/mount
nodist_mountinc_HEADERS = mount.h

usrlib_exec_LTLIBRARIES = libmount.la
libmount_la_SOURCES = $(mountinc_HEADERS)

nodist_libmount_la_SOURCES = mount.h version.c utils.c test.c init.c cache.c \
			optstr.c optmap.c optent.c optls.c iter.c list.h \
			$(top_srcdir)/lib/canonicalize.c

libmount_la_LIBADD = $(ul_libblkid_la)

libmount_la_DEPENDENCIES = $(libmount_la_LIBADD) mount.sym mount.h.in

libmount_la_LDFLAGS = -Wl,--version-script=$(ul_libmount_srcdir)/mount.sym \
                      -version-info $(LIBMOUNT_VERSION_INFO)

EXTRA_DIST = mount.sym mount.h.in
CLEANFILES = $(tests)

# move lib from $(usrlib_execdir) to $(libdir) if needed
install-exec-hook:
	if test "$(usrlib_execdir)" != "$(libdir)"; then \
		mkdir -p $(DESTDIR)$(libdir); \
		mv $(DESTDIR)$(usrlib_execdir)/libmount.so.* $(DESTDIR)$(libdir); \
		so_img_name=$$(readlink $(DESTDIR)$(usrlib_execdir)/libmount.so); \
		so_img_rel_target=$$(echo $(usrlib_execdir) | sed 's,\(^/\|\)[^/][^/]*,..,g'); \
		(cd $(DESTDIR)$(usrlib_execdir) && \
			rm -f libmount.so && \
			$(LN_S) $$so_img_rel_target$(libdir)/$$so_img_name libmount.so); \
	fi

uninstall-hook:
	rm -f $(DESTDIR)$(libdir)/libmount.so*

# tests
noinst_PROGRAMS = test_version test_cache test_optstr test_optls
tests_cppflags = $(AM_CPPFLAGS) -DTEST_PROGRAM
tests_ldadd = .libs/libmount.a $(ul_libblkid_la)

test_version_SOURCES = version.c
test_version_CPPFLAGS = $(tests_cppflags)
test_version_LDADD = $(tests_ldadd)

test_cache_SOURCES = cache.c
test_cache_CPPFLAGS = $(tests_cppflags)
test_cache_LDADD = $(tests_ldadd)

test_optstr_SOURCES = optstr.c
test_optstr_CPPFLAGS = $(tests_cppflags)
test_optstr_LDADD = $(tests_ldadd)

test_optls_SOURCES = optls.c
test_optls_CPPFLAGS = $(tests_cppflags)
test_optls_LDADD = $(tests_ldadd)
