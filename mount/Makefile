# To make "ext" the default file system type for mount
#  (used when no other type is specified), replace \"minix\" by \"ext2\".
DEFAULT_FSTYPE=\"minix\"

# you need rpcgen and libc-4.2 or rpclib to compile in the NFS support
DEFINES = -DHAVE_NFS -DFSTYPE_DEFAULT=$(DEFAULT_FSTYPE)

include ../MCONFIG
#CC = gcc
#OPTFLAGS=	-O2 -m486 -fomit-frame-pointer
##OPTFLAGS=	-O2 -fomit-frame-pointer  # or change on make's command line
#CFLAGS = -pipe $(OPTFLAGS)
WARNFLAGS = -Wall -Wstrict-prototypes -Wmissing-prototypes
#LDFLAGS = -s -N
LDLIBS =
RPCSVCDIR = rpcsvc
RPC_CFLAGS = -Wno-unused 
RPCGEN = rpcgen
#INSTALL = install
#INSTALL_SUID = $(INSTALL) -m 4755 -o root
#INSTALL_PROG = $(INSTALL) -m 755
#INSTALL_DATA = $(INSTALL) -m 644
#prefix = /usr

## for suid progs (mount, umount)
#BINDIR = /bin
## for nosuid progs (swapon)
#SBINDIR = /etc

# End of configuration section.

COMPILE = $(CC) -c $(WARNFLAGS) $(CFLAGS) $(DEFINES)
LINK = $(CC) $(LDFLAGS)

SUID_PROGS = mount umount
NOSUID_PROGS = swapon
PROGS = $(SUID_PROGS) $(NOSUID_PROGS)
MAN5 = fstab.5 nfs.5
MAN8 = mount.8 swapoff.8 swapon.8 umount.8

# comment these out if you are not compiling in NFS support
NFS_OBJS = nfsmount.o mount_xdr.o mount_clnt.o
# uncomment this if you don't have libc-4.2 but do have the rpclib
GEN_FILES = mount.x mount.h mount_xdr.c mount_clnt.c

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

.c.o:
	$(COMPILE) $<

mount: mount.o fstab.o sundries.o version.o $(NFS_OBJS) $(LO_OBJS)
	$(LINK) $^ $(LDLIBS) -o $@

umount: umount.o fstab.o sundries.o version.o $(LO_OBJS)
	$(LINK) $^ $(LDLIBS) -o $@

swapon:	swapon.o fstab.o version.o
	$(LINK) $^ $(LDLIBS) -o $@

nfsmount.o mount_xdr.o mount_clnt.o: mount.h

mount_clnt.o: mount_clnt.c
	$(COMPILE) $(RPC_CFLAGS) mount_clnt.c

mount_xdr.o: mount_xdr.c
	$(COMPILE) $(RPC_CFLAGS) mount_xdr.c

mount.h mount_xdr.c mount_clnt.c: mount.x
	rm -f mount.h mount_xdr.c mount_clnt.c
	$(RPCGEN) -h -o mount.h mount.x
	$(RPCGEN) -c -o mount_xdr.c mount.x
	$(RPCGEN) -l -o mount_clnt.c mount.x

mount.x:
	cp $(RPCSVCDIR)/mount.x .

clean:
	rm -f a.out core *~ *.o $(PROGS)

clobber: clean
	rm -f $(PROGS) $(GEN_FILES)
