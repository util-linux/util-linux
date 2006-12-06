include $(top_srcdir)/config/include-Makefile.am

AM_CPPFLAGS += -DHAVE_NFS

bin_PROGRAMS = mount umount
sbin_PROGRAMS = losetup swapon
man_MANS = fstab.5 nfs.5 mount.8 swapoff.8 swapon.8 umount.8 losetup.8

mount_SOURCES = mount.c fstab.c sundries.c xmalloc.c realpath.c mntent.c \
	get_label_uuid.c mount_by_label.c mount_blkid.c mount_guess_fstype.c \
	getusername.c \
	nfsmount.c nfsmount_xdr.c nfsmount_clnt.c \
	lomount.c

mount_LDADD = $(top_srcdir)/lib/libenv.a $(top_srcdir)/lib/libsetproctitle.a

umount_SOURCES = umount.c fstab.c sundries.c xmalloc.c realpath.c mntent.c \
	getusername.c get_label_uuid.c mount_by_label.c mount_blkid.c \
	lomount.c

umount_LDADD = $(top_srcdir)/lib/libenv.a

swapon_SOURCES = swapon.c xmalloc.c \
	get_label_uuid.c mount_by_label.c mount_blkid.c

losetup_SOURCES = lomount.c
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
