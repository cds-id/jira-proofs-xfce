libscreenshooter_platform_sources = [
  'sc-platform.h',
]

platform_deps = [
  glib,
  dependency('gdk-pixbuf-2.0'),
]

if host_machine.system() == 'windows'
  libscreenshooter_platform_sources += 'sc-platform-windows.c'
  add_project_arguments('-D_WIN32_WINNT=0x0A00', language: 'c')
  platform_deps += [
    cc.find_library('gdi32'),
    cc.find_library('user32'),
    cc.find_library('shell32'),
    cc.find_library('advapi32'),
    cc.find_library('ole32'),
  ]
else
  libscreenshooter_platform_sources += 'sc-platform-linux.c'
endif

libscreenshooter_platform = static_library(
  'screenshooter-platform',
  libscreenshooter_platform_sources,
  include_directories: [
    include_directories('..'),
    include_directories('../..'),
  ],
  dependencies: platform_deps,
  install: false,
)

libscreenshooter_platform_dep = declare_dependency(
  link_with: libscreenshooter_platform,
  include_directories: include_directories('.'),
  dependencies: platform_deps,
)
