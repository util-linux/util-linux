include $(top_srcdir)/config/include-Makefile.am

EXTRA_DIST = README.mount

bin_PROGRAMS = mount umount
sbin_PROGRAMS = losetup swapon
man_MANS = fstab.5 mount.8 swapoff.8 swapon.8 umount.8 losetup.8

MNTHDRS = fstab.h linux_fs.h mount_mntent.h mount_constants.h my_dev_t.h \
	mount_paths.h lomount.h fsprobe.h realpath.h xmalloc.h \
	getusername.h loop.h sundries.h

mount_common = fstab.c sundries.c xmalloc.c realpath.c mount_mntent.c \
	fsprobe.c getusername.c lomount.c $(MNTHDRS) ../lib/env.c

mount_SOURCES = mount.c $(mount_common) ../lib/setproctitle.c
mount_CFLAGS = $(SUID_CFLAGS) $(AM_CFLAGS)
mount_LDFLAGS = $(SUID_LDFLAGS) $(AM_LDFLAGS)

umount_SOURCES = umount.c $(mount_common)
umount_CFLAGS = $(SUID_CFLAGS) $(AM_CFLAGS)
umount_LDFLAGS = $(SUID_LDFLAGS) $(AM_LDFLAGS)

swapon_SOURCES = swapon.c xmalloc.c \
	swap_constants.h realpath.c

losetup_SOURCES = lomount.c loop.h lomount.h
losetup_CPPFLAGS = -DMAIN $(AM_CPPFLAGS)

mount_LDADD =
umount_LDADD =
swapon_LDADD =

if HAVE_BLKID
mount_SOURCES += fsprobe_blkid.c
umount_SOURCES += fsprobe_blkid.c
swapon_SOURCES += fsprobe_blkid.c
mount_LDADD += -lblkid -luuid
umount_LDADD += -lblkid -luuid
swapon_LDADD += -lblkid -luuid
endif

if HAVE_SELINUX
mount_LDADD += -lselinux
endif

if HAVE_PIVOT_ROOT
sbin_PROGRAMS += pivot_root
man_MANS += pivot_root.8
endif

noinst_PROGRAMS = mtab_lock_test
mtab_lock_test_SOURCES = fstab.c sundries.c xmalloc.c $(MNTHDRS)
mtab_lock_test_CPPFLAGS = -DMAIN_TEST_MTABLOCK $(AM_CPPFLAGS)

install-exec-hook:
	chmod 4755 $(DESTDIR)$(bindir)/mount
	chmod 4755 $(DESTDIR)$(bindir)/umount
	cd $(DESTDIR)$(sbindir) && ln -sf swapon swapoff
