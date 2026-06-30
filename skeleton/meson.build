# GTK4 + libadwaita port: foundation skeleton.
#
# Links GTK4 + libadwaita and the ported core library (libdia-core), so the
# canvas can draw via the real DiaCairoRenderer. As app/ is ported, wire its
# widgets in here.

executable(
  'dia',
  'main.c',
  'dia-shell.c',
  c_args: [
    '-DPACKAGE_VERSION="@0@"'.format(meson.project_version()),
  ],
  dependencies: [
    libadwaita_dep,
    libgtk_dep,
    libglib_dep,
    gio_dep,
    libdia_core_dep,
  ],
  install: true,
)
