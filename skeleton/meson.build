# GTK4 + libadwaita port: foundation skeleton.
#
# Self-contained on purpose -- it links only GTK4 + libadwaita (+ glib/gio,
# pulled in transitively) and does not depend on the legacy libdia/config.h.
# As lib/ and app/ widgets are ported, add their sources/deps here and wire
# them into the window in main.c.

executable(
  'dia',
  'main.c',
  c_args: [
    '-DPACKAGE_VERSION="@0@"'.format(meson.project_version()),
  ],
  dependencies: [
    libadwaita_dep,
    libgtk_dep,
    libglib_dep,
    gio_dep,
  ],
  install: true,
)
