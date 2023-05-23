dir_libmount = include_directories('.', 'src')

defs = configuration_data()
defs.set('LIBMOUNT_VERSION', pc_version)
defs.set('LIBMOUNT_MAJOR_VERSION', pc_version.split('.')[0])
defs.set('LIBMOUNT_MINOR_VERSION', pc_version.split('.')[1])
defs.set('LIBMOUNT_PATCH_VERSION', pc_version.split('.')[2])

configure_file(
  input : 'src/libmount.h.in',
  output : 'libmount.h',
  configuration : defs,
  install : build_libmount,
  install_dir : join_paths(get_option('includedir'), 'libmount'),
)

lib_mount_sources = '''
  src/mountP.h
  src/cache.c
  src/hooks.c
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
    src/hooks.c
    src/monitor.c
    src/optlist.c
    src/hook_veritydev.c
    src/hook_subdir.c
    src/hook_owner.c
    src/hook_mount.c
    src/hook_mount_legacy.c
    src/hook_mkdir.c
    src/hook_selinux.c
    src/hook_loopdev.c
    src/hook_idmap.c
    src/context_umount.c
    src/context_mount.c
    src/context.c
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
  'mount_static',
  link_whole : lib__mount,
  link_with : [lib_common,
               lib_blkid.get_static_lib()],
  dependencies : [realtime_libs],
  install : false)

lib__mount_deps = [
  lib_selinux,
  get_option('cryptsetup-dlopen').enabled() ? lib_dl : lib_cryptsetup,
  realtime_libs
]
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
  dependencies : lib__mount_deps,
  install : build_libmount)
mount_dep = declare_dependency(link_with: lib_mount, include_directories: '.')

if build_libmount
  pkgconfig.generate(lib_mount,
                     description : 'mount library',
                     subdirs : 'libmount',
                     version : pc_version)
  if meson.version().version_compare('>=0.54.0')
    meson.override_dependency('mount', mount_dep)
  endif
endif

libmount_tests = [
  'cache',
  'context',
  'lock',
  'optstr',
  'tab',
  'tab_diff',
  'monitor',
  'tab_update',
  'utils',
  'version',
  'debug',
]

libmount_test_src_override = {
  'debug': 'init',
}

foreach libmount_test: libmount_tests
  test_name = 'test_mount_' + libmount_test
  exe = executable(
    test_name,
    'src/' + libmount_test_src_override.get(libmount_test, libmount_test) + '.c',
    include_directories : [dir_include, dir_libblkid],
    link_with : [lib__mount, lib_common, lib_blkid_static],
    dependencies : lib__mount_deps,
    c_args : ['-DTEST_PROGRAM'],
  )
  # the test-setup expects the helpers in the toplevel build-directory
  link = meson.project_build_root() / test_name
  run_command('ln', '-srf', exe.full_path(), link,
    check : true)
endforeach

subdir('python')
