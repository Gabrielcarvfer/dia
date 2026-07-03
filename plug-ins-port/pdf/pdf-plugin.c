/* Dia -- GTK4 + libadwaita port
 *
 * Built-in registrar for the poppler-based PDF import filter. The importer
 * itself is the unchanged upstream plug-ins/pdf/pdf-import.cpp (import_pdf,
 * extern "C"); this file is the uniquely-named builtin PluginInitFunc handed to
 * dia_register_builtin_plugin(), replacing the dynamic-module dia_plugin_init()
 * of plug-ins/pdf/pdf.c (which can't be statically linked alongside other
 * plug-ins that define the same symbol).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "filter.h"
#include "plug-ins.h"

/* Defined in plug-ins/pdf/pdf-import.cpp (extern "C"). */
gboolean import_pdf (const char  *filename,
                     DiagramData *dia,
                     DiaContext  *ctx,
                     void        *user_data);

static const char *pdf_extensions[] = { "pdf", NULL };

static DiaImportFilter pdf_import_filter = {
  N_("Portable Document Format"),
  pdf_extensions,
  import_pdf,
  NULL,     /* user_data */
  "pdf",    /* unique_name */
};

PluginInitResult
dia_pdf_builtin_init (PluginInfo *info)
{
  if (!dia_plugin_info_init (info, "PDF", _("PDF import filter"),
                             NULL, NULL)) {
    return DIA_PLUGIN_INIT_ERROR;
  }

  filter_register_import (&pdf_import_filter);

  return DIA_PLUGIN_INIT_OK;
}
