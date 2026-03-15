libscreenshooter_core_sources = [
  'sc-cloud-config.c',
  'sc-cloud-config.h',
  'sc-r2.c',
  'sc-r2.h',
  'sc-jira.c',
  'sc-jira.h',
  'sc-recorder.c',
  'sc-recorder.h',
  'sc-video-editor-blur.c',
  'sc-video-editor-blur.h',
]

libscreenshooter_core = static_library(
  'screenshooter-core',
  libscreenshooter_core_sources,
  include_directories: [
    include_directories('..'),
    include_directories('../..'),
  ],
  dependencies: [
    glib,
    dependency('gio-2.0'),
    libcurl,
    json_glib,
    libm,
    libscreenshooter_platform_dep,
  ],
  install: false,
)

libscreenshooter_core_dep = declare_dependency(
  link_with: libscreenshooter_core,
  include_directories: include_directories('.'),
)
