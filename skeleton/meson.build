# GTK4 + libadwaita port: foundation skeleton.
#
# Links GTK4 + libadwaita and the ported core library (libdia-core), so the
# canvas can draw via the real DiaCairoRenderer. As app/ is ported, wire its
# widgets in here.

# Tool-palette images: object pixmaps + control-tool icons, found in
# objects/standard/pixmaps and app/icons. The generated source auto-registers
# the resource bundle at startup.
gnome = import('gnome')
skeleton_resources = gnome.compile_resources(
  'dia-skeleton-resources',
  'dia-skeleton.gresource.xml',
  source_dir: [
    meson.project_source_root() / 'objects' / 'standard' / 'pixmaps',
    meson.project_source_root() / 'app' / 'icons',
  ],
  c_name: 'dia_skeleton',
)

dia_skeleton_exe = executable(
  'dia',
  'main.c',
  'dia-shell.c',
  skeleton_resources,
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

# Headless CLI tests (no display needed).
python3_prog = find_program('python3', required: false)
if get_option('tests') and python3_prog.found()
  test(
    'cli-export',
    python3_prog,
    args: [
      meson.project_source_root() / 'ui-tests' / 'test_cli_export.py',
      meson.project_source_root() / 'samples' / 'Circuit.dia',
    ],
    env: {'DIA_BIN': dia_skeleton_exe.full_path()},
  )
  # Regression test for GNOME/dia#524 (relative image paths on export).
  test(
    'image-relpath',
    python3_prog,
    args: [meson.project_source_root() / 'ui-tests' / 'test_image_relpath.py'],
    env: {'DIA_BIN': dia_skeleton_exe.full_path()},
  )
endif
