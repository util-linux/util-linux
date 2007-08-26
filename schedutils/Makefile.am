include $(top_srcdir)/config/include-Makefile.am

if BUILD_SCHEDUTILS

usrbinexec_PROGRAMS = chrt ionice
man_MANS = chrt.1 ionice.1

if HAVE_SCHED_GETAFFINITY
usrbinexec_PROGRAMS += taskset
man_MANS += taskset.1
endif

endif
