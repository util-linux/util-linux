cal_sources = files(
  'cal.c',
)

logger_sources = files(
  'logger.c',
) + \
  strutils_c + \
  strv_c

look_sources = files(
  'look.c',
)

mcookie_sources = files(
  'mcookie.c',
) + \
  md5_c

namei_sources = files(
  'namei.c',
) + \
  strutils_c + \
  idcache_c

whereis_sources = files(
  'whereis.c',
)

lslocks_sources = files(
  'lslocks.c',
)

lsblk_sources = files(
  'lsblk.c',
  'lsblk-mnt.c',
  'lsblk-properties.c',
  'lsblk-devtree.c',
  'lsblk.h',
)

lsfd_sources = files (
  'lsfd.c',
  'lsfd.h',
  'lsfd-filter.h',
  'lsfd-filter.c',
  'lsfd-counter.h',
  'lsfd-counter.c',
  'lsfd-file.c',
  'lsfd-cdev.c',
  'lsfd-bdev.c',
  'lsfd-sock.c',
  'lsfd-unkn.c',
  'lsfd-fifo.c',
)

uuidgen_sources = files(
  'uuidgen.c',
)

uuidparse_sources = files(
  'uuidparse.c',
)

uuidd_sources = files(
  'uuidd.c',
) + \
  monotonic_c + \
  timer_c

test_uuidd_sources = files(
  'test_uuidd.c',
)

if build_uuidd and systemd.found()
  uuidd_service = configure_file(
    input : 'uuidd.service.in',
    output : 'uuidd.service',
    configuration : conf)
  install_data(
    uuidd_service,
    install_dir : systemdsystemunitdir)

  uuidd_socket = configure_file(
    input : 'uuidd.socket.in',
    output : 'uuidd.socket',
    configuration : conf)
  install_data(
    uuidd_socket,
    install_dir : systemdsystemunitdir)
endif

blkid_sources = files(
  'blkid.c',
) + \
  ismounted_c

findfs_sources = files(
  'findfs.c',
)

wipefs_sources = files(
  'wipefs.c',
)

findmnt_sources = files(
  'findmnt.c',
  'findmnt-verify.c',
  'findmnt.h',
)

kill_sources = files(
  'kill.c',
)

rename_sources = files(
  'rename.c',
)

getopt_sources = files(
  'getopt.c',
)

install_data(
  'getopt-example.bash',
  'getopt-example.tcsh',
  install_dir : docdir,
  install_mode: 'rwxr-xr-x')

fincore_sources = files(
  'fincore.c',
)

hardlink_sources = files(
  'hardlink.c',
) + \
  monotonic_c + \
  fileeq_c

cal_sources = files(
  'cal.c',
)
