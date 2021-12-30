dir_libsmartcols = include_directories('.', 'src')

defs = configuration_data()
defs.set('LIBSMARTCOLS_VERSION', pc_version)

libsmartcols_h = configure_file(
  input : 'src/libsmartcols.h.in',
  output : 'libsmartcols.h',
  configuration : defs,
  install : build_libsmartcols,
  install_dir : join_paths(get_option('includedir'), 'libsmartcols'),
)

lib_smartcols_sources = '''
  src/smartcolsP.h
  src/iter.c
  src/symbols.c
  src/cell.c
  src/column.c
  src/line.c
  src/table.c
  src/print.c
  src/print-api.c
  src/version.c
  src/calculate.c
  src/grouping.c
  src/walk.c
  src/init.c
'''.split()

libsmartcols_sym = 'src/libsmartcols.sym'
libsmartcols_sym_path = '@0@/@1@'.format(meson.current_source_dir(), libsmartcols_sym)

lib_smartcols = both_libraries(
  'smartcols',
  list_h,
  lib_smartcols_sources,
  include_directories : [dir_include, dir_libsmartcols],
  link_depends : libsmartcols_sym,
  version : libsmartcols_version,
  link_args : ['-Wl,--version-script=@0@'.format(libsmartcols_sym_path)],
  link_with : lib_common,
  dependencies : build_libsmartcols ? [] : disabler(),
  install : build_libsmartcols)

lib_smartcols_static = lib_smartcols.get_static_lib()

if build_libsmartcols
  pkgconfig.generate(lib_smartcols,
                     description : 'table or tree library',
                     subdirs : 'libsmartcols',
                     version : pc_version)
endif
