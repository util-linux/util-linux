include $(top_srcdir)/config/include-Makefile.am

noinst_PROGRAMS = mnt_test_sysinfo
mnt_test_sysinfo_SOURCES = mnt_test_sysinfo.c


noinst_LTLIBRARIES = libpreload-time.la

libpreload_time_la_SOURCES = libpreload-time.c
libpreload_time_la_LDFLAGS = -shared -rpath `pwd`  -avoid-version

