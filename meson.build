project('file-avif', 'c', 'cpp',
  license : 'GPL3',
  default_options: [
    'c_std=gnu11',
    'cpp_std=gnu++14',
    'warning_level=1'
  ]
)

gimp             = dependency('gimp-3.0')
gimpplugindir = gimp.get_pkgconfig_variable('gimplibdir')

subdir('src')
