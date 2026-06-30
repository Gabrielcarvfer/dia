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
 *
 * Copyright © 2019 Zander Brown <zbrown@gnome.org>
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include <gtk/gtk.h>

#include "dia-line-cell-renderer.h"
#include "renderer/diacairo.h"
#include "diarenderer.h"


#define LINEWIDTH 2

typedef struct _DiaLineCellRendererPrivate DiaLineCellRendererPrivate;
struct _DiaLineCellRendererPrivate {
  DiaLineStyle  line;
};


G_DEFINE_TYPE_WITH_PRIVATE (DiaLineCellRenderer, dia_line_cell_renderer, GTK_TYPE_CELL_RENDERER)


enum {
  PROP_0,
  PROP_LINE,
  LAST_PROP
};
static GParamSpec *pspecs[LAST_PROP] = { NULL, };


static void
dia_line_cell_renderer_get_property (GObject    *object,
                                     guint       param_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  DiaLineCellRenderer *self = DIA_LINE_CELL_RENDERER (object);
  DiaLineCellRendererPrivate *priv = dia_line_cell_renderer_get_instance_private (self);

  switch (param_id) {
    case PROP_LINE:
      g_value_set_enum (value, priv->line);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
  }
}


static void
dia_line_cell_renderer_set_property (GObject      *object,
                                     guint         param_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  DiaLineCellRenderer *self = DIA_LINE_CELL_RENDERER (object);
  DiaLineCellRendererPrivate *priv = dia_line_cell_renderer_get_instance_private (self);

  switch (param_id) {
    case PROP_LINE:
      priv->line = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}


/* GTK4: the single get_size vfunc is gone; provide preferred width/height. */
static void
dia_line_cell_renderer_get_preferred_width (GtkCellRenderer *cell,
                                            GtkWidget       *widget,
                                            gint            *minimum,
                                            gint            *natural)
{
  int xpad, ypad;

  gtk_cell_renderer_get_padding (cell, &xpad, &ypad);

  if (minimum) *minimum = xpad * 2 + 40;
  if (natural) *natural = xpad * 2 + 40;
}


static void
dia_line_cell_renderer_get_preferred_height (GtkCellRenderer *cell,
                                             GtkWidget       *widget,
                                             gint            *minimum,
                                             gint            *natural)
{
  int xpad, ypad;

  gtk_cell_renderer_get_padding (cell, &xpad, &ypad);

  if (minimum) *minimum = ypad * 2 + 20;
  if (natural) *natural = ypad * 2 + 20;
}


static void
dia_line_cell_renderer_snapshot (GtkCellRenderer      *cell,
                                 GtkSnapshot          *snapshot,
                                 GtkWidget            *widget,
                                 const GdkRectangle   *background_area,
                                 const GdkRectangle   *cell_area,
                                 GtkCellRendererState  flags)
{
  DiaLineCellRenderer *self;
  DiaLineCellRendererPrivate *priv;
  DiaRenderer *renderer;
  Point from, to;
  DiaColour colour_fg;
  GtkStyleContext *style = gtk_widget_get_style_context (widget);
  GdkRGBA fg;
  cairo_t *ctx;
  int x, y, width, height, xpad, ypad;

  /* GTK4: get_color no longer takes a state-flags argument. */
  gtk_style_context_get_color (style, &fg);

  g_return_if_fail (DIA_IS_LINE_CELL_RENDERER (cell));

  self = DIA_LINE_CELL_RENDERER (cell);
  priv = dia_line_cell_renderer_get_instance_private (self);

  gtk_cell_renderer_get_padding (cell, &xpad, &ypad);

  dia_colour_from_gdk (&colour_fg, &fg);

  x = cell_area->x + xpad;
  y = cell_area->y + ypad;
  width = cell_area->width - (xpad * 2);
  height = cell_area->height - (ypad * 2);

  to.y = from.y = y + (height / 2);
  from.x = x;
  to.x = x + width - LINEWIDTH;

  /* GTK4: render (cairo_t) -> snapshot. Get a cairo context for the cell. */
  ctx = gtk_snapshot_append_cairo (snapshot,
                                   &GRAPHENE_RECT_INIT (background_area->x,
                                                        background_area->y,
                                                        background_area->width,
                                                        background_area->height));

  renderer = g_object_new (DIA_CAIRO_TYPE_RENDERER, NULL);

  DIA_CAIRO_RENDERER (renderer)->cr = cairo_reference (ctx);
  DIA_CAIRO_RENDERER (renderer)->with_alpha = TRUE;

  dia_renderer_begin_render (DIA_RENDERER (renderer), NULL);
  dia_renderer_set_linewidth (DIA_RENDERER (renderer),
                              LINEWIDTH);
  dia_renderer_set_linestyle (DIA_RENDERER (renderer),
                              priv->line,
                              20.0);

  dia_renderer_draw_line (DIA_RENDERER (renderer),
                          &from,
                          &to,
                          &colour_fg);

  dia_renderer_end_render (DIA_RENDERER (renderer));

  g_clear_object (&renderer);
  cairo_destroy (ctx);
}


static void
dia_line_cell_renderer_class_init (DiaLineCellRendererClass *klass)
{
  GObjectClass         *object_class = G_OBJECT_CLASS (klass);
  GtkCellRendererClass *cell_class   = GTK_CELL_RENDERER_CLASS (klass);

  object_class->set_property = dia_line_cell_renderer_set_property;
  object_class->get_property = dia_line_cell_renderer_get_property;

  cell_class->get_preferred_width = dia_line_cell_renderer_get_preferred_width;
  cell_class->get_preferred_height = dia_line_cell_renderer_get_preferred_height;
  cell_class->snapshot = dia_line_cell_renderer_snapshot;

  /**
   * DiaLineCellRenderer:line:
   *
   * Since: 0.98
   */
  pspecs[PROP_LINE] =
    g_param_spec_enum ("line",
                       "Line",
                       "Line style",
                       DIA_TYPE_LINE_STYLE,
                       DIA_LINE_STYLE_DEFAULT,
                       G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, LAST_PROP, pspecs);
}


static void
dia_line_cell_renderer_init (DiaLineCellRenderer *self)
{
}


GtkCellRenderer *
dia_line_cell_renderer_new (void)
{
  return g_object_new (DIA_TYPE_LINE_CELL_RENDERER, NULL);
}
