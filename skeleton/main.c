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

#define DIA_APP_ID "org.gnome.Dia"

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

static GtkWidget *
build_primary_menu_button (void)
{
  GtkWidget *button = gtk_menu_button_new ();
  GMenu *menu = g_menu_new ();

  g_menu_append (menu, _("_About Dia"), "app.about");
  g_menu_append (menu, _("_Quit"), "app.quit");

  gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (button), "open-menu-symbolic");
  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (button),
                                  G_MENU_MODEL (menu));
  gtk_widget_set_tooltip_text (button, _("Main Menu"));

  g_object_unref (menu);

  return button;
}

static void
on_activate (GApplication *app,
             gpointer      user_data)
{
  GtkWidget *window;
  GtkWidget *toolbar_view;
  GtkWidget *header_bar;
  GtkWidget *status_page;

  /* If we are activated again, just present the existing window. */
  window = GTK_WIDGET (gtk_application_get_active_window (GTK_APPLICATION (app)));
  if (window != NULL) {
    gtk_window_present (GTK_WINDOW (window));
    return;
  }

  window = adw_application_window_new (GTK_APPLICATION (app));
  gtk_window_set_title (GTK_WINDOW (window), "Dia");
  gtk_window_set_default_size (GTK_WINDOW (window), 1024, 768);

  header_bar = adw_header_bar_new ();
  adw_header_bar_pack_end (ADW_HEADER_BAR (header_bar),
                           build_primary_menu_button ());

  status_page = adw_status_page_new ();
  adw_status_page_set_icon_name (ADW_STATUS_PAGE (status_page), DIA_APP_ID);
  adw_status_page_set_title (ADW_STATUS_PAGE (status_page), "Dia");
  adw_status_page_set_description (ADW_STATUS_PAGE (status_page),
      _("GTK4 / libadwaita port — foundation skeleton.\n"
        "The diagram canvas and tools are being migrated incrementally."));

  toolbar_view = adw_toolbar_view_new ();
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar_view), header_bar);
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar_view), status_page);

  adw_application_window_set_content (ADW_APPLICATION_WINDOW (window),
                                      toolbar_view);

  gtk_window_present (GTK_WINDOW (window));
}

int
main (int argc, char *argv[])
{
  g_autoptr (AdwApplication) app = NULL;
  int status;

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
