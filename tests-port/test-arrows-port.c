/* GTK4 port: regression test for GNOME/dia#405 -- a dashed (or dotted) line
 * style must not bleed into the arrowheads. The ER/UML cardinality arrows
 * ("crow's foot" and the "exactly one" / "zero or one" hash marks) draw their
 * strokes after the dashed stem, so each must reset the line style to solid
 * before drawing. This used to be done by some of them but not all.
 *
 * We drive dia_arrow_draw() with a renderer left in a dashed state (exactly as
 * a dashed line leaves it) and assert every primitive the arrowhead emits is
 * drawn solid.
 *
 * https://gitlab.gnome.org/GNOME/dia/-/issues/405
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <glib.h>
#include <glib-object.h>

#include "arrows.h"
#include "diarenderer.h"
#include "dialib.h"
#include "geometry.h"

/* A renderer that records the line style active at each draw_line call. Every
 * arrowhead primitive ultimately decomposes to draw_line in the base renderer
 * (polyline/polygon/bezier all fan out to it), so this one override is enough
 * to observe the style of everything an arrowhead draws. */
#define REC_TYPE_RENDERER (rec_renderer_get_type ())
G_DECLARE_FINAL_TYPE (RecRenderer, rec_renderer, REC, RENDERER, DiaRenderer)

struct _RecRenderer {
  DiaRenderer  parent;
  DiaLineStyle cur_style;
  gboolean     saw_nonsolid;   /* a stroke was drawn while non-solid */
  int          line_calls;
};

G_DEFINE_TYPE (RecRenderer, rec_renderer, DIA_TYPE_RENDERER)

static void
rec_set_linestyle (DiaRenderer *self, DiaLineStyle mode, double length)
{
  REC_RENDERER (self)->cur_style = mode;
}

/* The base renderer's state setters warn "not implemented" (fatal under the
 * test harness), so stub the ones the arrowheads touch. */
static void rec_set_linewidth (DiaRenderer *s, real w) {}
static void rec_set_linecaps (DiaRenderer *s, DiaLineCaps m) {}
static void rec_set_linejoin (DiaRenderer *s, DiaLineJoin m) {}
static void rec_set_fillstyle (DiaRenderer *s, DiaFillStyle m) {}

static void
rec_draw_line (DiaRenderer *self, Point *start, Point *end, Color *color)
{
  RecRenderer *r = REC_RENDERER (self);

  r->line_calls++;
  if (r->cur_style != DIA_LINE_STYLE_SOLID) {
    r->saw_nonsolid = TRUE;
  }
}

static void
rec_renderer_class_init (RecRendererClass *klass)
{
  DiaRendererClass *rc = DIA_RENDERER_CLASS (klass);

  rc->set_linestyle = rec_set_linestyle;
  rc->set_linewidth = rec_set_linewidth;
  rc->set_linecaps = rec_set_linecaps;
  rc->set_linejoin = rec_set_linejoin;
  rc->set_fillstyle = rec_set_fillstyle;
  rc->draw_line = rec_draw_line;
}

static void
rec_renderer_init (RecRenderer *self)
{
  self->cur_style = DIA_LINE_STYLE_SOLID;
}


static void
test_arrow_ignores_dash (gconstpointer data)
{
  ArrowType type = GPOINTER_TO_INT (data);
  RecRenderer *r = g_object_new (REC_TYPE_RENDERER, NULL);
  Arrow arrow = { type, 0.8, 0.8 };
  Point to = { 5.0, 5.0 }, from = { 0.0, 5.0 };
  Color fg = { 0.0, 0.0, 0.0, 1.0 }, bg = { 1.0, 1.0, 1.0, 1.0 };

  /* Leave the renderer dashed, as a dashed stem would before its arrowhead. */
  dia_renderer_set_linestyle (DIA_RENDERER (r), DIA_LINE_STYLE_DASHED, 0.5);

  dia_arrow_draw (&arrow, DIA_RENDERER (r), &to, &from, 0.1, &fg, &bg);

  g_assert_cmpint (r->line_calls, >, 0);   /* the arrowhead actually drew */
  g_assert_false (r->saw_nonsolid);        /* ...and every stroke was solid */

  g_object_unref (r);
}


int
main (int argc, char **argv)
{
  /* The ER/UML cardinality arrows reported in #405, plus their solid-resetting
   * siblings as controls. */
  const struct { const char *name; ArrowType type; } cases[] = {
    { "crow-foot",    ARROW_CROW_FOOT },
    { "one-exactly",  ARROW_ONE_EXACTLY },
    { "one-or-none",  ARROW_ONE_OR_NONE },
    { "one-or-many",  ARROW_ONE_OR_MANY },
    { "none-or-many", ARROW_NONE_OR_MANY },
  };

  g_test_init (&argc, &argv, NULL);
  libdia_init (DIA_MESSAGE_STDERR);

  for (gsize i = 0; i < G_N_ELEMENTS (cases); i++) {
    char *path = g_strdup_printf ("/port/arrows/dash-solid/%s", cases[i].name);
    g_test_add_data_func (path, GINT_TO_POINTER (cases[i].type),
                          test_arrow_ignores_dash);
    g_free (path);
  }

  return g_test_run ();
}
