agetty_sources = files(
  'agetty.c',
  'agetty.h',
  'credentials.c',
  'tty.c',
  'utils.c',
)

if LINUX
  agetty_sources += netlink_files_c
endif

agetty_manadocs = files('../term-utils/agetty.8.adoc')
