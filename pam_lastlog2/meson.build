cc = meson.get_compiler('c')
pkg = import('pkgconfig')
lib_pam_lastlog2_sources = '''
  src/pam_lastlog2.c
'''.split()  

pamlibdir = get_option('pamlibdir')
if pamlibdir == ''
        pamlibdir = get_option('libdir') / 'security'
endif

if build_liblastlog2
  pam_lastlog2_sym = 'src/pam_lastlog2.sym'
  pam_lastlog2_sym_path = '@0@/@1@'.format(meson.current_source_dir(), pam_lastlog2_sym)

  libpam = cc.find_library('pam')

  pam_lastlog2 = both_libraries(
    'pam_lastlog2',
    lib_pam_lastlog2_sources,
    name_prefix : '',
    include_directories : [dir_include, dir_liblastlog2],
    link_args : ['-Wl,--version-script=@0@'.format(pam_lastlog2_sym_path)],
    link_depends : pam_lastlog2_sym,
    link_with : [lib_lastlog2],
    dependencies : [libpam],
    install : build_liblastlog2,
    install_dir : pamlibdir,
    version : liblastlog2_version,
  )
  manadocs += ['pam_lastlog2/man/pam_lastlog2.8.adoc']

  pkg.generate(
    pam_lastlog2,
    description : 'pam library to manage last login data with lastlog2',
    subdirs : 'lastlog2',
    version : pc_version
  )
endif  
