include $(top_srcdir)/config/include-Makefile.am

EXTRA_DIST = README.mount swap.configure

AM_CPPFLAGS += -DHAVE_NFS

bin_PROGRAMS = mount umount
sbin_PROGRAMS = losetup swapon
man_MANS = fstab.5 nfs.5 mount.8 swapoff.8 swapon.8 umount.8 losetup.8

MNTHDRS = fstab.h linux_fs.h mount_mntent.h mount_constants.h my_dev_t.h \
	mount_paths.h get_label_uuid.h lomount.h mount_blkid.h \
	mount_guess_fstype.h nfs_mount4.h realpath.h xmalloc.h \
	getusername.h loop.h mount_by_label.h mount_guess_rootdev.h \
	nfsmount.h sundries.h

mount_SOURCES = mount.c fstab.c sundries.c xmalloc.c realpath.c mount_mntent.c \
	get_label_uuid.c mount_by_label.c mount_blkid.c mount_guess_fstype.c \
	getusername.c \
	nfsmount.c nfsmount_xdr.c nfsmount_clnt.c \
	lomount.c \
	$(MNTHDRS)

mount_LDADD = $(top_srcdir)/lib/libenv.a $(top_srcdir)/lib/libsetproctitle.a

umount_SOURCES = umount.c fstab.c sundries.c xmalloc.c realpath.c mount_mntent.c \
	getusername.c get_label_uuid.c mount_by_label.c mount_blkid.c \
	lomount.c \
	$(MNTHDRS)
	
umount_LDADD = $(top_srcdir)/lib/libenv.a

swapon_SOURCES = swapon.c xmalloc.c \
	get_label_uuid.c mount_by_label.c mount_blkid.c \
	swap_constants.h realpath.c

losetup_SOURCES = lomount.c loop.h lomount.h
losetup_CFLAGS = -DMAIN

if HAVE_BLKID
mount_LDADD += -lblkid -luuid
umount_LDADD += -lblkid -luuid
swapon_LDADD = -lblkid -luuid
endif

if HAVE_PIVOT_ROOT
sbin_PROGRAMS += pivot_root
man_MANS += pivot_root.8
endif


swapon.c: swapargs.h

swapargs.h:
	sh swap.configure

clean-local:
	rm -f swapargs.h


install-exec-hook:
	chmod 4755 $(DESTDIR)$(bindir)/mount
	chmod 4755 $(DESTDIR)$(bindir)/umount
	cd $(DESTDIR)$(sbindir) && ln -sf swapon swapoff
