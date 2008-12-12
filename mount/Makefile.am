include $(top_srcdir)/config/include-Makefile.am

EXTRA_DIST = README.mount

bin_PROGRAMS = mount umount
sbin_PROGRAMS = losetup swapon
dist_man_MANS = fstab.5 mount.8 swapoff.8 swapon.8 umount.8 losetup.8

utils_common = sundries.c xmalloc.c realpath.c fsprobe.c

if !HAVE_VERSIONSORT
fallback =  ../lib/strverscmp.c
endif

headers_common = fstab.h mount_mntent.h mount_constants.h \
	lomount.h fsprobe.h realpath.h xmalloc.h \
	getusername.h loop.h sundries.h

mount_common = fstab.c mount_mntent.c getusername.c lomount.c \
	$(utils_common) $(headers_common) ../lib/env.c ../lib/linux_version.c \
	../lib/blkdev.c $(fallback)


mount_SOURCES = mount.c $(mount_common) ../lib/setproctitle.c
mount_CFLAGS = $(SUID_CFLAGS) $(AM_CFLAGS)
mount_LDFLAGS = $(SUID_LDFLAGS) $(AM_LDFLAGS)

umount_SOURCES = umount.c $(mount_common)
umount_CFLAGS = $(SUID_CFLAGS) $(AM_CFLAGS)
umount_LDFLAGS = $(SUID_LDFLAGS) $(AM_LDFLAGS)

swapon_SOURCES = swapon.c swap_constants.h $(utils_common)

losetup_SOURCES = lomount.c sundries.c xmalloc.c realpath.c \
	loop.h lomount.h xmalloc.h sundries.h realpath.h $(fallback)
losetup_CPPFLAGS = -DMAIN $(AM_CPPFLAGS)


mount_LDADD = $(LDADD_common)
umount_LDADD = $(LDADD_common)
swapon_LDADD = $(LDADD_common)

LDADD_common =
LDADD_common_static =
mount_static_LDADD =

if HAVE_STATIC_MOUNT
bin_PROGRAMS += mount.static
mount_static_SOURCES = $(mount_SOURCES)
mount_static_LDFLAGS = $(LDFLAGS_STATIC)
mount_static_LDADD += $(LDADD_common_static)
endif

if HAVE_STATIC_UMOUNT
bin_PROGRAMS += umount.static
umount_static_SOURCES = $(umount_SOURCES)
umount_static_LDFLAGS = $(LDFLAGS_STATIC)
umount_static_LDADD = $(LDADD_common_static)
endif

if HAVE_STATIC_LOSETUP
bin_PROGRAMS += losetup.static
losetup_static_SOURCES = $(losetup_SOURCES)
losetup_static_LDFLAGS = $(LDFLAGS_STATIC)
losetup_static_CPPFLAGS = -DMAIN $(AM_CPPFLAGS)
endif

if HAVE_BLKID
utils_common += fsprobe_blkid.c
LDADD_common += $(BLKID_LIBS)
LDADD_common_static += $(BLKID_LIBS_STATIC)
endif

if HAVE_SELINUX
mount_LDADD += $(SELINUX_LIBS)
mount_static_LDADD += $(SELINUX_LIBS_STATIC)
endif

if HAVE_VOLUME_ID
utils_common += fsprobe_volumeid.c
swapon_SOURCES += ../lib/linux_version.c ../lib/blkdev.c
LDADD_common += $(VOLUMEID_LIBS)
LDADD_common_static += $(VOLUMEID_LIBS_STATIC)
endif

if HAVE_PIVOT_ROOT
sbin_PROGRAMS += pivot_root
dist_man_MANS += pivot_root.8
endif

noinst_PROGRAMS = mtab_lock_test
mtab_lock_test_SOURCES = fstab.c sundries.c xmalloc.c $(headers_common)
mtab_lock_test_CPPFLAGS = -DMAIN_TEST_MTABLOCK $(AM_CPPFLAGS)

install-exec-hook:
	chmod 4755 $(DESTDIR)$(bindir)/mount
	chmod 4755 $(DESTDIR)$(bindir)/umount
	cd $(DESTDIR)$(sbindir) && ln -sf swapon swapoff
