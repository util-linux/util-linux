include $(top_srcdir)/config/include-Makefile.am

utils_common = ../lib/linux_version.c ../lib/blkdev.c

dist_man_MANS = blockdev.8 isosize.8 mkfs.8 mkswap.8 \
           fsck.minix.8 mkfs.minix.8 mkfs.bfs.8

sbin_PROGRAMS = mkfs mkswap blockdev fsck.minix mkfs.minix mkfs.bfs
fsck_minix_SOURCES = fsck.minix.c bitops.h minix.h
mkfs_minix_SOURCES = mkfs.minix.c bitops.h minix.h $(utils_common)
mkfs_bfs_SOURCES = mkfs.bfs.c
mkswap_SOURCES = mkswap.c swapheader.h $(utils_common)
blockdev_SOURCES = blockdev.c $(utils_common)

usrbinexec_PROGRAMS = isosize
usrsbinexec_PROGRAMS =

if LINUX
dist_man_MANS += fdformat.8
usrsbinexec_PROGRAMS += fdformat
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
sbin_PROGRAMS += fsck.cramfs mkfs.cramfs
fsck_cramfs_SOURCES = fsck.cramfs.c cramfs.h
mkfs_cramfs_SOURCES = mkfs.cramfs.c cramfs.h ../lib/md5.c
fsck_cramfs_LDADD = -lz
mkfs_cramfs_LDADD = -lz
endif

mkswap_LDADD =

if HAVE_UUID
mkswap_LDADD += -luuid
endif

if HAVE_SELINUX
mkswap_LDADD += -lselinux
endif

