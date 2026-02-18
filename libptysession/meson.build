cc = meson.get_compiler('c')
pkg = import('pkgconfig')
dir_libptysession = include_directories('src')

lib_ptysession_sources = '''
  src/pty-session.h
  src/pty-session.c
'''.split() + [monotonic_c]

libptysession_sym = 'src/libptysession.sym'
libptysession_sym_path = '@0@/@1@'.format(meson.current_source_dir(), libptysession_sym)

if build_libptysession
  lib_ptysession = library(
    'ptysession',
    lib_ptysession_sources,
    include_directories : [dir_include],
    link_args : ['-Wl,--version-script=@0@'.format(libptysession_sym_path)],
    link_depends : libptysession_sym,
    dependencies : [lib_util, realtime_libs],
    install : build_libptysession,
    version : libptysession_version,
  )

  ptysession_dep = declare_dependency(link_with: lib_ptysession, include_directories: dir_libptysession)

  pkg.generate(
    lib_ptysession,
    description : 'library to create parent proxy for psuedo-terminal child',
    subdirs : 'libptysession',
    version : pc_version
  )
  meson.override_dependency('ptysession', ptysession_dep)

  install_headers('src/pty-session.h', subdir : 'libptysession')

endif

