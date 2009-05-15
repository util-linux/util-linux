include $(top_srcdir)/config/include-Makefile.am

AM_CPPFLAGS += -I$(ul_libuuid_srcdir)

noinst_PROGRAMS = tst_uuid
tst_uuid_LDADD = $(ul_libuuid_la)
tst_uuid_CFLAGS = -I$(ul_libuuid_srcdir)

# includes
uuidincdir = $(includedir)/uuid
uuidinc_HEADERS = uuid.h

lib_LTLIBRARIES = libuuid.la
libuuid_la_SOURCES = clear.c compare.c copy.c gen_uuid.c \
           isnull.c pack.c parse.c unpack.c unparse.c uuidd.h \
           uuid.h uuidP.h uuid_time.c $(uuidinc_HEADERS)

libuuid_la_DEPENDENCIES = $(libuuid_la_LIBADD) uuid.sym

libuuid_la_LDFLAGS = -Wl,--version-script=$(ul_libuuid_srcdir)/uuid.sym \
                     -version-info 1:0:0

#tests = test_uuid_time

EXTRA_DIST = uuid.sym gen_uuid_nt.c
CLEANFILES = $(tests)

tests: all $(tests)
test_%: %.c
	$(COMPILE) -DTEST_PROGRAM $< .libs/libuuid.a -o $@

