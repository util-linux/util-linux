agetty_sources = files(
  'agetty.c',
  'agetty.h',
  'utils.c',
)

if LINUX
  agetty_sources += netlink_files_c
endif

agetty_manadocs = files('../term-utils/agetty.8.adoc')
