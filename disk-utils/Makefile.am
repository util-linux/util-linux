include $(top_srcdir)/config/include-Makefile.am

utils_common = ../lib/blkdev.c
if LINUX
utils_common += ../lib/linux_version.c
endif

dist_man_MANS = isosize.8 mkfs.8 mkswap.8 \
           fsck.minix.8 mkfs.minix.8 mkfs.bfs.8

sbin_PROGRAMS = mkfs mkswap fsck.minix mkfs.minix mkfs.bfs
fsck_minix_SOURCES = fsck.minix.c minix.h
mkfs_minix_SOURCES = mkfs.minix.c minix.h $(utils_common)
mkfs_bfs_SOURCES = mkfs.bfs.c $(utils_common)

mkswap_SOURCES = mkswap.c $(utils_common) ../lib/wholedisk.c
mkswap_LDADD =
mkswap_CFLAGS = $(AM_CFLAGS)

usrbin_exec_PROGRAMS = isosize
usrsbin_exec_PROGRAMS =

if LINUX
dist_man_MANS += fdformat.8 blockdev.8
sbin_PROGRAMS += blockdev
usrsbin_exec_PROGRAMS += fdformat
blockdev_SOURCES = blockdev.c $(utils_common)
endif

if BUILD_ELVTUNE
sbin_PROGRAMS += elvtune
dist_man_MANS += elvtune.8
elvtune_SOURCES = elvtune.c ../lib/linux_version.c
endif

if HAVE_RAW
if BUILD_RAW
sbin_PROGRAMS += raw
dist_man_MANS += raw.8
endif
endif

if BUILD_CRAMFS
cramfs_common = $(utils_common) cramfs.h cramfs_common.c cramfs_common.h
sbin_PROGRAMS += fsck.cramfs mkfs.cramfs
fsck_cramfs_SOURCES = fsck.cramfs.c $(cramfs_common)
mkfs_cramfs_SOURCES = mkfs.cramfs.c $(cramfs_common) ../lib/md5.c
fsck_cramfs_LDADD = -lz
mkfs_cramfs_LDADD = -lz
endif

if HAVE_UUID
if BUILD_LIBUUID
mkswap_LDADD += $(ul_libuuid_la)
mkswap_CFLAGS += -I$(ul_libuuid_srcdir)
else
mkswap_LDADD += $(UUID_LIBS)
mkswap_CFLAGS += $(UUID_CFLAGS)
endif
endif

if BUILD_LIBBLKID
# only in-tree libblkid has partitions parsing support
mkswap_LDADD += $(ul_libblkid_la)
mkswap_CFLAGS += -I$(ul_libblkid_incdir)
endif

if HAVE_SELINUX
mkswap_LDADD += -lselinux
endif

