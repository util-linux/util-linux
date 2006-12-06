include $(top_srcdir)/config/include-Makefile.am

man_MANS = blockdev.8 fdformat.8 isosize.8 mkfs.8 mkswap.8 \
           fsck.minix.8 mkfs.minix.8 mkfs.bfs.8

sbin_PROGRAMS = mkfs mkswap blockdev fsck.minix mkfs.minix mkfs.bfs
fsck_minix_SOURCES = fsck.minix.c
mkfs_minix_SOURCES = mkfs.minix.c
mkfs_bfs_SOURCES = mkfs.bfs.c

usrbinexec_PROGRAMS = fdformat isosize

if BUILD_ELVTUNE
sbin_PROGRAMS += elvtune
man_MANS += elvtune.8
endif

if HAVE_RAW
if BUILD_RAW
usrbinexec_PROGRAMS += raw
man_MANS += raw.8
endif
endif

if BUILD_CRAMFS
sbin_PROGRAMS += fsck.cramfs mkfs.cramfs
fsck_cramfs_SOURCES = fsck.cramfs.c
mkfs_cramfs_SOURCES = mkfs.cramfs.c
fsck_cramfs_LDADD = -lz
mkfs_cramfs_LDADD = -lz $(top_srcdir)/lib/libmd5.a
endif

if HAVE_UUID
mkswap_LDADD = -luuid
endif
