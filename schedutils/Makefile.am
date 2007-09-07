include $(top_srcdir)/config/include-Makefile.am

if BUILD_SCHEDUTILS

usrbinexec_PROGRAMS = chrt
man_MANS = chrt.1

if HAVE_IOPRIO_GET
if HAVE_IOPRIO_SET
usrbinexec_PROGRAMS += ionice
man_MANS += ionice.1
endif
endif

if HAVE_SCHED_GETAFFINITY
usrbinexec_PROGRAMS += taskset
man_MANS += taskset.1
endif

endif
