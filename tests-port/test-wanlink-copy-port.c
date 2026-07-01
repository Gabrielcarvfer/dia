/* GTK4 port: regression test for a copy bug in objects/network/wanlink.c.
 * A WanLink caches a derived polygon (wanlink->poly) that both wanlink_draw()
 * and wanlink_distance_from() read directly; only wanlink_update_data() fills
 * it, and create/load/move/move_handle all call it. wanlink_copy() did not:
 * it copied the width/colours (and object_copy() carried over the bounding
 * box, hiding the problem) but left poly at its zero-initialised {0,0}. So a
 * freshly copied/pasted WAN Link rendered and hit-tested as a degenerate point
 * at the origin until its first move -- e.g. distance_from() at the object's
 * own centre returned ~9 (the distance to the origin) instead of 0.
 *
 * The sibling connection object bus.c copies its derived parallel_points[]
 * array explicitly for exactly this reason; wanlink.c copied neither the array
 * nor called update_data. Calling wanlink_update_data() on the copy fixes it.
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


const char *__lsan_default_suppressions (void);

/* Creating a WAN Link pulls in pango/fontconfig indirectly (attributes); the
 * process-wide font cache is a pre-existing system leak LeakSanitizer reports
 * at exit. Suppress just those so a real leak in our own code still trips. */
const char *
__lsan_default_suppressions (void)
{
  return "leak:libfontconfig\n"
         "leak:libpangoft2\n"
         "leak:libpango-\n"
         "leak:libglib-\n";
}


static void
test_wanlink_copy_polygon (void)
{
  DiaObjectType *type = object_get_type ("Network - WAN Link");
  Handle *h1 = NULL, *h2 = NULL;
  Point p = { 5.0, 5.0 };
  DiaObject *o, *clone;
  Point center;
  real d_src, d_clone;

  g_assert_nonnull (type);
  o = type->ops->create (&p, type->default_user_data, &h1, &h2);
  g_assert_nonnull (o);

  /* A point in the middle of the object: it lies on/inside the rendered
   * polygon, so distance_from() is ~0 for a correctly-built object. */
  center.x = (o->bounding_box.left + o->bounding_box.right) / 2.0;
  center.y = (o->bounding_box.top + o->bounding_box.bottom) / 2.0;

  d_src = o->ops->distance_from (o, &center);
  g_assert_cmpfloat (d_src, <, 1e-6);

  clone = o->ops->copy (o);
  g_assert_nonnull (clone);

  /* The copy's bounding box matches (object_copy carries it), so the centre is
   * the same point -- and the copy must hit-test identically. With the bug the
   * copy's polygon was {0,0} and this returned ~9 (distance to the origin). */
  g_assert_cmpfloat (fabs (clone->bounding_box.left  - o->bounding_box.left),  <, 1e-9);
  g_assert_cmpfloat (fabs (clone->bounding_box.right - o->bounding_box.right), <, 1e-9);

  d_clone = clone->ops->distance_from (clone, &center);
  g_assert_cmpfloat (d_clone, <, 1e-6);
  g_assert_cmpfloat (fabs (d_clone - d_src), <, 1e-6);

  dia_object_destroy (clone);
  dia_object_destroy (o);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  libdia_init (DIA_MESSAGE_STDERR);
  dia_port_register_objects ();

  g_test_add_func ("/port/wanlink/copy-polygon", test_wanlink_copy_polygon);

  return g_test_run ();
}
