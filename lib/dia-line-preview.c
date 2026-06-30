/* Dia -- an diagram creation/manipulation program
 * Copyright (C) 1998 Alexander Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dia-line-preview.h"

struct _DiaLinePreview {
  GtkWidget widget;
  DiaLineStyle lstyle;
};

G_DEFINE_TYPE (DiaLinePreview, dia_line_preview, GTK_TYPE_WIDGET)


static void
dia_line_preview_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
  DiaLinePreview *line = DIA_LINE_PREVIEW (widget);
  /* GTK4: snapshot coords are widget-local and the size already excludes
   * margins, so no allocation/margin juggling is needed. */
  int width = gtk_widget_get_width (widget);
  int height = gtk_widget_get_height (widget);
  double dash_list[6];
  GtkStyleContext *style;
  GdkRGBA fg;
  cairo_t *ctx;

  style = gtk_widget_get_style_context (widget);
  gtk_style_context_get_color (style, &fg);

  ctx = gtk_snapshot_append_cairo (snapshot,
                                   &GRAPHENE_RECT_INIT (0, 0, width, height));

  gdk_cairo_set_source_rgba (ctx, &fg);
  cairo_set_line_width (ctx, 2);
  cairo_set_line_cap (ctx, CAIRO_LINE_CAP_BUTT);
  cairo_set_line_join (ctx, CAIRO_LINE_JOIN_MITER);

  switch (line->lstyle) {
    case DIA_LINE_STYLE_DEFAULT:
    case DIA_LINE_STYLE_SOLID:
      cairo_set_dash (ctx, dash_list, 0, 0);
      break;
    case DIA_LINE_STYLE_DASHED:
      dash_list[0] = 10;
      dash_list[1] = 10;
      cairo_set_dash (ctx, dash_list, 2, 0);
      break;
    case DIA_LINE_STYLE_DASH_DOT:
      dash_list[0] = 10;
      dash_list[1] = 4;
      dash_list[2] = 2;
      dash_list[3] = 4;
      cairo_set_dash (ctx, dash_list, 4, 0);
      break;
    case DIA_LINE_STYLE_DASH_DOT_DOT:
      dash_list[0] = 10;
      dash_list[1] = 2;
      dash_list[2] = 2;
      dash_list[3] = 2;
      dash_list[4] = 2;
      dash_list[5] = 2;
      cairo_set_dash (ctx, dash_list, 6, 0);
      break;
    case DIA_LINE_STYLE_DOTTED:
      dash_list[0] = 2;
      dash_list[1] = 2;
      cairo_set_dash (ctx, dash_list, 2, 0);
      break;
    default:
      cairo_destroy (ctx);
      g_return_if_reached ();
  }
  cairo_move_to (ctx, 0, height / 2);
  cairo_line_to (ctx, width, height / 2);
  cairo_stroke (ctx);

  cairo_destroy (ctx);
}


static void
dia_line_preview_class_init (DiaLinePreviewClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->snapshot = dia_line_preview_snapshot;
}


static void
dia_line_preview_init (DiaLinePreview *line)
{
  /* GTK4: widgets are windowless; margins are handled by the framework. */
  gtk_widget_set_size_request (GTK_WIDGET (line), 30, 15);

  line->lstyle = DIA_LINE_STYLE_SOLID;
}


GtkWidget *
dia_line_preview_new (DiaLineStyle lstyle)
{
  DiaLinePreview *line = g_object_new (DIA_TYPE_LINE_PREVIEW, NULL);

  line->lstyle = lstyle;

  return GTK_WIDGET (line);
}


void
dia_line_preview_set_style (DiaLinePreview *line, DiaLineStyle lstyle)
{
  if (line->lstyle == lstyle) {
    return;
  }

  line->lstyle = lstyle;

  if (gtk_widget_is_drawable (GTK_WIDGET (line))) {
    gtk_widget_queue_draw (GTK_WIDGET (line));
  }
}
