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

#include "dia-arrow-cell-renderer.h"
#include "renderer/diacairo.h"
#include "diarenderer.h"
#include "arrows.h"

#define ARROW_LINEWIDTH 2

typedef struct _DiaArrowCellRendererPrivate DiaArrowCellRendererPrivate;
struct _DiaArrowCellRendererPrivate {
  Arrow       *arrow;
  gboolean     point_left;
};


G_DEFINE_TYPE_WITH_PRIVATE (DiaArrowCellRenderer, dia_arrow_cell_renderer, GTK_TYPE_CELL_RENDERER)


enum {
  PROP_0,
  PROP_ARROW,
  PROP_POINT_LEFT,
  LAST_PROP
};
static GParamSpec *pspecs[LAST_PROP] = { NULL, };


static void
dia_arrow_cell_renderer_get_property (GObject    *object,
                                      guint       param_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  DiaArrowCellRenderer *self = DIA_ARROW_CELL_RENDERER (object);
  DiaArrowCellRendererPrivate *priv = dia_arrow_cell_renderer_get_instance_private (self);

  switch (param_id) {
    case PROP_ARROW:
      g_value_set_boxed (value, priv->arrow);
      break;

    case PROP_POINT_LEFT:
      g_value_set_boolean (value, priv->point_left);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
  }
}


static void
dia_arrow_cell_renderer_set_property (GObject      *object,
                                          guint         param_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
  DiaArrowCellRenderer *self = DIA_ARROW_CELL_RENDERER (object);
  DiaArrowCellRendererPrivate *priv = dia_arrow_cell_renderer_get_instance_private (self);

  switch (param_id) {
    case PROP_ARROW:
      g_clear_pointer (&priv->arrow, dia_arrow_free);
      priv->arrow = g_value_dup_boxed (value);
      break;

    case PROP_POINT_LEFT:
      priv->point_left = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}


static void
dia_arrow_cell_renderer_finalize (GObject *object)
{
  DiaArrowCellRenderer *self = DIA_ARROW_CELL_RENDERER (object);
  DiaArrowCellRendererPrivate *priv = dia_arrow_cell_renderer_get_instance_private (self);

  g_clear_pointer (&priv->arrow, dia_arrow_free);

  G_OBJECT_CLASS (dia_arrow_cell_renderer_parent_class)->finalize (object);
}


/* GTK4: the single get_size vfunc is gone; provide preferred width/height. */
static void
dia_arrow_cell_renderer_get_preferred_width (GtkCellRenderer *cell,
                                             GtkWidget       *widget,
                                             int             *minimum,
                                             int             *natural)
{
  int xpad, ypad;

  gtk_cell_renderer_get_padding (cell, &xpad, &ypad);

  if (minimum) *minimum = xpad * 2 + 40;
  if (natural) *natural = xpad * 2 + 40;
}


static void
dia_arrow_cell_renderer_get_preferred_height (GtkCellRenderer *cell,
                                              GtkWidget       *widget,
                                              int             *minimum,
                                              int             *natural)
{
  int xpad, ypad;

  gtk_cell_renderer_get_padding (cell, &xpad, &ypad);

  if (minimum) *minimum = ypad * 2 + 20;
  if (natural) *natural = ypad * 2 + 20;
}


static void
dia_arrow_cell_renderer_snapshot (GtkCellRenderer      *cell,
                                  GtkSnapshot          *snapshot,
                                  GtkWidget            *widget,
                                  const GdkRectangle   *background_area,
                                  const GdkRectangle   *cell_area,
                                  GtkCellRendererState  flags)
{
  DiaArrowCellRenderer *self;
  DiaArrowCellRendererPrivate *priv;
  Point from, to;
  Point move_arrow, move_line, arrow_head;
  Arrow tmp_arrow;
  Color colour_fg, colour_bg;
  GtkStyleContext *style = gtk_widget_get_style_context (widget);
  /* GTK4: gtk_style_context_get_background_color() is gone (backgrounds are
   * CSS-drawn). Approximate the hollow-arrow fill with an opaque white; the
   * cell renderers will be replaced wholesale when the tree/list views move
   * to GtkColumnView. */
  GdkRGBA bg = { 1.0, 1.0, 1.0, 1.0 };
  GdkRGBA fg;
  GdkRectangle rect;
  cairo_t *ctx;
  int xpad, ypad;
  DiaCairoRenderer *renderer;
  cairo_surface_t *surface;

  /* GTK4: get_color no longer takes a state-flags argument. */
  gtk_style_context_get_color (style, &fg);

  g_return_if_fail (DIA_IS_ARROW_CELL_RENDERER (cell));

  self = DIA_ARROW_CELL_RENDERER (cell);
  priv = dia_arrow_cell_renderer_get_instance_private (self);

  dia_colour_from_gdk (&colour_bg, &bg);
  dia_colour_from_gdk (&colour_fg, &fg);

  gtk_cell_renderer_get_padding (cell, &xpad, &ypad);

  rect.x = cell_area->x + xpad;
  rect.y = cell_area->y + ypad;
  rect.width = cell_area->width - (xpad * 2);
  rect.height = cell_area->height - (ypad * 2);

  to.y = from.y = rect.height / 2;
  if (priv->point_left) {
    from.x = rect.width - ARROW_LINEWIDTH;
    to.x = 0;
  } else {
    from.x = 0;
    to.x = rect.width - ARROW_LINEWIDTH;
  }

  /* here we must do some acrobatics and construct Arrow type
    * variable
    */
  tmp_arrow.type = priv->arrow->type;
  tmp_arrow.length = .75 * ((double) rect.height - ARROW_LINEWIDTH);
  tmp_arrow.width = .75 * ((double) rect.height - ARROW_LINEWIDTH);

  /* and here we calculate new arrow start and end of line points */
  calculate_arrow_point (&tmp_arrow,
                         &from,
                         &to,
                         &move_arrow,
                         &move_line,
                         ARROW_LINEWIDTH);
  arrow_head = to;
  point_add (&arrow_head, &move_arrow);
  point_add (&to, &move_line);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        rect.width,
                                        rect.height);

  renderer = g_object_new (DIA_CAIRO_TYPE_RENDERER, NULL);
  renderer->with_alpha = TRUE;
  renderer->surface = cairo_surface_reference (surface);

  dia_renderer_begin_render (DIA_RENDERER (renderer), NULL);
  dia_renderer_set_linewidth (DIA_RENDERER (renderer),
                              ARROW_LINEWIDTH);

  dia_renderer_draw_line (DIA_RENDERER (renderer),
                          &from,
                          &to,
                          &colour_fg);
  dia_arrow_draw (&tmp_arrow,
                  DIA_RENDERER (renderer),
                  &arrow_head,
                  &from,
                  ARROW_LINEWIDTH,
                  &colour_fg,
                  &colour_bg);

  dia_renderer_end_render (DIA_RENDERER (renderer));

  /* GTK4: render (cairo_t) -> snapshot. Get a cairo context for the cell. */
  ctx = gtk_snapshot_append_cairo (snapshot,
                                   &GRAPHENE_RECT_INIT (background_area->x,
                                                        background_area->y,
                                                        background_area->width,
                                                        background_area->height));

  cairo_set_source_surface (ctx, surface, rect.x, rect.y);
  gdk_cairo_rectangle (ctx, &rect);
  cairo_paint (ctx);

  cairo_destroy (ctx);
}


static void
dia_arrow_cell_renderer_class_init (DiaArrowCellRendererClass *klass)
{
  GObjectClass         *object_class = G_OBJECT_CLASS (klass);
  GtkCellRendererClass *cell_class   = GTK_CELL_RENDERER_CLASS (klass);

  object_class->set_property = dia_arrow_cell_renderer_set_property;
  object_class->get_property = dia_arrow_cell_renderer_get_property;
  object_class->finalize = dia_arrow_cell_renderer_finalize;

  cell_class->get_preferred_width = dia_arrow_cell_renderer_get_preferred_width;
  cell_class->get_preferred_height = dia_arrow_cell_renderer_get_preferred_height;
  cell_class->snapshot = dia_arrow_cell_renderer_snapshot;

  /**
   * DiaArrowCellRenderer:arrow:
   *
   * Since: 0.98
   */
  pspecs[PROP_ARROW] =
    g_param_spec_boxed ("arrow",
                        "Arrow",
                        "Arrow to draw",
                        DIA_TYPE_ARROW,
                        G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE);

  /**
   * DiaArrowCellRenderer:point-left:
   *
   * Since: 0.98
   */
  pspecs[PROP_POINT_LEFT] =
    g_param_spec_boolean ("point-left",
                          "Point Left",
                          "Arrow to should be pointing to the left",
                          FALSE,
                          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, LAST_PROP, pspecs);
}


static void
dia_arrow_cell_renderer_init (DiaArrowCellRenderer *self)
{
  DiaArrowCellRendererPrivate *priv = dia_arrow_cell_renderer_get_instance_private (self);

  priv->point_left = FALSE;
}


GtkCellRenderer *
dia_arrow_cell_renderer_new (void)
{
  return g_object_new (DIA_TYPE_ARROW_CELL_RENDERER, NULL);
}
