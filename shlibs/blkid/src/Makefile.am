include $(top_srcdir)/config/include-Makefile.am

SUBDIRS = probers .

common_ldadd =
common_cflags =

if HAVE_UUID
if BUILD_LIBUUID
common_ldadd += $(ul_libuuid_la)
common_cflags += -I$(ul_libuuid_srcdir)
else
common_ldadd += $(UUID_LIBS)
common_cflags += $(UUID_CFLAGS)
endif
endif

AM_CPPFLAGS += -I$(ul_libblkid_srcdir) $(common_cflags)

# includes
blkidincdir = $(includedir)/blkid
blkidinc_HEADERS = blkid.h

usrlib_exec_LTLIBRARIES = libblkid.la
libblkid_la_SOURCES = cache.c dev.c devname.c devno.c getsize.c llseek.c  \
		     probe.c read.c resolve.c save.c tag.c version.c verify.c \
		     encode.c blkid.h list.h blkidP.h probers/probers.h \
		     config.c evaluate.c \
		     $(blkidinc_HEADERS) \
		     $(top_srcdir)/lib/blkdev.c \
		     $(top_srcdir)/lib/linux_version.c \
		     $(top_srcdir)/lib/canonicalize.c \
		     $(top_srcdir)/lib/md5.c

libblkid_la_LIBADD = probers/libblkid_probers.la $(common_ldadd)
libblkid_la_DEPENDENCIES = $(libblkid_la_LIBADD) blkid.sym

libblkid_la_LDFLAGS = -Wl,--version-script=$(ul_libblkid_srcdir)/blkid.sym \
                      -version-info $(LIBBLKID_VERSION_INFO)

tests = test_cache test_config test_dev test_devname test_devno test_getsize \
	test_read test_resolve test_save test_tag test_verify test_evaluate

EXTRA_DIST = blkid.sym tst_types.c
CLEANFILES = $(tests)

tests: all $(tests)
test_%: %.c
	$(COMPILE) -DTEST_PROGRAM $< .libs/libblkid.a -o $@ $(UUID_LIBS)


# move lib from $(usrlib_execdir) to $(libdir) if needed
install-exec-hook:
	if test "$(usrlib_execdir)" != "$(libdir)"; then \
		mkdir -p $(DESTDIR)$(libdir); \
		mv $(DESTDIR)$(usrlib_execdir)/libblkid.so.* $(DESTDIR)$(libdir); \
		so_img_name=$$(readlink $(DESTDIR)$(usrlib_execdir)/libblkid.so); \
		so_img_rel_target=$$(echo $(usrlib_execdir) | sed 's,\(^/\|\)[^/][^/]*,..,g'); \
		(cd $(DESTDIR)$(usrlib_execdir) && \
			rm -f libblkid.so && \
			$(LN_S) $$so_img_rel_target$(libdir)/$$so_img_name libblkid.so); \
	fi

uninstall-hook:
	rm -f $(DESTDIR)$(libdir)/libblkid.so*
