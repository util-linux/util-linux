include $(top_srcdir)/config/include-Makefile.am

if BUILD_SCHEDUTILS

usrbin_exec_PROGRAMS = chrt
dist_man_MANS = chrt.1

if HAVE_IOPRIO_GET
if HAVE_IOPRIO_SET
usrbin_exec_PROGRAMS += ionice
dist_man_MANS += ionice.1
endif
endif

if HAVE_SCHED_GETAFFINITY
usrbin_exec_PROGRAMS += taskset
dist_man_MANS += taskset.1
endif

endif
