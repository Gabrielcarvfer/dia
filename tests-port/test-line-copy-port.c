/* GTK4 port: regression test for a copy bug in objects/standard/line.c.
 * Like arc_copy(), line_copy() called line_update_data() on the *source* line
 * instead of the freshly-created copy. line_update_data() is what places the
 * "Standard - Line" mid-line connection point (via connpointline_putonaline())
 * and recomputes the handles, so a copied/pasted line ended up with its
 * connection point left at the origin (0,0) rather than at the line's midpoint.
 * Anything snapping to a pasted line's connection point would jump to (0,0).
 * Updating the copy fixes it.
 *
 * no-tracker-but-real
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <glib.h>
#include <math.h>

#include "object.h"
#include "handle.h"
#include "dia-object-change.h"
#include "dialib.h"
#include "register-objects.h"


static void
test_line_copy_connection_point (void)
{
  DiaObjectType *type = object_get_type ("Standard - Line");
  Handle *h1 = NULL, *h2 = NULL;
  Point p = { 5.0, 5.0 };
  Point end = { 12.0, 9.0 };
  DiaObject *o, *clone;
  DiaObjectChange *ch;

  g_assert_nonnull (type);
  o = type->ops->create (&p, type->default_user_data, &h1, &h2);
  g_assert_nonnull (o);
  g_assert_cmpint (o->num_connections, >, 0);

  /* make a non-trivial line so the mid connection point is well away from 0,0 */
  ch = o->ops->move_handle (o, h2, &end, NULL, HANDLE_MOVE_USER_FINAL, 0);
  g_clear_pointer (&ch, dia_object_change_unref);

  clone = o->ops->copy (o);
  g_assert_nonnull (clone);
  g_assert_cmpint (clone->num_connections, ==, o->num_connections);

  /* every connection point of the copy must match the source (the mid point of
   * this line is (8.5, 7); pre-fix the copy's was left at the origin). */
  for (int i = 0; i < o->num_connections; i++) {
    g_assert_cmpfloat (fabs (clone->connections[i]->pos.x
                             - o->connections[i]->pos.x), <, 1e-6);
    g_assert_cmpfloat (fabs (clone->connections[i]->pos.y
                             - o->connections[i]->pos.y), <, 1e-6);
  }
  /* and it must not be the tell-tale (0,0) */
  g_assert_cmpfloat (fabs (clone->connections[0]->pos.x)
                     + fabs (clone->connections[0]->pos.y), >, 1.0);

  dia_object_destroy (clone);
  dia_object_destroy (o);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  libdia_init (DIA_MESSAGE_STDERR);
  dia_port_register_objects ();

  g_test_add_func ("/port/line/copy-connection-point",
                   test_line_copy_connection_point);

  return g_test_run ();
}
