mkfs_sources = files(
  'mkfs.c',
)
mkfs_manadocs = files('mkfs.8.adoc')

mkfs_bfs_sources = files(
  'mkfs.bfs.c',
)
mkfs_bfs_manadocs = files('mkfs.bfs.8.adoc')

isosize_sources = files(
  'isosize.c',
)
isosize_manadocs = files('isosize.8.adoc')

mkswap_sources = files(
  'mkswap.c',
) + \
  ismounted_c
if lib_selinux.found()
  mkswap_sources += selinux_utils_c
endif
mkswap_manadocs = files('mkswap.8.adoc')

swaplabel_sources = files(
  'swaplabel.c',
) + \
  swapprober_c
swaplabel_manadocs = files('swaplabel.8.adoc')

fsck_sources = files(
  'fsck.c',
) + \
  monotonic_c
fsck_manadocs = files('fsck.8.adoc')

mkfs_minix_sources = files(
  'mkfs.minix.c',
  'minix_programs.h',
) + \
  ismounted_c
mkfs_minix_manadocs = files('mkfs.minix.8.adoc')

fsck_minix_sources = files(
  'fsck.minix.c',
  'minix_programs.h',
) + \
  ismounted_c
fsck_minix_manadocs = files('fsck.minix.8.adoc')

mkfs_cramfs_sources = files(
  'mkfs.cramfs.c',
  'cramfs.h',
  'cramfs_common.c',
)

fsck_cramfs_sources = files(
  'fsck.cramfs.c',
  'cramfs.h',
  'cramfs_common.c',
)
fsck_cramfs_manadocs = files('fsck.cramfs.8.adoc')

raw_sources = files(
  'raw.c',
)
raw_manadocs = files('raw.8.adoc')

fdformat_sources = files(
  'fdformat.c',
)
fdformat_manadocs = files('fdformat.8.adoc')

blockdev_sources = files(
  'blockdev.c',
)
blockdev_manadocs = files('blockdev.8.adoc')

fdisk_sources = files(
  'fdisk.c',
  'fdisk.h',
  'fdisk-menu.c',
  'fdisk-list.c',
  'fdisk-list.h') + \
  pager_c
fdisk_manadocs = files('fdisk.8.adoc')

sfdisk_sources = files(
  'sfdisk.c',
  'fdisk-list.c',
  'fdisk-list.h')
sfdisk_manadocs = files('sfdisk.8.adoc')

cfdisk_sources = files(
  'cfdisk.c',
)
cfdisk_manadocs = files('cfdisk.8.adoc')

addpart_sources = files(
  'addpart.c',
)
addpart_manadocs = files('addpart.8.adoc')
delpart_sources = files(
  'delpart.c',
)
delpart_manadocs = files('delpart.8.adoc')
resizepart_sources = files(
  'resizepart.c',
)
resizepart_manadocs = files('resizepart.8.adoc')
partx_sources = files(
  'partx.c',
)
partx_manadocs = files('partx.8.adoc')
