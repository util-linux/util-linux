include $(top_srcdir)/config/include-Makefile.am

bin_PROGRAMS =
if LINUX
bin_PROGRAMS += dmesg
endif

usrbinexec_PROGRAMS = flock ipcrm ipcs renice setsid
if LINUX
usrbinexec_PROGRAMS += cytune setarch
endif

cytune_SOURCES = cytune.c cyclades.h

if LINUX
sbin_PROGRAMS = ctrlaltdel
endif

usrsbinexec_PROGRAMS = readprofile
if LINUX
usrsbinexec_PROGRAMS += tunelp rtcwake
endif

tunelp_SOURCES = tunelp.c lp.h

dist_man_MANS = flock.1 readprofile.1 \
	ctrlaltdel.8 cytune.8 dmesg.1 ipcrm.1 ipcs.1 renice.1 \
	setsid.1 tunelp.8 setarch.8 rtcwake.8

info_TEXINFOS = ipc.texi

if BUILD_ARCH
bin_PROGRAMS += arch
dist_man_MANS += arch.1
endif


RDEV_LINKS =
if BUILD_RDEV
usrsbinexec_PROGRAMS += rdev
RDEV_LINKS += ramsize vidmode rootflags
dist_man_MANS += rdev.8
endif


SETARCH_LINKS = linux32 linux64

if ARCH_S390
SETARCH_LINKS += s390 s390x
endif
if ARCH_I86
SETARCH_LINKS += i386
endif
if ARCH_86_64
SETARCH_LINKS += i386 x86_64
endif
if ARCH_PPC
SETARCH_LINKS += ppc ppc64 ppc32
endif
if ARCH_SPARC
SETARCH_LINKS += sparc sparc64 sparc32 sparc32bash
endif
if ARCH_MIPS
SETARCH_LINKS += mips mips64 mips32
endif
if ARCH_IA64
SETARCH_LINKS += i386 ia64
endif
if ARCH_HPPA
SETARCH_LINKS += parisc parisc64 parisc32
endif

RDEV_MAN_LINKS = $(RDEV_LINKS:=.8)
SETARCH_MAN_LINKS = $(SETARCH_LINKS:=.8)
man_MANS = $(RDEV_MAN_LINKS) $(SETARCH_MAN_LINKS)
CLEANFILES = $(man_MANS)

$(RDEV_MAN_LINKS): rdev.8
	echo ".so man8/rdev.8" > $@

$(SETARCH_MAN_LINKS): setarch.8
	echo ".so man8/setarch.8" > $@

install-exec-hook:
	for I in $(RDEV_LINKS); do \
		cd $(DESTDIR)$(usrsbinexecdir) && ln -sf rdev $$I ; \
	done
	for I in $(SETARCH_LINKS); do \
		cd $(DESTDIR)$(usrbinexecdir) && ln -sf setarch $$I ; \
	done

uninstall-hook:
	for I in $(RDEV_LINKS); do \
		rm -f $(DESTDIR)$(usrbinexecdir)/$$I ; \
	done
	for I in $(SETARCH_LINKS); do \
		rm -f $(DESTDIR)$(usrbinexecdir)/$$I ; \
	done

