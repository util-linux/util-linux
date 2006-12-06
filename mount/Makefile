include ../make_include
include ../MCONFIG

DEFINES = -DHAVE_NFS $(BLKID_DEFINE)

RPCSVCDIR = rpcsvc
RPC_CFLAGS = -Wno-unused 
RPCGEN = rpcgen

COMPILE = $(CC) -c $(CFLAGS) $(DEFINES)
LINK = $(CC) $(LDFLAGS)

SUID_PROGS = mount umount
NOSUID_PROGS = swapon losetup
MAN5 = fstab.5 nfs.5
MAN8 = mount.8 swapoff.8 swapon.8 umount.8 losetup.8

ifeq "$(HAVE_PIVOT_ROOT)" "yes"
NOSUID_PROGS := $(NOSUID_PROGS) pivot_root
MAN8 := $(MAN8) pivot_root.8
endif

ifeq "$(HAVE_BLKID)" "yes"
BLKID_DEFINE = -DHAVE_BLKID
BLKID_LIB = -lblkid -luuid
endif

PROGS = $(SUID_PROGS) $(NOSUID_PROGS)

MAYBE = pivot_root swapoff

LO_OBJS = lomount.o $(LIB)/xstrncpy.o
NFS_OBJS = nfsmount.o nfsmount_xdr.o nfsmount_clnt.o
GEN_FILES = nfsmount.h nfsmount_xdr.c nfsmount_clnt.c

all: $(PROGS)

install: $(PROGS)
	$(INSTALLDIR) $(BINDIR) $(SBINDIR)
	$(INSTALLSUID) $(SUID_PROGS) $(BINDIR)
	$(INSTALLBIN) $(NOSUID_PROGS) $(SBINDIR)
	(cd $(SBINDIR); ln -sf swapon swapoff)
	$(INSTALLDIR) $(MAN5DIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN5) $(MAN5DIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)

%.o: %.c
	$(COMPILE) $<

mount: mount.o fstab.o sundries.o xmalloc.o realpath.o mntent.o version.o \
       get_label_uuid.o mount_by_label.o mount_blkid.o mount_guess_fstype.o \
       getusername.o $(LIB)/setproctitle.o $(LIB)/env.o $(NFS_OBJS) $(LO_OBJS)
	$(LINK) $^ -o $@ $(BLKID_LIB)

umount: umount.o fstab.o sundries.o xmalloc.o realpath.o mntent.o \
	getusername.o get_label_uuid.o mount_by_label.o mount_blkid.o \
	version.o $(LIB)/env.o $(LO_OBJS)
	$(LINK) $^ -o $@ $(BLKID_LIB)

swapon:	swapon.o version.o xmalloc.o \
	get_label_uuid.o mount_by_label.o mount_blkid.o
	$(LINK) $^ -o $@ $(BLKID_LIB)

main_losetup.o: lomount.c
	$(COMPILE) -DMAIN lomount.c -o $@

losetup: main_losetup.o $(LIB)/xstrncpy.o
	$(LINK) $^ -o $@

mount.o umount.o nfsmount.o losetup.o fstab.o realpath.o sundries.o: sundries.h

mount.o umount.o fstab.o sundries.o: fstab.h

mount.o fstab.o mntent.o: mntent.h

mount.o mount_guess_fstype.o: mount_guess_fstype.h

mount.o: $(LIB)/setproctitle.h

mount.o umount.o getusername.o: getusername.h

mount.o umount.o losetup.o lomount.o: lomount.h loop.h my_dev_t.h

swapon.o: swap_constants.h swapargs.h

sundries.o nfsmount.o nfsmount_xdr.o nfsmount_clnt.o: nfsmount.h

umount.o: mount_constants.h

mount.o mount_blkid.o: mount_blkid.h

mount.o mount_by_label.o mount_guess_fstype.o: linux_fs.h

sundries.o realpath.o: realpath.h

nfsmount_clnt.o: nfsmount_clnt.c
	$(COMPILE) $(RPC_CFLAGS) nfsmount_clnt.c

nfsmount_xdr.o: nfsmount_xdr.c
	$(COMPILE) $(RPC_CFLAGS) nfsmount_xdr.c

ifeq "$(HAVE_GOOD_RPC)" "yes"
nfsmount.h: nfsmount.x
	rm -f nfsmount.h
	$(RPCGEN) -h -o nfsmount.h nfsmount.x

nfsmount_xdr.c: nfsmount.x
	rm -f nfsmount_xdr.c
	$(RPCGEN) -c -o nfsmount_xdr.c nfsmount.x

nfsmount_clnt.c: nfsmount.x
	rm -f nfsmount_clnt.c
	$(RPCGEN) -l -o nfsmount_clnt.c nfsmount.x
else
nfsmount.h:
	cp $(RPCSVCDIR)/nfsmount.h .

nfsmount_xdr.c:
	cp $(RPCSVCDIR)/nfsmount_xdr.c .

nfsmount_clnt.c:
	cp $(RPCSVCDIR)/nfsmount_clnt.c .
endif

nfsmount.o: nfs_mount4.h

swapargs.h:
	sh swap.configure

clean:
	rm -f a.out core *~ *.o swapargs.h $(PROGS) $(MAYBE)
	rm -f nfs_mountversion.h

clobber distclean realclean: clean
	rm -f $(GEN_FILES)
