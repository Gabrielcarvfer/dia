/* GTK4 port: regression test for a copy bug in objects/standard/arc.c.
 * arc_copy() allocated a fresh enclosing_box for the new arc and copied all the
 * geometry fields, but then called arc_update_data() on the *source* arc rather
 * than on the freshly-created copy (every sibling object -- zigzagline, polyline,
 * ... -- updates the new object). object_copy() copies bounding_box but never
 * the object-specific enclosing_box pointer, so the copy's enclosing box was
 * left at its zero-initialised {0,0,0,0}. The enclosing box drives selection and
 * redraw/invalidation regions, so a copied/pasted arc was tracked with a bogus
 * box anchored at the origin. Updating the copy fixes it.
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
test_arc_copy_enclosing_box (void)
{
  DiaObjectType *type = object_get_type ("Standard - Arc");
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

  g_test_add_func ("/port/arc/copy-enclosing-box",
                   test_arc_copy_enclosing_box);

  return g_test_run ();
}
