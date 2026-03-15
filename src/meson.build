if host_machine.system() == 'windows'
  executable(
    'xfce4-screenshooter',
    'main-win32.c',
    include_directories: [include_directories('..')],
    dependencies: [
      glib,
      libscreenshooter_core_dep,
      libscreenshooter_platform_dep,
    ],
    install: true,
  )
else
  screenshooter_sources = [
    'main.c',
  ]

  screenshooter = executable(
    'xfce4-screenshooter',
    screenshooter_sources,
    sources: xfce_revision_h,
    include_directories: [
      include_directories('..', '..' / 'lib' / 'ui-gtk'),
    ],
    dependencies: [
      gtk,
      glib,
      libxfce4util,
      libxfce4ui,
      xfconf,
      x11_deps,
      wayland_deps,
      xfixes,
      libscreenshooter_core_dep,
      libscreenshooter_platform_dep,
    ],
    link_with: [
      libscreenshooter,
    ],
    install: true,
    install_dir: get_option('prefix') / get_option('bindir'),
  )

  i18n.merge_file(
      input: 'xfce4-screenshooter.desktop.in',
      output: 'xfce4-screenshooter.desktop',
      po_dir: '..' / 'po',
      type: 'desktop',
      install: true,
      install_dir: get_option('prefix') / get_option('datadir') / 'applications',
  )

  i18n.merge_file(
      input: 'xfce4-screenshooter.appdata.xml.in',
      output: 'xfce4-screenshooter.appdata.xml',
      po_dir: '..' / 'po',
      type: 'xml',
      install: true,
      install_dir: get_option('prefix') / get_option('datadir') / 'metainfo',
  )

  help2man = find_program('help2man', required: false)
  if help2man.found()
    custom_target(
      'xfce4-screenshooter.1',
      depends: screenshooter,
      output: 'xfce4-screenshooter.1',
      command: [
        help2man, ['--no-info'], '--output=@OUTPUT@', screenshooter.full_path(),
      ],
      install: true,
      install_dir: get_option('prefix') / get_option('mandir') / 'man1',
    )
  endif
endif
