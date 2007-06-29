include $(top_srcdir)/config/include-Makefile.am

bin_PROGRAMS = dmesg

usrbinexec_PROGRAMS = cytune flock ipcrm ipcs renice setsid

cytune_SOURCES = cytune.c cyclades.h

sbin_PROGRAMS = ctrlaltdel

usrsbinexec_PROGRAMS = readprofile tunelp

tunelp_SOURCES = tunelp.c lp.h

man_MANS = flock.1 readprofile.1 \
	ctrlaltdel.8 cytune.8 dmesg.1 ipcrm.1 ipcs.1 renice.1 \
	setsid.1 tunelp.8

info_TEXINFOS = ipc.texi


if BUILD_RDEV
if INTEL
usrsbinexec_PROGRAMS += rdev
man_MANS += rdev.8 ramsize.8 rootflags.8 vidmode.8
RDEV_LINKS = ramsize vidmode rootflags
endif
endif

install-exec-hook:
	for I in $(RDEV_LINKS); do \
		cd $(DESTDIR)$(usrsbinexecdir) && ln -sf rdev $$I ; \
	done
