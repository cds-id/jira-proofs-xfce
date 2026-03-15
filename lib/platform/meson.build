libscreenshooter_platform_sources = [
  'sc-platform.h',
  'sc-platform-linux.c',
]

libscreenshooter_platform = static_library(
  'screenshooter-platform',
  libscreenshooter_platform_sources,
  include_directories: [
    include_directories('..'),
    include_directories('../..'),
  ],
  dependencies: [
    glib,
    dependency('gdk-pixbuf-2.0'),
  ],
  install: false,
)

libscreenshooter_platform_dep = declare_dependency(
  link_with: libscreenshooter_platform,
  include_directories: include_directories('.'),
)
