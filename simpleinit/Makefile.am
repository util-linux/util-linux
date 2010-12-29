include $(top_srcdir)/config/include-Makefile.am

sbin_PROGRAMS = simpleinit shutdown initctl
dist_man_MANS = fastboot.8 fasthalt.8 halt.8 reboot.8 simpleinit.8 shutdown.8 \
	initctl.8

simpleinit_SOURCES = simpleinit.c simpleinit.h
initctl_SOURCES = initctl.c simpleinit.h

if NEED_LIBCRYPT
simpleinit_LDADD = -lcrypt
endif

install-exec-hook::
	cd $(DESTDIR)$(sbindir) && ln -sf shutdown reboot
	cd $(DESTDIR)$(sbindir) && ln -sf shutdown fastboot
	cd $(DESTDIR)$(sbindir) && ln -sf shutdown halt
	cd $(DESTDIR)$(sbindir) && ln -sf shutdown fasthalt
	cd $(DESTDIR)$(sbindir) && ln -sf initctl need
	cd $(DESTDIR)$(sbindir) && ln -sf initctl display-services
	cd $(DESTDIR)$(sbindir) && ln -sf initctl provide

install-data-hook:
	cd $(DESTDIR)$(mandir)/man8 && ln -sf initctl.8 need.8
	cd $(DESTDIR)$(mandir)/man8 && ln -sf initctl.8 display-services.8
	cd $(DESTDIR)$(mandir)/man8 && ln -sf initctl.8 provide.8

