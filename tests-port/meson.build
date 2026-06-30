# GTK4 port: run a subset of the upstream libdia unit tests (tests/) against the
# ported libdia-core, so the core library port is validated beyond the GUI
# dogtail suite. Dependency-light tests only — test-graphene needs
# dia-graphene.c (not yet ported) and test-objects loads object .so plugins
# (the port registers types statically), so both are skipped for now.

tests_dir = meson.project_source_root() / 'tests'

# test-colour: exercises dia-colour.c (parse/format/copy/GdkRGBA conversion).
test(
  'lib-colour',
  executable(
    'test-colour-port',
    [tests_dir / 'test-colour.c', tests_dir / 'dia-test-utils.c'],
    dependencies: [config_dep, libdia_core_dep, libgtk_dep],
    include_directories: include_directories('../tests'),
  ),
  protocol: 'tap',
)

# test-boundingbox: exercises the polybezier bounding-box geometry.
test(
  'lib-boundingbox',
  executable(
    'test-boundingbox-port',
    [tests_dir / 'test-boundingbox.c'],
    dependencies: [config_dep, libdia_core_dep, libgtk_dep, libm_dep],
    include_directories: include_directories('../tests'),
  ),
  protocol: 'tap',
)
