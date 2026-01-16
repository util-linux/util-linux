po4a = find_program('po4a', required : false)

if not get_option('translate-docs').require(asciidoctor.found()).require(po4a.found()).allowed()
  subdir_done()
endif

awk = find_program('awk', required : true)
locales_output = run_command(awk, '/\[po4a_langs\]/ {for (i=2; i<=NF; i++) print $i}', 'po4a.cfg', check : true)
locales = locales_output.stdout().strip().split('\n')
po_files = []
foreach locale : locales
  po_files += locale + '.po'
endforeach

custom_target('translate',
  build_by_default : true,
  command : [meson.project_source_root() / 'tools/poman-translate.sh',
    '--srcdir', meson.current_source_dir(),
    '--destdir', meson.current_build_dir() / 'translations',
    '--asciidoctor-load-path', meson.project_source_root() / 'tools',
    '--docdir', docdir,
    '--po4acfg', meson.current_source_dir() / 'po4a.cfg',
    '--util-linux-version', meson.project_version(),
    '@INPUT@'],
  console : true,
  depend_files : ['po4a.cfg', 'util-linux-man.pot'] + po_files,
  input : man_common_adocs + manadocs,
  output : ['translations'])

manpages = []
foreach adoc : manadocs
  manpages += fs.stem(adoc)
endforeach

meson.add_install_script(meson.project_source_root() / 'tools/poman-install.sh',
  '--force-destdir',
  '--mandir', mandir,
  '--mansrcdir', meson.current_build_dir() / 'translations/man',
  manpages)
