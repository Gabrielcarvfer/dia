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

# Regression test for the rgba() colour-parsing bug in lib/dia_svg.c: the
# "rgba(r,g,b,a)" branch ran sscanf() at str+4 (the "rgb(" offset) instead of
# str+5, so the leftover '(' made every rgba() colour fail to parse and stay
# black. Also covers the signed "<<24" shift UB on the same lines.
test(
  'lib-dia-svg-color',
  executable(
    'test-dia-svg-color-port',
    ['test-dia-svg-color-port.c'],
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

# Regression test for an uninitialised dash length in "Standard - Path":
# stdpath_create() forgot to seed the line-style defaults (every sibling object
# does), so the dash length stayed 0.0 (from g_new0) instead of
# DEFAULT_LINESTYLE_DASHLEN, giving a degenerate dashed style and a line-style
# block that did not round-trip through save/load.
test(
  'lib-stdpath-dashlen',
  executable(
    'test-stdpath-dashlen-port',
    ['test-stdpath-dashlen-port.c'],
    dependencies: [config_dep, libdia_core_dep, libdia_objects_dep,
                   libgtk_dep, libm_dep],
  ),
  protocol: 'tap',
)

# Regression test for a truncation bug in find_slope_directions(): the slope
# magnitude fabs(dy/dx) was stored in a gint, so an edge whose slope was in
# (0.5, 1.0) truncated to 0 and wrongly failed the "slope > 0.5" test, dropping
# the east/west connection-point direction that polyshape/beziershape assign to
# sloped edges. Storing the slope in a real fixes it.
test(
  'lib-connpoint-slope',
  executable(
    'test-connpoint-slope-port',
    ['test-connpoint-slope-port.c'],
    dependencies: [config_dep, libdia_core_dep, libgtk_dep, libm_dep],
  ),
  protocol: 'tap',
)

# Regression test for a NULL-deref crash in the custom-shape XML parser: a
# <svg:polyline>/<svg:polygon> without a "points" attribute made shape_info.c
# walk xmlGetProp()'s NULL return and segfault while loading the shape. Also
# checks a well-formed polyline still parses.
test(
  'lib-custom-shape-parse',
  executable(
    'test-custom-shape-parse-port',
    ['test-custom-shape-parse-port.c'],
    dependencies: [config_dep, libdia_core_dep, libdia_objects_dep,
                   libgtk_dep, libm_dep],
    include_directories: include_directories('../objects/custom'),
  ),
  protocol: 'tap',
)

# Regression test for a copy bug in "Standard - Arc": arc_copy() called
# arc_update_data() on the source arc instead of the new copy, leaving the
# copy's enclosing_box at a zero-initialised {0,0,0,0} (object_copy copies
# bounding_box but not the object-specific enclosing_box).
test(
  'lib-arc-copy',
  executable(
    'test-arc-copy-port',
    ['test-arc-copy-port.c'],
    dependencies: [config_dep, libdia_core_dep, libdia_objects_dep,
                   libgtk_dep, libm_dep],
  ),
  protocol: 'tap',
)

# Regression test for the same copy bug in "Standard - Line": line_copy() called
# line_update_data() on the source line instead of the copy, so the copy's
# mid-line connection point was left at the origin (0,0) rather than being
# placed on the line.
test(
  'lib-line-copy',
  executable(
    'test-line-copy-port',
    ['test-line-copy-port.c'],
    dependencies: [config_dep, libdia_core_dep, libdia_objects_dep,
                   libgtk_dep, libm_dep],
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

# Regression test for a copy bug in "Standard - BezierLine"/"Standard -
# Beziergon" (same class as the arc_copy/line_copy fixes): their _copy()
# allocated a fresh enclosing_box but never called their _update_data(), so the
# box that also spans the off-curve control points stayed {0,0,0,0} on the copy
# (bezierconn_copy/beziershape_copy recompute only the bounding_box).
test(
  'lib-bezier-copy',
  executable(
    'test-bezier-copy-port',
    ['test-bezier-copy-port.c'],
    dependencies: [config_dep, libdia_core_dep, libdia_objects_dep,
                   libgtk_dep, libm_dep],
  ),
  protocol: 'tap',
)

# Regression test for a copy bug in "Network - WAN Link": wanlink_copy() copied
# the width/colours but never recomputed the cached render/hit-test polygon
# (wanlink->poly), leaving it at {0,0}. object_copy() carried the bounding box
# over so the flaw was invisible there, but distance_from()/draw() used the
# degenerate polygon at the origin. Calling wanlink_update_data() on the copy
# (as create/load/move all do) rebuilds it.
test(
  'lib-wanlink-copy',
  executable(
    'test-wanlink-copy-port',
    ['test-wanlink-copy-port.c'],
    dependencies: [config_dep, libdia_core_dep, libdia_objects_dep,
                   libgtk_dep, libm_dep],
  ),
  protocol: 'tap',
)

# Regression test for a copy bug in "ER - Relationship": relationship_copy()
# rebuilds the connection-point array by hand but forgot to copy .flags, so the
# central connection point lost its CP_FLAGS_MAIN (anyplace/auto-gap) marker on
# every copy/duplicate. The sibling ER objects (attribute, entity) copy .flags
# in the same loop; relationship.c was the odd one out.
test(
  'lib-relationship-copy',
  executable(
    'test-relationship-copy-port',
    ['test-relationship-copy-port.c'],
    dependencies: [config_dep, libdia_core_dep, libdia_objects_dep,
                   libgtk_dep, libm_dep],
  ),
  protocol: 'tap',
)

# Regression test for a division-by-zero in the custom-shape XML parser's
# aspect-ratio derivation: update_bounds() computes the missing default
# dimension as (default_width / shape_width) * shape_height (or the height
# analogue), which divided by zero for a shape with zero extent in that
# direction (a vertical/horizontal line, or an empty drawing), leaving a
# non-finite default size that then propagated into a created object.
test(
  'lib-custom-shape-aspect',
  executable(
    'test-custom-shape-aspect-port',
    ['test-custom-shape-aspect-port.c'],
    dependencies: [config_dep, libdia_core_dep, libdia_objects_dep,
                   libgtk_dep, libm_dep],
    include_directories: include_directories('../objects/custom'),
  ),
  protocol: 'tap',
)

# Upstream SVG path-data parser unit test (dia_svg_parse_path): validates that
# our ported dia-svg.c reads SVG path strings correctly — part of "sane output".
test(
  'lib-svg',
  executable(
    'test-svg-port',
    [tests_dir / 'test-svg.c'],
    dependencies: [config_dep, libdia_core_dep, libgtk_dep, libm_dep],
    include_directories: include_directories('../tests'),
  ),
  protocol: 'tap',
)
