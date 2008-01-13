include $(top_srcdir)/config/include-Makefile.am

bin_PROGRAMS = dmesg

usrbinexec_PROGRAMS = cytune flock ipcrm ipcs renice setsid setarch

cytune_SOURCES = cytune.c cyclades.h

sbin_PROGRAMS = ctrlaltdel

usrsbinexec_PROGRAMS = readprofile tunelp rtcwake

tunelp_SOURCES = tunelp.c lp.h

man_MANS = flock.1 readprofile.1 \
	ctrlaltdel.8 cytune.8 dmesg.1 ipcrm.1 ipcs.1 renice.1 \
	setsid.1 tunelp.8 setarch.8 rtcwake.8

info_TEXINFOS = ipc.texi

if BUILD_ARCH
bin_PROGRAMS += arch
man_MANS += arch.1
endif


if BUILD_RDEV
rdev_common_mans = rdev.8 ramsize.8 rootflags.8 vidmode.8
rdev_common_links = ramsize vidmode rootflags

if ARCH_I86
usrsbinexec_PROGRAMS += rdev
man_MANS += $(rdev_common_mans)
RDEV_LINKS = $(rdev_common_links)
endif
if ARCH_86_64
usrsbinexec_PROGRAMS += rdev
man_MANS += $(rdev_common_mans)
RDEV_LINKS = $(rdev_common_links)
endif

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

SETARCH_MAN_LINKS = $(addsuffix .8, $(SETARCH_LINKS))
man_MANS += $(SETARCH_MAN_LINKS)
CLEANFILES=$(SETARCH_MAN_LINKS)

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

