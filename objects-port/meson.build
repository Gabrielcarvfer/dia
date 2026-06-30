# GTK4 port: the standard object types, compiled against GTK4 and linked into
# the skeleton so the canvas can create real DiaObjects (Standard - Box, etc.).
# Sources stay in objects/standard (untouched for the legacy build).

obj_dir = meson.project_source_root() / 'objects' / 'standard'

libdia_objects_sources = files(
  obj_dir / 'arc.c',
  obj_dir / 'box.c',
  obj_dir / 'ellipse.c',
  obj_dir / 'textobj.c',
  obj_dir / 'line.c',
  obj_dir / 'zigzagline.c',
  obj_dir / 'polyline.c',
  obj_dir / 'bezier.c',
  obj_dir / 'standard.c',
  obj_dir / 'image.c',
  obj_dir / 'outline.c',
  obj_dir / 'polygon.c',
  obj_dir / 'beziergon.c',
)

libdia_objects = static_library(
  'dia-objects',
  libdia_objects_sources,
  include_directories: include_directories('../objects/standard'),
  dependencies: [libdia_core_dep],
  c_args: ['-DLIBDIA_COMPILATION', '-Werror=implicit-function-declaration'],
)

libdia_objects_dep = declare_dependency(
  link_with: libdia_objects,
  dependencies: [libdia_core_dep],
)
