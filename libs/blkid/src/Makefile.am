include $(top_srcdir)/config/include-Makefile.am

AM_CPPFLAGS += -I$(top_builddir)/blkid/src

# static library
lib_LIBRARIES = libblkid.a
libblkid_a_SOURCES = cache.c dev.c devname.c devno.c getsize.c llseek.c  \
		     probe.c read.c resolve.c save.c tag.c version.c \
		     blkid.h blkidP.h list.h
libblkid_a_CFLAGS = -fPIC

# shared library (note that we don't use LIBTOOL!)
blkid_IMAGE = libblkid.so
blkid_LDSCRIPT = $(srcdir)/blkid.sym
blkid_LIB = $(blkid_IMAGE).$(BLKID_VERSION)
blkid_SONAME = $(blkid_IMAGE).$(BLKID_VERSION_MAJOR)
blkid_LINKS = $(blkid_IMAGE) $(blkid_SONAME)

if HAVE_DEVMAPPER
blkid_OTHERLDADD = $(DEVMAPPER_LIBS)
endif

EXTRA_DIST = blkid.sym

all-local: $(blkid_LIB)

$(blkid_LIB): $(lib_LIBRARIES) $(blkid_LDSCRIPT)
	$(CC) --shared -o $(blkid_LIB) $(AM_LDFLAGS) \
              -Wl,-soname,$(blkid_SONAME),--version-script,$(blkid_LDSCRIPT) \
	      $(blkid_a_OBJECTS) $(blkid_OTHERLDADD)
	for I in $(blkid_LINKS); do \
		ln -sf $(blkid_LIB) $$I; \
	done

clean-local:
	rm -f $(blkid_LIB) $(blkid_LINKS)

installdirs-local:
	$(MKDIR_P) $(DESTDIR)$(libdir)

install-exec-hook:
	$(INSTALL_PROGRAM) $(blkid_LIB) $(DESTDIR)$(libdir)
	for I in $(blkid_LINKS); do \
		cd $(DESTDIR)$(libdir) && ln -sf $(blkid_LIB) $$I; \
	done

uninstall-hook:
	rm -f $(DESTDIR)$(libdir)/$(blkid_LIB)
	for I in $(blkid_LINKS); do \
		cd $(DESTDIR)$(libdir) && rm -f $(blkid_LIB) $$I; \
	done

