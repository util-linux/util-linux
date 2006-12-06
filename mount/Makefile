ifeq (../MCONFIG,$(wildcard ../MCONFIG))
include ../MCONFIG
endif

CC = gcc
CFLAGS = -I$(LIB) $(OPT)
WARNFLAGS = -Wall -Wstrict-prototypes -Wmissing-prototypes
DEFINES = -DHAVE_NFS

RPCSVCDIR = rpcsvc
RPC_CFLAGS = -Wno-unused 
RPCGEN = rpcgen
#INSTALL = install
#INSTALLSUID = $(INSTALL) -m 4755 -o root
#INSTALLPROG = $(INSTALL) -m 755
#INSTALLDATA = $(INSTALL) -m 644

## for suid progs (mount, umount)
#BINDIR = /bin
## for nosuid progs (swapon)
#SBINDIR = /sbin

# End of configuration section.

COMPILE = $(CC) -c $(WARNFLAGS) $(CFLAGS) $(DEFINES)
LINK = $(CC) $(LDFLAGS)

SUID_PROGS = mount umount
NOSUID_PROGS = swapon losetup
PROGS = $(SUID_PROGS) $(NOSUID_PROGS)
MAN5 = fstab.5 nfs.5
MAN8 = mount.8 swapoff.8 swapon.8 umount.8 losetup.8

# comment these out if you are not compiling in NFS support
NFS_OBJS = nfsmount.o nfsmount_xdr.o nfsmount_clnt.o
# uncomment this if you don't have libc-4.2 but do have the rpclib
GEN_FILES = nfsmount.x nfsmount.h nfsmount_xdr.c nfsmount_clnt.c

# comment these out if you are not compiling in loop support
LO_OBJS=lomount.o

all: $(PROGS)

install: $(PROGS)
	$(INSTALLDIR) $(BINDIR) $(SBINDIR)
	$(INSTALLSUID) -s $(SUID_PROGS) $(BINDIR)
	$(INSTALLBIN) -s $(NOSUID_PROGS) $(SBINDIR)
	(cd $(SBINDIR); ln -sf swapon swapoff)
	$(INSTALLDIR) $(MAN5DIR) $(MAN8DIR)
	$(INSTALLMAN) $(MAN5) $(MAN5DIR)
	$(INSTALLMAN) $(MAN8) $(MAN8DIR)

%.o: %.c
	$(COMPILE) $<

mount: mount.o fstab.o sundries.o realpath.o mntent.o version.o \
       mount_guess_fstype.o mount_by_label.o getusername.o \
       $(LIB)/setproctitle.o $(NFS_OBJS) $(LO_OBJS)
	$(LINK) $^ -o $@

umount: umount.o fstab.o sundries.o realpath.o mntent.o getusername.o \
	version.o $(LO_OBJS)
	$(LINK) $^ -o $@

swapon:	swapon.o version.o
	$(LINK) $^ -o $@

losetup: losetup.o
	$(LINK) $^ -o $@

mount.o umount.o nfsmount.o losetup.o fstab.o sundries.o: sundries.h

mount.o umount.o fstab.o sundries.o: fstab.h

mount.o fstab.o mntent.o: mntent.h

mount.o mount_guess_fstype.o: mount_guess_fstype.h

mount.o: $(LIB)/setproctitle.h

mount.o umount.o getusername.o: getusername.h

mount.o umount.o losetup.o lomount.o: lomount.h loop.h

swapon.o: swap_constants.h swapargs.h

sundries.o nfsmount.o nfsmount_xdr.o nfsmount_clnt.o: nfsmount.h

umount.o: mount_constants.h

mount.o mount_by_label.o mount_guess_fstype.o: linux_fs.h

nfsmount_clnt.o: nfsmount_clnt.c
	$(COMPILE) $(RPC_CFLAGS) nfsmount_clnt.c

nfsmount_xdr.o: nfsmount_xdr.c
	$(COMPILE) $(RPC_CFLAGS) nfsmount_xdr.c

# rpcgen generates files that do not compile - use the pregenerated ones
# nfsmount.h nfsmount_xdr.c nfsmount_clnt.c: nfsmount.x
# 	rm -f nfsmount.h nfsmount_xdr.c nfsmount_clnt.c
# 	$(RPCGEN) -h -o nfsmount.h nfsmount.x
# 	$(RPCGEN) -c -o nfsmount_xdr.c nfsmount.x
# 	$(RPCGEN) -l -o nfsmount_clnt.c nfsmount.x

nfsmount.x:
	cp $(RPCSVCDIR)/nfsmount.x .

nfsmount.o: nfs_mountversion.h nfs_mount3.h

NFSMOUNTH=/usr/include/linux/nfs_mount.h

nfs_mountversion.h:
	rm -f nfs_mountversion.h
	if [ -f $(NFSMOUNTH) ]; then \
		grep NFS_MOUNT_VERSION $(NFSMOUNTH) \
			| sed -e 's/NFS/KERNEL_NFS/'; \
	else \
		echo '#define KERNEL_NFS_MOUNT_VERSION 0'; \
	fi > nfs_mountversion.h

swapargs.h:
	sh swap.configure

loop.h:
	sh mk_loop_h

clean:
	rm -f a.out core *~ *.o swapargs.h $(PROGS) swapoff
	rm -f loop.h nfs_mountversion.h

clobber distclean realclean: clean
#	rm -f $(GEN_FILES)
