include $(top_srcdir)/config/include-Makefile.am

bin_PROGRAMS = dmesg

usrbinexec_PROGRAMS = cytune flock ipcrm ipcs renice setsid

sbin_PROGRAMS = ctrlaltdel

usrsbinexec_PROGRAMS = readprofile tunelp

man_MANS = flock.1 readprofile.1 \
	ctrlaltdel.8 cytune.8 dmesg.8 ipcrm.8 ipcs.8 renice.8 \
	setsid.8 tunelp.8

info_TEXINFOS = ipc.texi


if BUILD_RDEV

if INTEL

usrsbinexec_PROGRAMS += rdev
man_MANS += rdev.8 ramsize.8 rootflags.8 vidmode.8

install-exec-hook:
	cd $(DESTDIR)$(usrsbinexecdir) && ln -sf rdev ramsize
	cd $(DESTDIR)$(usrsbinexecdir) && ln -sf rdev vidmode
	cd $(DESTDIR)$(usrsbinexecdir) && ln -sf rdev rootflags

endif

endif
