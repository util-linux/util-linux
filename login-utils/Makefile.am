include $(top_srcdir)/config/include-Makefile.am

bin_PROGRAMS =
usrbin_exec_PROGRAMS =
sbin_PROGRAMS =
usrsbin_exec_PROGRAMS =
dist_man_MANS =

if BUILD_LAST
usrbin_exec_PROGRAMS += last
dist_man_MANS += last.1
endif

if BUILD_SULOGIN
sbin_PROGRAMS += sulogin
dist_man_MANS += sulogin.8
sulogin_SOURCES = sulogin.c $(top_srcdir)/lib/strutils.c
sulogin_LDADD =
if HAVE_LIBCRYPT
sulogin_LDADD += -lcrypt
endif
if HAVE_SELINUX
sulogin_LDADD += -lselinux
endif
endif # BUILD_SULOGIN


if BUILD_LOGIN
bin_PROGRAMS += login
dist_man_MANS += login.1
login_SOURCES = \
	login.c \
	logindefs.c \
	logindefs.h \
	$(top_srcdir)/lib/fileutils.c \
	$(top_srcdir)/lib/setproctitle.c
login_LDADD = -lpam -lpam_misc
if HAVE_AUDIT
login_LDADD += -laudit
endif
if HAVE_SELINUX
login_LDADD += -lselinux
endif
endif # BUILD_LOGIN


if BUILD_CHFN_CHSH
usrbin_exec_PROGRAMS += chfn chsh
dist_man_MANS += chfn.1 chsh.1

chfn_chsh_sources = \
	islocal.c \
	islocal.h \
	setpwnam.c \
	setpwnam.h \
	$(top_srcdir)/lib/env.c \
	$(top_srcdir)/lib/fileutils.c

chfn_chsh_cflags = $(SUID_CFLAGS) $(AM_CFLAGS)
chfn_chsh_ldflags = $(SUID_LDFLAGS) $(AM_LDFLAGS)
chfn_chsh_ldadd = -lpam -lpam_misc

if HAVE_SELINUX
chfn_chsh_sources += selinux_utils.c selinux_utils.h
chfn_chsh_ldadd += -lselinux
endif

chfn_SOURCES = chfn.c $(chfn_chsh_sources)
chfn_CFLAGS = $(chfn_chsh_cflags)
chfn_LDFLAGS = $(chfn_chsh_ldflags)
chfn_LDADD = $(chfn_chsh_ldadd)

chsh_SOURCES = chsh.c $(chfn_chsh_sources)
chsh_CFLAGS = $(chfn_chsh_cflags)
chsh_LDFLAGS = $(chfn_chsh_ldflags)
chsh_LDADD = $(chfn_chsh_ldadd)
endif # BUILD_CHFN_CHSH


if BUILD_SU
bin_PROGRAMS += su
su_SOURCES = \
	su.c \
	logindefs.c \
	logindefs.h
su_CFLAGS = $(SUID_CFLAGS) $(AM_CFLAGS)
su_LDFLAGS = $(SUID_LDFLAGS) $(AM_LDFLAGS)
su_LDADD = -lpam -lpam_misc
endif

if BUILD_NEWGRP
usrbin_exec_PROGRAMS += newgrp
dist_man_MANS += newgrp.1
newgrp_CFLAGS = $(SUID_CFLAGS) $(AM_CFLAGS)
newgrp_LDFLAGS = $(SUID_LDFLAGS) $(AM_LDFLAGS)
newgrp_LDADD =
if HAVE_LIBCRYPT
newgrp_LDADD += -lcrypt
endif
endif # BUILD_NEWGRP


if BUILD_VIPW
usrsbin_exec_PROGRAMS += vipw
dist_man_MANS += vigr.8 vipw.8
vipw_SOURCES = \
	vipw.c \
	setpwnam.h \
	$(top_srcdir)/lib/fileutils.c
vipw_LDADD =
if HAVE_SELINUX
vipw_LDADD += -lselinux
endif
endif # BUILD_VIPW


noinst_PROGRAMS = test_islocal test_logindefs
test_islocal_SOURCES = islocal.c
test_islocal_CPPFLAGS = -DTEST_PROGRAM $(AM_CPPFLAGS)

test_logindefs_SOURCES = logindefs.c logindefs.h
test_logindefs_CPPFLAGS = -DTEST_PROGRAM $(AM_CPPFLAGS)


install-exec-hook:
if BUILD_SU
if MAKEINSTALL_DO_SETUID
	chmod 4755 $(DESTDIR)$(bindir)/su
endif
endif
if BUILD_VIPW
	cd $(DESTDIR)$(usrsbin_execdir) && ln -sf vipw vigr
endif
