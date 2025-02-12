dir_sys_utils = include_directories('.')

hwclock_parse_date = bison_gen.process('hwclock-parse-date.y')

lsmem_sources = files(
  'lsmem.c',
)
lsmem_manadocs = files('lsmem.1.adoc')

chmem_sources = files(
  'chmem.c',
)
chmem_manadocs = files('chmem.8.adoc')

choom_sources = files(
  'choom.c',
)
choom_manadocs = files('choom.1.adoc')

ipcmk_sources = files(
  'ipcmk.c',
)
ipcmk_manadocs = files('ipcmk.1.adoc')

ipcrm_sources = files(
  'ipcrm.c',
  'ipcutils.c',
  'ipcutils.h',
)
ipcrm_manadocs = files('ipcrm.1.adoc')

ipcs_sources = files(
  'ipcs.c',
  'ipcutils.c',
  'ipcutils.h',
)
ipcs_manadocs = files('ipcs.1.adoc')

rfkill_sources = files(
  'rfkill.c',
)
rfkill_manadocs = files('rfkill.8.adoc')

renice_sources = files(
  'renice.c',
)
renice_manadocs = files('renice.1.adoc')

setpgid_sources = files(
  'setpgid.c',
)
setpgid_manadocs = files('setpgid.1.adoc')

setsid_sources = files(
  'setsid.c',
)
setsid_manadocs = files('setsid.1.adoc')

readprofile_sources = files(
  'readprofile.c',
)
readprofile_manadocs = files('readprofile.8.adoc')

tunelp_sources = files(
  'tunelp.c',
)
tunelp_manadocs = files('tunelp.8.adoc')

fstrim_sources = files(
  'fstrim.c',
)
fstrim_manadocs = files('fstrim.8.adoc')

dmesg_sources = files(
  'dmesg.c',
) + \
  monotonic_c + pager_c
dmesg_manadocs = files('dmesg.1.adoc')

ctrlaltdel_sources = files(
  'ctrlaltdel.c',
)
ctrlaltdel_manadocs = files('ctrlaltdel.8.adoc')

fsfreeze_sources = files(
  'fsfreeze.c',
)
fsfreeze_manadocs = files('fsfreeze.8.adoc')

blkdiscard_sources = files(
  'blkdiscard.c',
) + \
  monotonic_c
blkdiscard_manadocs = files('blkdiscard.8.adoc')

blkzone_sources = files(
  'blkzone.c',
)
blkzone_manadocs = files('blkzone.8.adoc')

blkpr_sources = files(
  'blkpr.c',
)
blkpr_manadocs = files('blkpr.8.adoc')

ldattach_sources = files(
  'ldattach.c',
)
ldattach_manadocs = files('ldattach.8.adoc')

rtcwake_sources = files(
  'rtcwake.c',
)
rtcwake_manadocs = files('rtcwake.8.adoc')

setarch_sources = files(
  'setarch.c',
)
setarch_manadocs = files('setarch.8.adoc')

eject_sources = files(
  'eject.c',
) + \
  monotonic_c
eject_manadocs = files('eject.1.adoc')

losetup_sources = files(
  'losetup.c',
)
losetup_manadocs = files('losetup.8.adoc')

zramctl_sources = files(
  'zramctl.c',
) + \
  ismounted_c
zramctl_manadocs = files('zramctl.8.adoc')

prlimit_sources = files(
  'prlimit.c',
)
prlimit_manadocs = files('prlimit.1.adoc')

lsns_sources = files(
  'lsns.c',
)
lsns_manadocs = files('lsns.8.adoc')

fstab_manadocs = files('fstab.5.adoc')

mount_sources = files(
  'mount.c',
)
mount_manadocs = files(
  'mount.8.adoc'
)

umount_sources = files(
  'umount.c',
)
umount_manadocs = files(
  'umount.8.adoc'
)
swapon_sources = files(
  'swapon.c',
  'swapon-common.c',
  'swapon-common.h',
) + \
  swapprober_c
swapon_manadocs = files('swapon.8.adoc')

swapoff_sources = files(
  'swapoff.c',
  'swapon-common.c',
  'swapon-common.h',
) + \
  swapprober_c

lscpu_sources = files(
  'lscpu.c',
  'lscpu.h',
  'lscpu-cputype.c',
  'lscpu-cpu.c',
  'lscpu-topology.c',
  'lscpu-virt.c',
  'lscpu-arm.c',
  'lscpu-dmi.c',
)
lscpu_manadocs = files('lscpu.1.adoc')

chcpu_sources = files(
  'chcpu.c',
)
chcpu_manadocs = files('chcpu.8.adoc')

wdctl_sources = files(
  'wdctl.c',
)
wdctl_manadocs = files('wdctl.8.adoc')

mountpoint_sources = files(
  'mountpoint.c',
)
mountpoint_manadocs = files('mountpoint.1.adoc')

fallocate_sources = files(
  'fallocate.c',
)
fallocate_manadocs = files('fallocate.1.adoc')

pivot_root_sources = files(
  'pivot_root.c',
)
pivot_root_manadocs = files('pivot_root.8.adoc')

switch_root_sources = files(
  'switch_root.c',
)
switch_root_manadocs = files('switch_root.8.adoc')

unshare_sources = files(
  'unshare.c',
) + \
    exec_shell_c
unshare_manadocs = files('unshare.1.adoc')

nsenter_sources = files(
  'nsenter.c',
) + \
    exec_shell_c
nsenter_manadocs = files('nsenter.1.adoc')

setpriv_sources = files(
  'setpriv.c',
)
if LINUX and conf.get('HAVE_LINUX_LANDLOCK_H').to_string() == '1'
  setpriv_sources += files('setpriv-landlock.c')
endif
setpriv_manadocs = files('setpriv.1.adoc')

flock_sources = files(
  'flock.c',
) + \
  monotonic_c + \
  timer_c
flock_manadocs = files('flock.1.adoc')

lsipc_sources = files(
  'lsipc.c',
  'ipcutils.c',
  'ipcutils.h',
)
lsipc_manadocs = files('lsipc.1.adoc')

lsirq_sources = files(
  'lsirq.c',
  'irq-common.c',
  'irq-common.h',
)
lsirq_manadocs = files('lsirq.1.adoc')

irqtop_sources = files(
  'irqtop.c',
  'irq-common.c',
  'irq-common.h',
) + \
    monotonic_c
irqtop_manadocs = files('irqtop.1.adoc')

hwclock_sources = [
  'sys-utils/hwclock.c',
  'sys-utils/hwclock.h',
  hwclock_parse_date,
]
if use_hwclock_cmos
  hwclock_sources += [
    'sys-utils/hwclock-cmos.c',
  ]
endif
if LINUX
  hwclock_sources += [
    'sys-utils/hwclock-rtc.c',
    monotonic_c,
  ]
endif

hwclock_manadocs = files(
  'adjtime_config.5.adoc',
  'hwclock.8.adoc',
)

if systemd.found()
  fstrim_service = configure_file(
    input : 'fstrim.service.in',
    output : 'fstrim.service',
    configuration : conf)

  install_data(fstrim_service,
               install_dir : systemdsystemunitdir)
  install_data('fstrim.timer',
               install_dir : systemdsystemunitdir)
endif
