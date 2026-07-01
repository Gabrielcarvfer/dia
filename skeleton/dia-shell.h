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

typedef struct _DiagramData DiagramData;

/* Returns the window content: an AdwToolbarView with the header bar, tool
 * toolbar, the toolbox/canvas/layers area, and the statusbar. */
GtkWidget *dia_shell_new (void);

/* Render a whole diagram to a file, format chosen by extension (.pdf/.svg,
 * else PNG). Shared by the GUI Export action and the --export CLI option. */
gboolean diagram_export_file (DiagramData *data, const char *path);

/* Headless: load @infile (.dia) and export to @outfile (format by extension).
 * Returns a process exit code. Used by `dia --export`. */
int dia_shell_export_cli (const char *const *infiles, int n_infiles,
                          const char *outfile, const char *outdir,
                          const char *indir, const char *fmt,
                          const char *size, const char *layers);

G_END_DECLS
