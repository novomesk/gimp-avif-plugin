
plugin_name = 'file-avif'

plugin_sources = [
  'file-avif.c',
  'file-avif-dialog.c',
  'file-avif-load.c',
  'file-avif-save.c'
]

avif_minver      = '0.8.3'
avif             = dependency('libavif',            version: '>='+avif_minver, required : false )

if avif.found()
  message('We will use dynamic linking with libavif')
else
  message('We need local libavif.a We try to build it. It may take few minutes. Please wait...')

  avif_local_inc = include_directories('../ext/libavif/include')
  avif = declare_dependency( include_directories : avif_local_inc ,
    link_args: [meson.source_root()+'/ext/libavif/build/libavif.a', meson.source_root()+'/ext/libavif/ext/aom/build.libavif/libaom.a' ] )
  # we need to ensure that local dependencies were build
  # build_local_libaom_avif.sh script will buid libaom.a and libavif.a if they are missing
  r = run_command(meson.source_root()+'/ext/build_local_libaom_libavif.sh')
  if r.returncode() != 0
    error(r.stderr())
  endif
endif

executable(plugin_name,
  plugin_sources,
  dependencies: [
    avif,
    gexiv2, libgimpui_dep,
    lcms
  ],
  install: true,
  install_dir: gimpplugindir / 'plug-ins' / plugin_name
)
