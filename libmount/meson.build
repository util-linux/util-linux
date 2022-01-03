dir_libmount = include_directories('.', 'src')

defs = configuration_data()
defs.set('LIBMOUNT_VERSION', pc_version)
defs.set('LIBMOUNT_MAJOR_VERSION', pc_version.split('.')[0])
defs.set('LIBMOUNT_MINOR_VERSION', pc_version.split('.')[1])
defs.set('LIBMOUNT_PATCH_VERSION', pc_version.split('.')[2])

libmount_h = configure_file(
  input : 'src/libmount.h.in',
  output : 'libmount.h',
  configuration : defs,
  install : build_libmount,
  install_dir : join_paths(get_option('includedir'), 'libmount'),
)

lib_mount_sources = '''
  src/mountP.h
  src/cache.c
  src/fs.c
  src/init.c
  src/iter.c
  src/lock.c
  src/optmap.c
  src/optstr.c
  src/tab.c
  src/tab_diff.c
  src/tab_parse.c
  src/tab_update.c
  src/test.c
  src/utils.c
  src/version.c
'''.split() + [
  list_h,
  monotonic_c,
]

if LINUX
  lib_mount_sources += '''
    src/context.c
    src/context_loopdev.c
    src/context_veritydev.c
    src/context_mount.c
    src/context_umount.c
    src/monitor.c
'''.split()
endif

if enable_btrfs
  lib_mount_sources += 'src/btrfs.c'
endif

libmount_sym = 'src/libmount.sym'
libmount_sym_path = '@0@/@1@'.format(meson.current_source_dir(), libmount_sym)

if build_libmount and not have_dirfd and not have_ddfd
  error('neither dirfd nor ddfd are available')
endif

lib__mount = static_library(
  '_mount',
  lib_mount_sources,
  include_directories : [dir_include,
                         dir_libmount,
                         dir_libblkid])

lib_mount_static = static_library(
  'mount',
  link_whole : lib__mount,
  link_with : [lib_common,
               lib_blkid.get_static_lib()],
  dependencies : [realtime_libs],
  install : false)

lib_mount = library(
  'mount',
  link_whole : lib__mount,
  include_directories : [dir_include,
                         dir_libmount,
                         dir_libblkid],
  link_depends : libmount_sym,
  version : libmount_version,
  link_args : ['-Wl,--version-script=@0@'.format(libmount_sym_path)],
  link_with : [lib_common,
               lib_blkid],
  dependencies : [lib_selinux,
                  get_option('cryptsetup-dlopen').enabled() ? lib_dl : lib_cryptsetup,
                  realtime_libs],
  install : build_libmount)

if build_libmount
  pkgconfig.generate(lib_mount,
                     description : 'mount library',
                     subdirs : 'libmount',
                     version : pc_version)
endif

subdir('python')
