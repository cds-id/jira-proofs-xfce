libscreenshooter_gtk_sources = [
  'libscreenshooter.h',
  'screenshooter-actions.c',
  'screenshooter-actions.h',
  'screenshooter-capture.c',
  'screenshooter-capture.h',
  'screenshooter-custom-actions.c',
  'screenshooter-custom-actions.h',
  'screenshooter-format.c',
  'screenshooter-format.h',
  'screenshooter-dialogs.c',
  'screenshooter-dialogs.h',
  'screenshooter-global.h',
  'screenshooter-select.c',
  'screenshooter-select.h',
  'screenshooter-utils.c',
  'screenshooter-utils.h',
  'screenshooter-jira-dialog.c',
  'screenshooter-jira-dialog.h',
  'screenshooter-recorder-dialog.c',
  'screenshooter-recorder-dialog.h',
  'screenshooter-video-editor.c',
  'screenshooter-video-editor.h',
  'screenshooter-video-editor-canvas.c',
  'screenshooter-video-editor-canvas.h',
  'screenshooter-video-editor-timeline.c',
  'screenshooter-video-editor-timeline.h',
  'screenshooter-wizard.c',
  'screenshooter-wizard.h',
]

libscreenshooter_gtk_sources += gnome.genmarshal(
  'screenshooter-marshal',
  sources: 'screenshooter-marshal.list',
  prefix: '_screenshooter_marshal',
  internal: true,
  install_header: false,
)

if enable_x11
  libscreenshooter_gtk_sources += [
    'screenshooter-capture-x11.c',
    'screenshooter-capture-x11.h',
    'screenshooter-select-x11.c',
    'screenshooter-select-x11.h',
    'screenshooter-utils-x11.c',
    'screenshooter-utils-x11.h',
  ]
endif

if enable_wayland
  libscreenshooter_gtk_sources += wayland_protocols_generated_sources
  libscreenshooter_gtk_sources += [
    'screenshooter-capture-wayland.c',
    'screenshooter-capture-wayland.h',
    'screenshooter-select-wayland.c',
    'screenshooter-select-wayland.h',
  ]
endif

libscreenshooter = static_library(
  'libscreenshooter',
  libscreenshooter_gtk_sources,
  include_directories: [
    include_directories('..'),
    include_directories('../..'),
  ],
  dependencies: [
    glib,
    gtk,
    gtk_layer_shell,
    exo,
    libxfce4ui,
    libxfce4util,
    xfconf,
    x11_deps,
    wayland_deps,
    xfixes,
    libcurl,
    json_glib,
    libm,
    libscreenshooter_core_dep,
    libscreenshooter_platform_dep,
  ],
  install: false,
)
