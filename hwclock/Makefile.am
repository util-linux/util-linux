include $(top_srcdir)/config/include-Makefile.am

dist_man_MANS = hwclock.8

sbin_PROGRAMS = hwclock

hwclock_SOURCES = hwclock.c cmos.c rtc.c kd.c clock.h
hwclock_LDADD =

if HAVE_AUDIT
hwclock_LDADD += -laudit
endif

EXTRA_DIST = README.hwclock clock-ppc.c
