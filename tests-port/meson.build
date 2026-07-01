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

# Regression test for #374 (text cut off on export): the flowchart shapes must
# include their (possibly overflowing, "Text fitting = Never") label in the
# object bounding box, else the cairo export surface clips it at the edge.
test(
  'lib-flowchart-text-bbox',
  executable(
    'test-flowchart-text-bbox-port',
    ['test-flowchart-text-bbox-port.c'],
    dependencies: [config_dep, libdia_core_dep, libdia_objects_dep,
                   libgtk_dep, libm_dep],
  ),
  protocol: 'tap',
)

# Regression test for #456 / #392: dia_relativize_filename() must only strip the
# diagram's directory prefix when it ends on a path-component boundary, so a
# sibling directory sharing a name prefix ("/foo/bar" vs "/foo/barbaz") is not
# mistaken for a child and the embedded-image path isn't chopped mid-component.
test(
  'lib-dia-dirs',
  executable(
    'test-dia-dirs-port',
    ['test-dia-dirs-port.c'],
    dependencies: [config_dep, libdia_core_dep, libgtk_dep, libm_dep],
  ),
  protocol: 'tap',
)

# Regression test for #140: Dia must not crash on highly broken files. A too
# short (empty or single-character) <string> node made data_string() strip a
# non-existent "#...#" frame and write before the start of its buffer.
test(
  'lib-dia-xml-string',
  executable(
    'test-dia-xml-string-port',
    ['test-dia-xml-string-port.c'],
    dependencies: [config_dep, libdia_core_dep, libgtk_dep, libm_dep],
  ),
  protocol: 'tap',
)

# Regression test for the DiaContext message-log leak: the add_message helpers
# used g_strv_builder_add() (which copies) on a g_strdup_vprintf()ed string and
# never freed the original. Runs under ASan/LSan.
test(
  'lib-diacontext',
  executable(
    'test-diacontext-port',
    ['test-diacontext-port.c'],
    dependencies: [config_dep, libdia_core_dep, libgtk_dep, libm_dep],
  ),
  protocol: 'tap',
)

# Regression test for #363: a UML class template's formal parameter is drawn as
# "<name> : <type>" only when a type/kind is given, but uml_formal_parameter_
# get_string() keyed the ":" on "type != NULL"; the dialog stores an empty
# string (not NULL) for a blank type, so a plain type parameter "T" rendered as
# "T:". The UML object set isn't in the port's object lib yet, so this compiles
# the single source file it needs directly.
test(
  'lib-uml-formalparam',
  executable(
    'test-uml-formalparam-port',
    ['test-uml-formalparam-port.c',
     meson.project_source_root() / 'objects/UML/umlformalparameter.c'],
    dependencies: [config_dep, libdia_core_dep, libgtk_dep, libm_dep],
    include_directories: include_directories('../objects/UML'),
  ),
  protocol: 'tap',
)

# Regression test for #140 (broken-file crash): the numeric data_* node parsers
# (data_int/data_enum/data_real/data_point/data_rectangle) dereferenced the
# "val" attribute unconditionally, so a corrupt file with a val-less node (e.g. a
# Standard - Box whose line_style is "<dia:enum/>") crashed the loader in
# atoi(NULL) / an uninitialised strtod endptr. Complements the data_string fix.
test(
  'lib-dia-xml-parse',
  executable(
    'test-dia-xml-parse-port',
    ['test-dia-xml-parse-port.c'],
    dependencies: [config_dep, libdia_core_dep, libgtk_dep, libm_dep],
  ),
  protocol: 'tap',
)
