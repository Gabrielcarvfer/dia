/* GTK4 port: regression test for GNOME/dia#482 -- the per-corner radius of a
 * rounded polyline must be derived from the original polyline vertices, not
 * from a vertex that an earlier corner's fillet already trimmed.
 *
 * draw_rounded_polyline() walks the corners, and fillet() clips the shared
 * segment endpoints to the arc's tangent points. The min_radius for the *next*
 * corner used to be computed from the already-trimmed start point, so it was
 * measured against a too-short incoming segment and came out smaller than it
 * should. On a point-symmetric polyline this shows up as two corners that
 * ought to be congruent being rounded with different radii.
 *
 * We drive dia_renderer_draw_rounded_polyline() with a recording renderer that
 * captures every arc's diameter, on a polyline whose two corners are 180deg
 * rotations of each other, and assert the two arcs come out with equal radius.
 *
 * https://gitlab.gnome.org/GNOME/dia/-/issues/482
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <math.h>

#include <glib.h>
#include <glib-object.h>

#include "diarenderer.h"
#include "dialib.h"
#include "geometry.h"

/* A renderer that records the diameter (width) of every arc it is asked to
 * draw. draw_rounded_polyline's default implementation emits one draw_arc per
 * rounded corner, so this is enough to observe each corner's chosen radius. */
#define REC_TYPE_RENDERER (rec_renderer_get_type ())
G_DECLARE_FINAL_TYPE (RecRenderer, rec_renderer, REC, RENDERER, DiaRenderer)

#define MAX_ARCS 8

struct _RecRenderer {
  DiaRenderer parent;
  double      arc_width[MAX_ARCS];
  int         arc_calls;
  int         line_calls;
};

G_DEFINE_TYPE (RecRenderer, rec_renderer, DIA_TYPE_RENDERER)

static void
rec_draw_line (DiaRenderer *self, Point *start, Point *end, Color *color)
{
  REC_RENDERER (self)->line_calls++;
}

static void
rec_draw_arc (DiaRenderer *self, Point *center, real width, real height,
              real angle1, real angle2, Color *color)
{
  RecRenderer *r = REC_RENDERER (self);

  if (r->arc_calls < MAX_ARCS) {
    r->arc_width[r->arc_calls] = width;
  }
  r->arc_calls++;
}

static void
rec_renderer_class_init (RecRendererClass *klass)
{
  DiaRendererClass *rc = DIA_RENDERER_CLASS (klass);

  rc->draw_line = rec_draw_line;
  rc->draw_arc = rec_draw_arc;
}

static void
rec_renderer_init (RecRenderer *self)
{
}


static void
test_rounded_polyline_symmetric_corners (void)
{
  RecRenderer *r = g_object_new (REC_TYPE_RENDERER, NULL);
  Color fg = { 0.0, 0.0, 0.0, 1.0 };

  /* Point-symmetric (180deg about the midpoint of the short middle segment)
   * polyline. The two interior corners p1 and p2 are congruent, so a correct
   * renderer must round them with the same radius. The middle segment is short
   * enough that the requested radius (0.3) is fully usable at the first corner
   * but, if measured from the trimmed start, gets clipped at the second. */
  Point pts[4] = {
    { -0.5, -5.0 },
    { -0.5,  0.0 },
    {  0.5,  0.0 },
    {  0.5,  5.0 },
  };

  dia_renderer_draw_rounded_polyline (DIA_RENDERER (r), pts, 4, &fg, 0.3);

  g_assert_cmpint (r->arc_calls, ==, 2);    /* one arc per interior corner */
  g_assert_cmpfloat (r->arc_width[0], >, 0.0);
  /* The congruent corners must share a radius. Pre-fix the second corner came
   * out noticeably smaller (computed from the trimmed incoming segment). */
  g_assert_cmpfloat (fabs (r->arc_width[0] - r->arc_width[1]), <, 1e-6);

  g_object_unref (r);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  libdia_init (DIA_MESSAGE_STDERR);

  g_test_add_func ("/port/geometry/rounded-polyline/symmetric-corners",
                   test_rounded_polyline_symmetric_corners);

  return g_test_run ();
}
