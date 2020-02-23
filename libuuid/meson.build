dir_libuuid = include_directories('src')

lib_uuid_sources = '''
  src/uuidP.h
  src/clear.c
  src/compare.c
  src/copy.c
  src/gen_uuid.c
  src/isnull.c
  src/pack.c
  src/parse.c
  src/uuidd.h
  src/uuid_time.c
'''.split()

predefined_c = files('src/predefined.c')
unpack_c = files('src/unpack.c')
unparse_c = files('src/unparse.c')

libuuid_sym = 'src/libuuid.sym'
libuuid_sym_path = '@0@/@1@'.format(meson.current_source_dir(), libuuid_sym)

lib_uuid = both_libraries(
  'uuid',
  list_h,
  lib_uuid_sources,
  predefined_c,
  unpack_c,
  unparse_c,
  randutils_c,
  md5_c,
  sha1_c,
  include_directories : [dir_include, dir_libuuid],
  link_depends : libuuid_sym,
  version : libuuid_version,
  link_args : ['-Wl,--version-script=@0@'.format(libuuid_sym_path)],
  dependencies : [socket_libs,
                  build_libuuid ? [] : disabler()],
  install : build_libuuid)

pkgconfig.generate(lib_uuid,
                   description : 'Universally unique id library',
                   subdirs : 'uuid',
                   version : pc_version)

if build_libuuid
  install_man(
    'man/uuid.3',
    'man/uuid_clear.3',
    'man/uuid_compare.3',
    'man/uuid_copy.3',
    'man/uuid_generate.3',
    'man/uuid_is_null.3',
    'man/uuid_parse.3',
    'man/uuid_time.3',
    'man/uuid_unparse.3',
    'man/uuid_generate_random.3',
    'man/uuid_generate_time.3',
    'man/uuid_generate_time_safe.3')
endif
