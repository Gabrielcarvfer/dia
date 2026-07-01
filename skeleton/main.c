/* Dia -- a diagram creation/manipulation program
 *
 * GTK4 + libadwaita port: foundation skeleton.
 *
 * This is a minimal, self-contained AdwApplication shell. It exists so the
 * GTK4/libadwaita build wiring can be compiled and launched while the rest of
 * the (GTK3) tree is migrated. It deliberately depends on nothing from lib/
 * or app/ yet -- those are ported incrementally (see PORTING-GTK4.md), and as
 * widgets land they get wired into the window built here.
 *
 * Copyright (C) 1998 Alexander Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <adwaita.h>
#include <glib/gi18n.h>

#include "dia-shell.h"

#define DIA_APP_ID "org.gnome.Dia"

/* Builds carry -fsanitize=address,undefined (see meson.build). Bake the runtime
 * defaults into the binary so even shipped artifacts behave sanely: keep the
 * valuable checks (heap overflow, use-after-free, undefined behaviour) aborting
 * on a real bug, but disable LeakSanitizer — GTK/GLib hold caches until exit,
 * which would otherwise spam every run. The sanitizer runtime looks these weak
 * symbols up by name; they are harmless no-ops if a sanitizer isn't linked. */
const char *__asan_default_options (void);
const char *__asan_default_options (void)
{
  return "detect_leaks=0:halt_on_error=1:abort_on_error=1";
}

const char *__ubsan_default_options (void);
const char *__ubsan_default_options (void)
{
  return "halt_on_error=1:print_stacktrace=1";
}

static void
on_about_action (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
  GtkApplication *app = GTK_APPLICATION (user_data);
  GtkWindow *window = gtk_application_get_active_window (app);

  AdwAboutDialog *about = ADW_ABOUT_DIALOG (adw_about_dialog_new ());

  adw_about_dialog_set_application_name (about, "Dia");
  adw_about_dialog_set_application_icon (about, DIA_APP_ID);
  adw_about_dialog_set_developer_name (about, "The Dia Developers");
  adw_about_dialog_set_version (about, PACKAGE_VERSION);
  adw_about_dialog_set_website (about, "https://wiki.gnome.org/Apps/Dia");
  adw_about_dialog_set_license_type (about, GTK_LICENSE_GPL_2_0);
  adw_about_dialog_set_comments (about,
      _("Diagram creation and manipulation program.\n\n"
        "GTK4 / libadwaita port — foundation skeleton."));

  adw_dialog_present (ADW_DIALOG (about), GTK_WIDGET (window));
}

static void
on_quit_action (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
  GApplication *app = G_APPLICATION (user_data);

  g_application_quit (app);
}

static const GActionEntry app_actions[] = {
  { "about", on_about_action, NULL, NULL, NULL },
  { "quit",  on_quit_action,  NULL, NULL, NULL },
};

static void
on_activate (GApplication *app,
             gpointer      user_data)
{
  GtkWidget *window;

  /* Use our app icon (looked up by name in the icon theme) for every window —
   * without this GTK shows no icon in the titlebar/taskbar/dock. The icon
   * (org.gnome.Dia.svg) ships in hicolor/scalable/apps of each bundle. */
  gtk_window_set_default_icon_name (DIA_APP_ID);

  /* If we are activated again, just present the existing window. */
  window = GTK_WIDGET (gtk_application_get_active_window (GTK_APPLICATION (app)));
  if (window != NULL) {
    gtk_window_present (GTK_WINDOW (window));
    return;
  }

  window = adw_application_window_new (GTK_APPLICATION (app));
  gtk_window_set_title (GTK_WINDOW (window), "Dia");
  /* Default size is the unmaximized fallback; start maximized. */
  gtk_window_set_default_size (GTK_WINDOW (window), 1100, 720);
  gtk_window_maximize (GTK_WINDOW (window));

  adw_application_window_set_content (ADW_APPLICATION_WINDOW (window),
                                      dia_shell_new ());

  gtk_window_present (GTK_WINDOW (window));
}

int
main (int argc, char *argv[])
{
  g_autoptr (AdwApplication) app = NULL;
  g_autoptr (GOptionContext) ctx = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *export_to = NULL;
  g_autofree char *filter = NULL;
  g_autofree char *size = NULL;
  g_autofree char *layers = NULL;
  g_autofree char *input_dir = NULL;
  g_autofree char *output_dir = NULL;
  gboolean show_version = FALSE;
  gboolean list_filters = FALSE;
  int status;

  /* Command-line options (upstream-compatible subset). --export renders the
   * input .dia to PNG/PDF/SVG headlessly and exits — no display required. */
  const GOptionEntry entries[] = {
    { "export", 'e', 0, G_OPTION_ARG_FILENAME, &export_to,
      "Export the input diagram to FILE and exit", "FILE" },
    { "filter", 't', 0, G_OPTION_ARG_STRING, &filter,
      "Select the export format (png/svg/pdf); default: from FILE's extension",
      "TYPE" },
    { "size", 's', 0, G_OPTION_ARG_STRING, &size,
      "Export at this pixel size (WxH; either side may be omitted)", "WxH" },
    { "show-layers", 'L', 0, G_OPTION_ARG_STRING, &layers,
      "Export only these layers (names, indices, or index ranges X-Y)",
      "LAYER,..." },
    { "input-directory", 'I', 0, G_OPTION_ARG_FILENAME, &input_dir,
      "Directory to resolve input files against", "DIR" },
    { "output-directory", 'O', 0, G_OPTION_ARG_FILENAME, &output_dir,
      "Directory to write exported files into", "DIR" },
    { "list-filters", 0, 0, G_OPTION_ARG_NONE, &list_filters,
      "List the supported export formats and exit", NULL },
    { "version", 'v', 0, G_OPTION_ARG_NONE, &show_version,
      "Show the version and exit", NULL },
    { NULL }
  };

  ctx = g_option_context_new ("[INPUT.dia] — GTK4 Dia");
  g_option_context_add_main_entries (ctx, entries, NULL);
  if (!g_option_context_parse (ctx, &argc, &argv, &error)) {
    g_printerr ("dia: %s\n", error->message);
    return 2;
  }
  if (show_version) {
    g_print ("Dia (GTK4 port) %s\n", PACKAGE_VERSION);
    return 0;
  }
  if (list_filters) {
    g_print ("Supported export formats:\n"
             "  png  Portable Network Graphics (cairo)\n"
             "  svg  Scalable Vector Graphics (cairo)\n"
             "  pdf  Portable Document Format (cairo)\n");
    return 0;
  }
  if (export_to || output_dir) {
    if (argc < 2) {
      g_printerr ("dia: nothing to export — give at least one input .dia file\n");
      return 2;
    }
    if (filter && g_ascii_strcasecmp (filter, "png") != 0
        && g_ascii_strcasecmp (filter, "svg") != 0
        && g_ascii_strcasecmp (filter, "pdf") != 0) {
      g_printerr ("dia: unknown export filter '%s' "
                  "(try --list-filters)\n", filter);
      return 2;
    }
    if (export_to && argc - 1 > 1) {
      g_printerr ("dia: --export names one output; for multiple inputs the "
                  "output names are derived (use -t/-O)\n");
    }
    return dia_shell_export_cli ((const char *const *) &argv[1], argc - 1,
                                 export_to, output_dir, input_dir,
                                 filter, size, layers);
  }

  /* WSLg's native-Wayland popovers are unreliable (the menu/popover surfaces
   * never show), so prefer the X11 (Xwayland) backend on WSL, where they work.
   * Only on WSL, and overridable by setting GDK_BACKEND yourself. */
  if (g_file_test ("/mnt/wslg", G_FILE_TEST_IS_DIR) ||
      g_getenv ("WSL_DISTRO_NAME") != NULL) {
    g_setenv ("GDK_BACKEND", "x11", FALSE);
  }

  /* Use GTK's native file chooser rather than the xdg-desktop-portal one,
   * which hangs in environments without a working portal (e.g. WSLg): the
   * first cancelled portal dialog leaves a stuck modal grab and the next
   * open freezes. Overridable with GTK_USE_PORTAL=1. */
  g_setenv ("GTK_USE_PORTAL", "0", FALSE);

  app = adw_application_new (DIA_APP_ID, G_APPLICATION_DEFAULT_FLAGS);

  g_action_map_add_action_entries (G_ACTION_MAP (app),
                                   app_actions,
                                   G_N_ELEMENTS (app_actions),
                                   app);
  gtk_application_set_accels_for_action (GTK_APPLICATION (app),
                                         "app.quit",
                                         (const char *[]) { "<primary>q", NULL });

  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);

  status = g_application_run (G_APPLICATION (app), argc, argv);

  return status;
}
