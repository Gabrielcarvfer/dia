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

# The libdia gresource (broken-image.png + chain/path icons + colour-selector
# UI). lib/dia_image.c loads /org/gnome/Dia/broken-image.png as the fallback
# for an unloadable image; without this the fallback fails and an unloadable
# image spews GdkPixbuf criticals and renders garbage. Built into the exe (not
# the static lib) so its auto-register constructor is kept by the linker.
libdia_resources = gnome.compile_resources(
  'dia-lib-resources',
  meson.project_source_root() / 'lib' / 'libdia.gresource.xml',
  source_dir: meson.project_source_root() / 'lib',
  c_name: 'dia_lib',
)

dia_skeleton_exe = executable(
  'dia',
  'main.c',
  'dia-shell.c',
  skeleton_resources,
  libdia_resources,
  c_args: [
    '-DPACKAGE_VERSION="@0@"'.format(meson.project_version()),
    # libdia-core is a STATIC lib, but its headers declare exported globals
    # (e.g. group_type via DIAVAR) with __declspec(dllimport) unless
    # LIBDIA_COMPILATION is set — which made Windows emit __imp_group_type with
    # no import table. Define it here too so DIAVAR is dllexport (a plain
    # reference that resolves from the static archive). No-op off-Windows.
    '-DLIBDIA_COMPILATION',
  ],
  dependencies: [
    libadwaita_dep,
    libgtk_dep,
    libglib_dep,
    gio_dep,
    libdia_core_dep,
    libdia_objects_dep,
    xpm_dep,        # dia-shell.c calls xpm_pixbuf_load() directly; declaring it
                    # here fixes the undefined reference at link on Windows
                    # (static-link ordering resolved it transitively elsewhere).
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
  # An unloadable image must fall back to broken-image, not crash.
  test(
    'missing-image',
    python3_prog,
    args: [meson.project_source_root() / 'ui-tests' / 'test_missing_image.py'],
    env: {'DIA_BIN': dia_skeleton_exe.full_path()},
  )
  # A broken orthconn (missing orth_points) must not crash on load (#319).
  test(
    'broken-orthconn',
    python3_prog,
    args: [meson.project_source_root() / 'ui-tests' / 'test_broken_orthconn.py'],
    env: {'DIA_BIN': dia_skeleton_exe.full_path()},
  )
  # Broken poly/bezier objects (missing poly_points/bez_points) must not crash
  # on load (#319 / bug 302781).
  test(
    'broken-polyobjects',
    python3_prog,
    args: [meson.project_source_root() / 'ui-tests' / 'test_broken_polyobjects.py'],
    env: {'DIA_BIN': dia_skeleton_exe.full_path()},
  )
  # CLI batch/scoping options: multiple inputs, -O/-I dirs, -L layer filter.
  test(
    'cli-batch',
    python3_prog,
    args: [meson.project_source_root() / 'ui-tests' / 'test_cli_batch.py'],
    env: {'DIA_BIN': dia_skeleton_exe.full_path()},
  )
  # Export-parity canary vs upstream `dia` (skips if it/xvfb/Pillow absent).
  test(
    'cli-parity',
    python3_prog,
    args: [
      meson.project_source_root() / 'ui-tests' / 'test_cli_parity.py',
      meson.project_source_root() / 'samples',
    ],
    env: {'DIA_BIN': dia_skeleton_exe.full_path()},
    timeout: 120,
  )
endif
