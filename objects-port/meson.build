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
  # standard.c (just a dia_plugin_init) dropped: we register manually and its
  # dia_plugin_init would clash with custom.c's below.
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
  # UML (uml.c excluded -- its dia_plugin_init would clash and it defines the
  # shared _uml_visibilities/_uml_inheritances arrays, which we redefine in
  # register-objects.c instead). Only GTK4-clean objects are listed here.
  objs / 'UML' / 'stereotype.c',      # shared helper, no GTK
  objs / 'UML' / 'umlattribute.c',    # UMLAttribute boxed type + visible_char[]
  # connection-based relationship objects (generic StdProp dialog, no custom UI)
  objs / 'UML' / 'association.c',
  objs / 'UML' / 'dependency.c',
  objs / 'UML' / 'generalization.c',
  objs / 'UML' / 'realizes.c',
  objs / 'UML' / 'implements.c',
  objs / 'UML' / 'constraint.c',
  objs / 'UML' / 'message.c',
  objs / 'UML' / 'transition.c',
  # element objects (all use the generic StdProp dialog, no custom GTK UI)
  objs / 'UML' / 'actor.c',
  objs / 'UML' / 'usecase.c',
  objs / 'UML' / 'node.c',
  objs / 'UML' / 'component.c',
  objs / 'UML' / 'component_feature.c',
  objs / 'UML' / 'note.c',
  objs / 'UML' / 'small_package.c',
  objs / 'UML' / 'large_package.c',
  objs / 'UML' / 'branch.c',
  objs / 'UML' / 'fork.c',
  objs / 'UML' / 'lifeline.c',
  objs / 'UML' / 'state.c',
  objs / 'UML' / 'state_term.c',
  objs / 'UML' / 'activity.c',
  objs / 'UML' / 'object.c',
  objs / 'UML' / 'classicon.c',
  # UML Class: the object + its data sub-types. The bespoke property dialog
  # (class_dialog.c and the attribute/operation/template pages) is heavy GTK3
  # code and is NOT compiled; class.c falls back to the generic StdProp dialog
  # (see UMLCLASS_CUSTOM_DIALOG in class.c).
  objs / 'UML' / 'umloperation.c',
  objs / 'UML' / 'umlparameter.c',
  objs / 'UML' / 'umlformalparameter.c',
  objs / 'UML' / 'class.c',
  # custom-shapes engine: loads .shape XML files as object types (custom.c's
  # dia_plugin_init is the only one linked now and is left unused; we call
  # custom_object_load ourselves from register-objects.c).
  objs / 'custom' / 'custom.c',
  objs / 'custom' / 'custom_object.c',
  objs / 'custom' / 'custom_util.c',
  objs / 'custom' / 'shape_info.c',
  objs / 'custom' / 'shape_typeinfo.c',
  # shared registration (used by the skeleton and the object tests)
  meson.current_source_dir() / 'register-objects.c',
)

libdia_objects = static_library(
  'dia-objects',
  libdia_objects_sources,
  include_directories: include_directories('../objects/standard', '../objects/custom'),
  dependencies: [libdia_core_dep, gmodule_dep],
  c_args: [
    '-DLIBDIA_COMPILATION', '-Werror=implicit-function-declaration',
    # source location of the bundled .shape / .sheet trees, for the dev build
    '-DDIA_SHAPES_DIR="@0@"'.format(meson.project_source_root() / 'shapes'),
  ],
)

libdia_objects_dep = declare_dependency(
  link_with: libdia_objects,
  # expose this dir so callers can #include "register-objects.h"
  include_directories: include_directories('.'),
  dependencies: [libdia_core_dep, gmodule_dep],
)
