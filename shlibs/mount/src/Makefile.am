include $(top_srcdir)/config/include-Makefile.am

AM_CPPFLAGS += -I$(ul_libmount_incdir) \
	       -I$(ul_libmount_srcdir) \
	       -I$(ul_libblkid_incdir)

# includes
mountincdir = $(includedir)/mount
nodist_mountinc_HEADERS = mount.h

usrlib_exec_LTLIBRARIES = libmount.la
libmount_la_SOURCES = $(mountinc_HEADERS)

nodist_libmount_la_SOURCES = mount.h version.c utils.c test.c init.c

libmount_la_LIBADD = $(ul_libblkid_la)

libmount_la_DEPENDENCIES = $(libmount_la_LIBADD) mount.sym mount.h.in

libmount_la_LDFLAGS = -Wl,--version-script=$(ul_libmount_srcdir)/mount.sym \
                      -version-info $(LIBMOUNT_VERSION_INFO)

tests =

EXTRA_DIST = mount.sym mount.h.in
CLEANFILES = $(tests)

tests: all $(tests)
test_%: %.c
	$(COMPILE) -DTEST_PROGRAM $< .libs/libmount.a -o $@ $(BLKID_LIBS)


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
