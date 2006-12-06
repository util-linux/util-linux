ifeq (../MCONFIG,$(wildcard ../MCONFIG))
include ../MCONFIG
endif

CC = gcc
CFLAGS = -O2
WARNFLAGS = -Wall -Wstrict-prototypes -Wmissing-prototypes
DEFINES = -DHAVE_NFS
LDFLAGS =

RPCSVCDIR = rpcsvc
RPC_CFLAGS = -Wno-unused 
RPCGEN = rpcgen
#INSTALL = install
#INSTALL_SUID = $(INSTALL) -m 4755 -o root
#INSTALL_PROG = $(INSTALL) -m 755
#INSTALL_DATA = $(INSTALL) -m 644

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

mount: mount.o fstab.o sundries.o realpath.o version.o $(NFS_OBJS) $(LO_OBJS)
	$(LINK) $^ $(LDLIBS) -o $@

umount: umount.o fstab.o sundries.o realpath.o version.o $(LO_OBJS)
	$(LINK) $^ $(LDLIBS) -o $@

swapon:	swapon.o version.o
	$(LINK) $^ $(LDLIBS) -o $@

losetup: losetup.o
	$(LINK) $^ $(LDLIBS) -o $@

mount.o: linux_fs.h

mount.o umount.o nfsmount.o losetup.o fstab.o sundries.o: sundries.h

mount.o umount.o fstab.o sundries.o: fstab.h

mount.o umount.o losetup.o lomount.o: lomount.h loop.h

swapon.o: swap.h swapargs.h

sundries.o nfsmount.o nfsmount_xdr.o nfsmount_clnt.o: nfsmount.h

umount.o: mount_constants.h

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

SWAPH=/usr/include/linux/swap.h
swap.h:
	rm -f swap.h
	if [ -f $(SWAPH) ]; then cp $(SWAPH) .; else touch swap.h; fi

swapargs.h:
	sh swap.configure

loop.h:
	sh mk_loop_h

clean:
	rm -f a.out core *~ *.o swapargs.h $(PROGS) swapoff
	rm -f swap.h loop.h nfs_mountversion.h

clobber distclean realclean: clean
#	rm -f $(GEN_FILES)
