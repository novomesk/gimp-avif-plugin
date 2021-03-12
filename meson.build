project('file-avif', 'c',
  license : 'GPL3',
  default_options: [
    'c_std=gnu11',
    'cpp_std=gnu++14',
    'warning_level=1'
  ]
)

warning_cflags_common = [
  '-fdiagnostics-show-option',
  '-fno-common',

  '-Wformat',
  '-Wformat-security',
  '-Winit-self',
  '-Wlogical-op',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wpointer-arith',
  '-Wreturn-type',
  '-Wtype-limits',
]
warning_cflags_c = [
  '-Wabsolute-value',
  '-Wdeclaration-after-statement',
  '-Wenum-conversion',
  '-Wliteral-conversion',
  '-Wno-strict-prototypes',
  '-Wold-style-definition',
  '-Wparentheses-equality',
  '-W#pragma-messages',
  '-Wsometimes-uninitialized',
  '-Wtautological-unsigned-enum-zero-compare',
  '-Wunneeded-internal-declaration',
  '-Wunused-function',
  '-Wunused-value',

  '-Werror=implicit-function-declaration',
  '-Wsign-compare',
]

cc        = meson.get_compiler('c')
compiler_args = []
compiler_args += cc.get_supported_arguments('-fPIE')
compiler_args += cc.get_supported_arguments(warning_cflags_common)
add_project_arguments(cc .get_supported_arguments(warning_cflags_c),   language: 'c')
add_project_arguments(compiler_args, language: [ 'c' ])

gimp             = dependency('gimp-3.0')
gimpplugindir    = gimp.get_pkgconfig_variable('gimplibdir')

libgimpui_dep    = dependency('gimpui-3.0')

gexiv2_minver    = '0.12.2'
gexiv2           = dependency('gexiv2',             version: '>='+gexiv2_minver)

lcms_minver      = '2.8'
lcms             = dependency('lcms2',              version: '>='+lcms_minver)

subdir('src')
