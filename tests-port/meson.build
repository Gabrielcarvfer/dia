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

# Port-adapted object battery: create/clone/move/move-handle/save-load on every
# statically-registered type (standard + flowchart/network/ER), under ASan.
test(
  'lib-objects',
  executable(
    'test-objects-port',
    ['test-objects-port.c'],
    dependencies: [config_dep, libdia_core_dep, libdia_objects_dep,
                   libgtk_dep, libm_dep],
  ),
  protocol: 'tap',
  timeout: 120,
)

# Regression test for #405: a dashed line style must not bleed into arrowheads
# (the ER/UML cardinality "hash mark" arrows used to inherit the stem's dash).
test(
  'lib-arrows',
  executable(
    'test-arrows-port',
    ['test-arrows-port.c'],
    dependencies: [config_dep, libdia_core_dep, libgtk_dep, libm_dep],
  ),
  protocol: 'tap',
)

# Regression test for #482: the per-corner radius of a rounded polyline must be
# computed from the original vertices, not from a start point an earlier
# corner's fillet already trimmed (which under-rounded later corners).
test(
  'lib-rounded-polyline',
  executable(
    'test-rounded-polyline-port',
    ['test-rounded-polyline-port.c'],
    dependencies: [config_dep, libdia_core_dep, libgtk_dep, libm_dep],
  ),
  protocol: 'tap',
)
