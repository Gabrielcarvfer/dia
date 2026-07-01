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

# Bundled XPM->GdkPixbuf helper (subproject); only needs glib + gdk-pixbuf, no
# GTK, so it is fine under GTK4. Needed by object.c for object-type icons.
# Kept as a SHARED library: its header marks xpm_pixbuf_load G_MODULE_EXPORT
# (__declspec(dllexport) on Windows), so a static build leaves consumers with an
# undefined __imp_ reference on MinGW. The packagers bundle the .so/.dll/.dylib
# (the Linux AppImage points linuxdeploy at it via LD_LIBRARY_PATH).
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
  lib_dir / 'dia-io.c',   # XML_SAVE_EMPTY shimmed for libxml < 2.14
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
  lib_dir / 'dia-unit-spinner.c',  # GtkSpinButton final in GTK4 -> compose instead of subclass
  lib_dir / 'prop_geomtypes.c',    # unblocked by dia-unit-spinner.h
  lib_dir / 'diainteractiverenderer.c',  # only GdkWindow ref was a doc comment
  lib_dir / 'dia-line-cell-renderer.c',   # GtkCellRenderer render->snapshot, get_size->get_preferred_*
  lib_dir / 'dia-arrow-cell-renderer.c',  # render->snapshot; get_background_color removed
  lib_dir / 'dia-colour-cell-renderer.c', # render->snapshot, chains to parent ->snapshot
  lib_dir / 'diacellrendererenum.c',      # compiles as-is (no render override)
  lib_dir / 'dia-line-preview.c',         # GtkWidget draw vfunc -> snapshot
  lib_dir / 'dia-arrow-preview.c',        # draw -> snapshot; get_background_color removed
  lib_dir / 'prop_inttypes.c',            # gtk_entry_get/set_text -> GtkEditable
  lib_dir / 'dia_image.c',                # gdk-pixbuf only, compiles as-is
  lib_dir / 'dia-colour-selector.c',      # gtk_widget_show (deprecated, present)
  lib_dir / 'prop_dict.c',                # scrolled-window ctor/set_child, drop show_all
  lib_dir / 'prop_matrix.c',              # box_pack_start->append, container_get_children->walk
  lib_dir / 'dia-font-selector.c',        # box_pack_start->append
  lib_dir / 'prop_pixbuf.c',              # gdk-pixbuf only
  lib_dir / 'dia-arrow-selector.c',       # box_pack_start->append (combo/cell-layout deprecated, present)
  lib_dir / 'dia-line-style-selector.c',  # box_pack_start->append
  lib_dir / 'dia-size-selector.c',        # box_pack_start->append, drop border_width
  lib_dir / 'widgets.c',                  # toggle button: bin/container->set_child, set_relief->set_has_frame
  lib_dir / 'prop_widgets.c',             # expander set_child, box_pack->append
  lib_dir / 'prop_sdarray_widget.c',      # box_pack->append, scrolled set_child, drop show_all
  lib_dir / 'propdialogs.c',              # box_append, label align, scrolled set_child, misc removed
  lib_dir / 'prop_text.c',                # GtkEditable, frame set_child, key controller
  lib_dir / 'message.c',                  # dialog content-area box_append, drop container_check_resize
  lib_dir / 'persistence.c',              # window geometry: size-only (GTK4 drops positioning)
  lib_dir / 'dia-arrow-chooser.c',        # GtkMenu -> GtkPopover, button-press -> clicked
  lib_dir / 'dia-line-chooser.c',         # GtkMenu -> GtkPopover, button-press -> clicked
  lib_dir / 'diapatternselector.c',       # GtkMenu/GtkArrow -> GtkMenuButton + popover

  # The cairo renderer (renders DiaRenderer ops to a cairo_t) lives in
  # lib/renderer/. Needed so the cell renderers + the canvas can draw.
  lib_dir / 'renderer' / 'diacairo-renderer.c',
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
  include_directories: include_directories('../lib', '../lib/renderer'),
  dependencies: libdia_core_deps,
  # Make calls to removed GTK4 APIs a hard error (they are only a warning by
  # default in C, so they would otherwise slip through to link failures).
  c_args: ['-DLIBDIA_COMPILATION', '-Werror=implicit-function-declaration'],
)

libdia_core_dep = declare_dependency(
  link_with: libdia_core,
  include_directories: include_directories('../lib', '../lib/renderer'),
  # Expose generated headers so dependents can #include them.
  sources: [dia_version_info_h, dia_lib_enums[1], dia_lib_private_enums[1], diamarshal[1]],
  dependencies: libdia_core_deps,
)
