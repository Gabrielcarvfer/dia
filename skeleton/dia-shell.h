/* Dia -- GTK4 + libadwaita port
 *
 * dia-shell: builds the integrated-UI layout (toolbox palette, canvas area
 * with rulers/scrollbars, layer list, statusbar) as a GTK4 widget tree.
 *
 * This is the "layout" milestone of the app/ port: the real Dia window shape
 * rebuilt with GTK4 widgets. Behaviour (tools, drawing, menus) is wired up
 * incrementally on top of this skeleton.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

/* Returns the window content: an AdwToolbarView with the header bar, tool
 * toolbar, the toolbox/canvas/layers area, and the statusbar. */
GtkWidget *dia_shell_new (void);

G_END_DECLS
