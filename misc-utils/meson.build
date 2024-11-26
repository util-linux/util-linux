blkid_manadocs = files('blkid.8.adoc')

cal_sources = files(
  'cal.c',
)
cal_manadocs = files('cal.1.adoc')

enosys_manadocs = files('enosys.1.adoc')

logger_sources = files(
  'logger.c',
) + \
  strutils_c + \
  strv_c
logger_manadocs = files('logger.1.adoc')

look_sources = files(
  'look.c',
)
look_manadocs = files('look.1.adoc')

lastlog2_sources = files(
  'lastlog2.c',
) + \
  strutils_c
lastlog2_manadocs = files('lastlog2.8.adoc')

mcookie_sources = files(
  'mcookie.c',
) + \
  md5_c
mcookie_manadocs = files('mcookie.1.adoc')

namei_sources = files(
  'namei.c',
) + \
  strutils_c + \
  idcache_c
namei_manadocs = files('namei.1.adoc')

whereis_sources = files(
  'whereis.c',
)
whereis_manadocs = files('whereis.1.adoc')

lslocks_sources = files(
  'lslocks.c',
)
lslocks_manadocs = files('lslocks.8.adoc')

lsblk_sources = files(
  'lsblk.c',
  'lsblk-mnt.c',
  'lsblk-properties.c',
  'lsblk-devtree.c',
  'lsblk.h',
)
lsblk_manadocs = files('lsblk.8.adoc')

uuidgen_sources = files(
  'uuidgen.c',
)
uuidgen_manadocs = files('uuidgen.1.adoc')

uuidparse_sources = files(
  'uuidparse.c',
)
uuidparse_manadocs = files('uuidparse.1.adoc')

uuidd_sources = files(
  'uuidd.c',
) + \
  monotonic_c + \
  timer_c
uuidd_manadocs = files('uuidd.8.adoc')

test_uuidd_sources = files(
  'test_uuidd.c',
)

if build_liblastlog2 and systemd.found()
  lastlog2_tmpfiles = configure_file(
    input : 'lastlog2-tmpfiles.conf.in',
    output : 'lastlog2-tmpfiles.conf',
    configuration : conf)
  install_data(
    lastlog2_tmpfiles,
    install_dir : systemd_tmpfiles_dir,
    rename : 'lastlog2.conf')

  lastlog2_service = configure_file(
    input : 'lastlog2-import.service.in',
    output : 'lastlog2-import.service',
    configuration : conf)
  install_data(
    lastlog2_service,
    install_dir : systemdsystemunitdir)
endif

if build_uuidd and systemd.found()
  uuidd_tmpfiles = configure_file(
    input : 'uuidd-tmpfiles.conf.in',
    output : 'uuidd-tmpfiles.conf',
    configuration : conf)
  install_data(
    uuidd_tmpfiles,
    install_dir : systemd_tmpfiles_dir,
    rename : 'uuidd.conf')

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

  uuidd_sysusers = configure_file(
    input : 'uuidd-sysusers.conf.in',
    output : 'uuidd-sysusers.conf',
    configuration : conf)
  install_data(
    uuidd_sysusers,
    install_dir: systemd_sysusers_dir,
    rename : 'uuidd.conf')
endif

if build_uuidd and sysvinit
  uuidd_rc = configure_file(
    input : 'uuidd.rc.in',
    output : 'uuidd.rc',
    configuration : conf)
  install_data(
    uuidd_rc,
    install_mode : 'rwxr-xr-x',
    install_dir : sysvinitrcdir)
endif

blkid_sources = files(
  'blkid.c',
) + \
  ismounted_c

findfs_sources = files(
  'findfs.c',
)
findfs_manadocs = files('findfs.8.adoc')

wipefs_sources = files(
  'wipefs.c',
)
wipefs_manadocs = files('wipefs.8.adoc')

findmnt_sources = files(
  'findmnt.c',
  'findmnt-verify.c',
  'findmnt.h',
)
findmnt_manadocs = files('findmnt.8.adoc')

kill_sources = files(
  'kill.c',
)
kill_manadocs = files('kill.1.adoc')

rename_sources = files(
  'rename.c',
)
rename_manadocs = files('rename.1.adoc')

getopt_sources = files(
  'getopt.c',
)
getopt_manadocs = files('getopt.1.adoc')

if not get_option('build-getopt').disabled()
  install_data(
    'getopt-example.bash',
    'getopt-example.tcsh',
    install_dir : docdir,
    install_mode: 'rwxr-xr-x')
endif

exch_sources = files(
  'exch.c',
)
exch_manadocs = files('exch.1.adoc')

fincore_sources = files(
  'fincore.c',
)
fincore_manadocs = files('fincore.1.adoc')

hardlink_sources = files(
  'hardlink.c',
) + \
  monotonic_c + \
  fileeq_c
hardlink_manadocs = files('hardlink.1.adoc')

cal_sources = files(
  'cal.c',
)

pipesz_sources = files(
  'pipesz.c',
)
pipesz_manadocs = files('pipesz.1.adoc')

fadvise_sources = files(
  'fadvise.c',
)
fadvise_manadocs = files('fadvise.1.adoc')

waitpid_sources = files(
  'waitpid.c',
)
waitpid_manadocs = files('waitpid.1.adoc')

lsclocks_sources = files(
  'lsclocks.c',
)
lsclocks_manadocs = files('lsclocks.1.adoc')
