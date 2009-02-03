include $(top_srcdir)/config/include-Makefile.am

SUBDIRS = probers .

AM_CPPFLAGS += -I$(top_builddir)/libs/blkid/src

# includes
blkidincdir = $(includedir)/blkid
blkidinc_HEADERS = blkid.h

# static library
lib_LIBRARIES = libblkid.a
libblkid_a_SOURCES = cache.c dev.c devname.c devno.c getsize.c llseek.c  \
		     probe.c read.c resolve.c save.c tag.c version.c verify.c \
		     encode.c blkid.h list.h probers/probers.h \
		     config.c evaluate.c \
		     $(blkidinc_HEADERS) \
		     $(top_srcdir)/lib/blkdev.c \
		     $(top_srcdir)/lib/linux_version.c \
		     $(top_srcdir)/lib/canonicalize.c \
		     $(top_srcdir)/lib/md5.c

libblkid_a_LIBADD = probers/libprobers.a
libblkid_a_CFLAGS = -fPIC

tests = test_cache test_config test_dev test_devname test_devno test_getsize \
	test_read test_resolve test_save test_tag test_verify test_evaluate

# shared library (note that we don't use LIBTOOL!)
blkid_IMAGE = libblkid.so
blkid_LDSCRIPT = $(srcdir)/blkid.sym
blkid_LIB = $(blkid_IMAGE).$(BLKID_VERSION)
blkid_SONAME = $(blkid_IMAGE).$(BLKID_VERSION_MAJOR)
blkid_LINKS = $(blkid_IMAGE) $(blkid_SONAME)
blkid_OTHERLDADD =

if HAVE_UUID
blkid_OTHERLDADD += -luuid	#TODO $(UUID_LIBS)
endif

EXTRA_DIST = blkid.sym
CLEANFILES = $(tests) $(blkid_LIB) $(blkid_LINKS)

all-local: $(blkid_LIB)

$(blkid_LIB): $(lib_LIBRARIES) $(blkid_LDSCRIPT)
	$(CC) --shared -o $(blkid_LIB) $(AM_LDFLAGS) \
              -Wl,-soname,$(blkid_SONAME),--version-script,$(blkid_LDSCRIPT) \
	      $(libblkid_a_OBJECTS) $(libblkid_a_LIBADD) $(blkid_OTHERLDADD)
	for I in $(blkid_LINKS); do \
		ln -sf $(blkid_LIB) $$I; \
	done

tests: $(tests) all-local

# TODO: the -md5.o requirement is odd..
test_%: %.c all
	$(COMPILE) -DTEST_PROGRAM $< $(lib_LIBRARIES) libblkid_a-md5.o $(libblkid_a_LIBADD) \
		-o $@ $(blkid_OTHERLDADD)

installdirs-local:
	$(MKDIR_P) $(DESTDIR)$(libdir)

install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(blkidincdir)
	$(INSTALL_PROGRAM) $(blkid_LIB) $(DESTDIR)$(libdir)
	for I in $(blkid_LINKS); do \
		cd $(DESTDIR)$(libdir) && ln -sf $(blkid_LIB) $$I; \
	done

uninstall-hook:
	rm -f $(DESTDIR)$(libdir)/$(blkid_LIB)
	for I in $(blkid_LINKS); do \
		cd $(DESTDIR)$(libdir) && rm -f $(blkid_LIB) $$I; \
	done

