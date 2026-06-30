# GTK4 port: the standard object types, compiled against GTK4 and linked into
# the skeleton so the canvas can create real DiaObjects (Standard - Box, etc.).
# Sources stay in objects/standard (untouched for the legacy build).

objs = meson.project_source_root() / 'objects'

# Each set's own *.c registration file defines dia_plugin_init (they would
# collide if linked together), so we compile only the object files and register
# the types directly in the skeleton (register_object_types in dia-shell.c).
# GTK-clean sets only; UML etc. (heavy property dialogs) come later.
libdia_objects_sources = files(
  # standard
  objs / 'standard' / 'arc.c',
  objs / 'standard' / 'box.c',
  objs / 'standard' / 'ellipse.c',
  objs / 'standard' / 'textobj.c',
  objs / 'standard' / 'line.c',
  objs / 'standard' / 'zigzagline.c',
  objs / 'standard' / 'polyline.c',
  objs / 'standard' / 'bezier.c',
  objs / 'standard' / 'standard.c',   # keeps the only dia_plugin_init we link
  objs / 'standard' / 'image.c',
  objs / 'standard' / 'outline.c',
  objs / 'standard' / 'polygon.c',
  objs / 'standard' / 'beziergon.c',
  # flowchart (object files only; flowchart.c excluded)
  objs / 'flowchart' / 'box.c',
  objs / 'flowchart' / 'diamond.c',
  objs / 'flowchart' / 'ellipse.c',
  objs / 'flowchart' / 'parallelogram.c',
  # network (network.c excluded)
  objs / 'network' / 'basestation.c',
  objs / 'network' / 'bus.c',
  objs / 'network' / 'radiocell.c',
  objs / 'network' / 'wanlink.c',
  # ER (er.c excluded)
  objs / 'ER' / 'attribute.c',
  objs / 'ER' / 'entity.c',
  objs / 'ER' / 'participation.c',
  objs / 'ER' / 'relationship.c',
  # shared registration (used by the skeleton and the object tests)
  meson.current_source_dir() / 'register-objects.c',
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
  # expose this dir so callers can #include "register-objects.h"
  include_directories: include_directories('.'),
  dependencies: [libdia_core_dep],
)
