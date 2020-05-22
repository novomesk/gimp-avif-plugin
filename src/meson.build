project('file-avif', 'c', 'cpp',
  license : 'GPL3')

plugin_name = 'file-avif'

plugin_sources = [
  'file-avif.c',
  'file-avif-dialog.c',
  'file-avif-load.c',
  'file-avif-save.c',
  'file-avif-exif.cpp'
]

avif_minver      = '0.7.3'
avif             = dependency('libavif',            version: '>='+avif_minver, required : false )

gimpui           = dependency('gimpui-3.0')

exiv2_minver     = '0.25'
exiv2            = dependency('exiv2',              version: '>='+exiv2_minver)

gexiv2_minver    = '0.10.6'
gexiv2           = dependency('gexiv2',             version: '>='+gexiv2_minver)

lcms_minver      = '2.8'
lcms             = dependency('lcms2',              version: '>='+lcms_minver)

if avif.found()
  message('We will use dynamic linking with libavif')
else
  message('We need local libavif.a We try to build it. It may take few minutes. Please wait...')

  avif_local_inc = include_directories('../ext/libavif/include')
  avif = declare_dependency( include_directories : avif_local_inc ,
    link_args: ['../../ext/libavif/build/libavif.a', '../../ext/libavif/ext/aom/build.libavif/libaom.a' ] )
  # we need to ensure that local dependencies were build
  # build_local_libaom_avif.sh script will buid libaom.a and libavif.a if they are missing
  r = run_command('../ext/build_local_libaom_libavif.sh')
  if r.returncode() != 0
    error(r.stderr())
  endif
endif

executable(plugin_name,
  plugin_sources,
  dependencies: [
    avif,
    exiv2,
    gexiv2, gimpui,
    lcms
  ]
)
