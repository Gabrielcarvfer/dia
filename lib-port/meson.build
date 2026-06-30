# GTK4 port: growing core of libdia.
#
# This rebuilds the dependency-light core model of lib/ against GTK4, one file
# at a time, as each is verified to compile. The sources still live in ../lib
# (untouched, so the legacy GTK3 build keeps working); they are pulled in here
# and compiled with the GTK4 headers. As more of lib/ is ported, add the files
# to `libdia_core_ported` below.

gnome = import('gnome')

lib_dir = meson.project_source_root() / 'lib'

# Unversioned: the core model only needs libxml declarations (tree.h). The
# legacy build pins >= 2.14; the port works fine against the system libxml2.
port_libxml_dep = dependency('libxml-2.0')

# Bundled GdkPixbuf loader (subproject); only needs glib + gdk-pixbuf, no GTK,
# so it is fine under GTK4. Needed by object.c for object-type icons.
xpm_dep = dependency('xpm-pixbuf')

# --- Generated headers/sources the core depends on -------------------------
dia_version_info_h = configure_file(
  configuration: {
    'VERSION_MAJOR': meson.project_version().split('.')[0],
    'VERSION_MINOR': meson.project_version().split('.')[1],
    'VERSION_MICRO': meson.project_version().split('.')[2],
  },
  input: files(lib_dir / 'dia-version-info.h.in'),
  output: 'dia-version-info.h',
)
dia_version_info_c = vcs_tag(
  input: files(lib_dir / 'dia-version-info.c.in'),
  output: 'dia-version-info.c',
)
dia_lib_enums = gnome.mkenums_simple(
  'dia-lib-enums',
  sources: files(
    lib_dir / 'dia-enums.h',
    lib_dir / 'font.h',
    lib_dir / 'units.h',
  ),
)
dia_lib_private_enums = gnome.mkenums_simple(
  'dia-lib-private-enums',
  sources: files(lib_dir / 'dia-colour-selector-private.h'),
)
diamarshal = gnome.genmarshal(
  'diamarshal',
  prefix: 'dia_marshal',
  sources: files(lib_dir / 'diamarshal.list'),
  valist_marshallers: true,
)

libdia_core_generated = [
  dia_version_info_c,
  dia_version_info_h,
  dia_lib_enums,
  dia_lib_private_enums,
  diamarshal,
]

# --- Ported core model sources (GROW THIS LIST as files are verified) ------
libdia_core_ported = files(
  lib_dir / 'geometry.c',
  lib_dir / 'units.c',
  lib_dir / 'boundingbox.c',
  lib_dir / 'arrows.c',
  lib_dir / 'attributes.c',
  lib_dir / 'autoroute.c',
  lib_dir / 'bezier-common.c',
  lib_dir / 'bezier_conn.c',
  lib_dir / 'beziershape.c',
  lib_dir / 'connection.c',
  lib_dir / 'connectionpoint.c',
  lib_dir / 'connpoint_line.c',
  lib_dir / 'create.c',
  lib_dir / 'debug.c',
  lib_dir / 'dia-change.c',
  lib_dir / 'dia-handle.c',
  # dia-io.c needs libxml >= 2.14 (XML_SAVE_EMPTY); not a GTK4 issue. Re-add
  # once the system libxml satisfies the legacy >= 2.14 requirement.
  lib_dir / 'dia-object-change-list.c',
  lib_dir / 'dia-object-change.c',
  lib_dir / 'dia-part.c',
  lib_dir / 'dia-state-object-change.c',
  lib_dir / 'dia-text.c',
  lib_dir / 'dia_dirs.c',
  lib_dir / 'dia_svg.c',
  lib_dir / 'dia_xml.c',
  lib_dir / 'diacontext.c',
  lib_dir / 'diagramdata.c',
  lib_dir / 'diapathrenderer.c',
  lib_dir / 'diarenderer.c',
  lib_dir / 'diasvgrenderer.c',
  lib_dir / 'diatransform.c',
  lib_dir / 'diatransformrenderer.c',
  lib_dir / 'dynamic_obj.c',
  lib_dir / 'element.c',
  lib_dir / 'filter.c',
  lib_dir / 'focus.c',
  lib_dir / 'group.c',
  lib_dir / 'intl.c',
  lib_dir / 'layer.c',
  lib_dir / 'object-alias.c',
  lib_dir / 'object_defaults.c',
  lib_dir / 'orth_conn.c',
  lib_dir / 'paper.c',
  lib_dir / 'parent.c',
  lib_dir / 'path-math.c',
  lib_dir / 'pattern.c',
  lib_dir / 'plug-ins.c',
  lib_dir / 'poly_conn.c',
  lib_dir / 'polyshape.c',
  lib_dir / 'prefs.c',
  lib_dir / 'propdesc.c',
  lib_dir / 'properties.c',
  lib_dir / 'proplist.c',
  lib_dir / 'propoffsets.c',
  lib_dir / 'propregistry.c',
  lib_dir / 'sheet.c',
  lib_dir / 'standard-path.c',
  lib_dir / 'textline.c',

  # Tier 2: use only GTK4-valid symbols (GdkPixbuf, GtkWidget prototypes,
  # GtkCellRenderer/GtkTreeView which still exist in GTK 4.14).
  lib_dir / 'object.c',
  lib_dir / 'propobject.c',
  lib_dir / 'prop_attr.c',
  lib_dir / 'prop_basic.c',
  lib_dir / 'prop_pattern.c',
  lib_dir / 'prop_sdarray.c',
  lib_dir / 'diaimportrenderer.c',
  lib_dir / 'dia-colour.c',
  lib_dir / 'diacellrendererenum.c',
  lib_dir / 'diaoptionmenu.c',
  lib_dir / 'dia-simple-list.c',
  lib_dir / 'dia-guide.c',
  lib_dir / 'dialib.c',

  # Tier 3: ported by hand (removed GTK3 APIs migrated to GTK4 equivalents).
  lib_dir / 'font.c',   # gtk_get_default_language/gdk_pango_context_get -> Pango/PangoCairo

  # --- Still need real GTK4 widget porting (deferred) ---------------------
  # prop_geomtypes.c        -> pulls dia-unit-spinner.h (subclasses
  #                            GtkSpinButton; GtkSpinButtonClass is private in
  #                            GTK4 -> widget needs porting first).
  # dia-colour-cell-renderer.c, and the other cell renderers -> GtkCellRenderer
  #                            'render' vfunc (cairo) replaced by 'snapshot'.
)

libdia_core_deps = [
  config_dep,
  libglib_dep,
  gio_dep,
  libgtk_dep,   # GTK4; also provides cairo / pango / graphene / gdk-pixbuf includes
  port_libxml_dep,
  xpm_dep,
  libm_dep,
]

libdia_core = static_library(
  'dia-core',
  libdia_core_ported + libdia_core_generated,
  include_directories: include_directories('../lib'),
  dependencies: libdia_core_deps,
  c_args: ['-DLIBDIA_COMPILATION'],
)

libdia_core_dep = declare_dependency(
  link_with: libdia_core,
  include_directories: include_directories('../lib'),
  # Expose generated headers so dependents can #include them.
  sources: [dia_version_info_h, dia_lib_enums[1], dia_lib_private_enums[1], diamarshal[1]],
  dependencies: libdia_core_deps,
)
