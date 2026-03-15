windows = import('windows')

# Compiled .rc resource — used by src/meson.build for the final executable
win32_resources = windows.compile_resources(
  'resources.rc',
  include_directories: include_directories('.'),
)

libscreenshooter_win32_sources = [
  'win-wizard.c',
  'win-wizard.h',
  'win-main-dialog.c',
  'win-main-dialog.h',
  'win-video-editor.c',
  'win-video-editor.h',
  'resource.h',
]

libscreenshooter_win32 = static_library(
  'screenshooter-win32',
  libscreenshooter_win32_sources,
  include_directories: [
    include_directories('..'),
    include_directories('../..'),
  ],
  dependencies: [
    glib,
    libscreenshooter_core_dep,
    libscreenshooter_platform_dep,
    cc.find_library('comctl32'),
    cc.find_library('comdlg32'),
  ],
  install: false,
)

libscreenshooter_win32_dep = declare_dependency(
  link_with: libscreenshooter_win32,
  include_directories: include_directories('.'),
)
