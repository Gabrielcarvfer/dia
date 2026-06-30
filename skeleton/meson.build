# GTK4 + libadwaita port: foundation skeleton.
#
# Links GTK4 + libadwaita and the ported core library (libdia-core), so the
# canvas can draw via the real DiaCairoRenderer. As app/ is ported, wire its
# widgets in here.

dia_skeleton_exe = executable(
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
    libdia_objects_dep,
  ],
  install: true,
)

# Headless dogtail UI test (ui-tests/run.sh re-execs under xvfb-run +
# dbus-run-session). Registered only when the tooling is present, so a plain
# build without the test deps still configures.
xvfb_run = find_program('xvfb-run', required: false)
bash_prog = find_program('bash', required: false)
if get_option('tests') and xvfb_run.found() and bash_prog.found()
  test(
    'dogtail-ui',
    bash_prog,
    args: [meson.project_source_root() / 'ui-tests' / 'run.sh'],
    env: {'DIA_BIN': dia_skeleton_exe.full_path()},
    timeout: 200,
  )
endif
