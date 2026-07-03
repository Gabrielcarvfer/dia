# GTK4 port: import/export filter plug-ins.
#
# libdia-core is a STATIC library, so a dlopen'd module would get its own copy
# of libdia's global registries (object types, filters) rather than sharing the
# app's. Plug-ins are therefore built as static libraries linked straight into
# the dia binary, each exposing a uniquely-named builtin PluginInitFunc that
# skeleton/dia-shell.c hands to dia_register_builtin_plugin() under the same
# HAVE_* guard used to select it here.
#
# port_plugin_libs / port_plugin_deps are consumed by skeleton/meson.build.
# They must always be defined (possibly empty) so the skeleton can reference
# them unconditionally.

port_plugin_libs = []
port_plugin_deps = []

plugins_dir = meson.project_source_root() / 'plug-ins'

# --- PDF import (poppler) ---------------------------------------------------
# HAVE_POPPLER is set in the top-level optional-deps probe; keep the guard in
# skeleton/dia-shell.c (dia_shell_init_plugins) in sync with this condition.
if libpoppler_dep.found() and libpopplercpp_dep.found()
  port_plugin_libs += static_library(
    'dia-pdf-port',
    [
      plugins_dir / 'pdf' / 'pdf-import.cpp',
      'pdf' / 'pdf-plugin.c',
    ],
    dependencies: [
      libdia_core_dep,
      libgtk_dep,          # GdkPixbuf for embedded raster images
      libpoppler_dep,
      libpopplercpp_dep,
      config_dep,
    ],
  )
  port_plugin_deps += [libpoppler_dep, libpopplercpp_dep]
endif
