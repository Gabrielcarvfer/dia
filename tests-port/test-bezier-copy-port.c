/* GTK4 port: regression test for a copy bug in objects/standard/bezier.c and
 * objects/standard/beziergon.c (the same class as the already-fixed arc_copy /
 * line_copy bugs).
 *
 * A bezier object keeps a separate, heap-allocated enclosing_box that also
 * spans the off-curve control points, and only <obj>_update_data() fills it.
 * bezierline_copy()/beziergon_copy() allocated a fresh enclosing_box (so it is
 * non-NULL and dia_object_get_enclosing_box() does NOT fall back to the
 * bounding_box) and copied all the drawing fields, but never called
 * bezierline_update_data()/beziergon_update_data() -- unlike their siblings
 * polyline_copy()/zigzagline_copy(). bezierconn_copy()/beziershape_copy()
 * recompute only the bounding_box. So the copy's enclosing_box stayed at its
 * zero-initialised {0,0,0,0}, and the copy was redrawn/invalidated and selected
 * with a bogus region anchored at the origin.
 *
 * no-tracker-but-real
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <glib.h>
#include <math.h>

#include "object.h"
#include "dialib.h"
#include "register-objects.h"


static void
check_copy_enclosing_box (gconstpointer user_data)
{
  const char *type_name = user_data;
  DiaObjectType *type = object_get_type ((char *) type_name);
  Handle *h1 = NULL, *h2 = NULL;
  Point p = { 5.0, 5.0 };
  DiaObject *o, *clone;
  const DiaRectangle *eo, *ec;

  g_assert_nonnull (type);
  o = type->ops->create (&p, type->default_user_data, &h1, &h2);
  g_assert_nonnull (o);

  eo = dia_object_get_enclosing_box (o);
  /* sanity: the original has a non-degenerate box away from the origin */
  g_assert_cmpfloat (eo->right, >, eo->left);
  g_assert_cmpfloat (eo->left, >, 1.0);

  clone = o->ops->copy (o);
  g_assert_nonnull (clone);
  ec = dia_object_get_enclosing_box (clone);

  /* the copy's enclosing box must match the original's, not be {0,0,0,0} */
  g_assert_cmpfloat (fabs (ec->left   - eo->left),   <, 1e-6);
  g_assert_cmpfloat (fabs (ec->top    - eo->top),    <, 1e-6);
  g_assert_cmpfloat (fabs (ec->right  - eo->right),  <, 1e-6);
  g_assert_cmpfloat (fabs (ec->bottom - eo->bottom), <, 1e-6);

  dia_object_destroy (clone);
  dia_object_destroy (o);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  libdia_init (DIA_MESSAGE_STDERR);
  dia_port_register_objects ();

  g_test_add_data_func ("/port/bezierline/copy-enclosing-box",
                        "Standard - BezierLine", check_copy_enclosing_box);
  g_test_add_data_func ("/port/beziergon/copy-enclosing-box",
                        "Standard - Beziergon", check_copy_enclosing_box);

  return g_test_run ();
}
