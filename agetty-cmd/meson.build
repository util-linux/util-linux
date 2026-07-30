agetty_sources = files(
  'agetty.c',
  'agetty.h',
  'credentials.c',
  'issuefile.c',
  'issuefile-parser.c',
  'issuefile-parser.h',
  'tty.c',
  'utils.c',
)

if LINUX
  agetty_sources += netlink_files_c
endif

agetty_manadocs = files('agetty.8.adoc')
