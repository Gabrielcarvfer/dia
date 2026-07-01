/* GTK4 port: regression test for an uninitialised dash length in the
 * "Standard - Path" object (lib/standard-path.c).
 *
 * StdPath is allocated with g_new0() and, unlike every other standard object
 * (arc, line, box, ellipse, bezier, ...), its stdpath_create() never seeded the
 * line-style defaults. The dash length therefore stayed 0.0. A dashed line
 * with a zero dash length is degenerate (an all-zero dash array), so a path the
 * user later switched to a dashed style would not render as intended -- and a
 * freshly created path's line-style block did not even round-trip through
 * save/load (the loader substitutes DEFAULT_LINESTYLE_DASHLEN for a solid
 * line's dash, so save-then-reload silently changed the stored 0 to 1).
 *
 * The fix seeds the style from attributes_get_default_line_style() in
 * stdpath_create(), matching the sibling objects. Found by save/load
 * round-trip fidelity auditing.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <glib.h>
#include <math.h>

#include "object.h"
#include "properties.h"
#include "prop_attr.h"
#include "dia-enums.h"
#include "dialib.h"
#include "register-objects.h"


static void
test_stdpath_dashlength_initialised (void)
{
  DiaObjectType *type = object_get_type ("Standard - Path");
  Handle *h1 = NULL, *h2 = NULL;
  Point p = { 5.0, 5.0 };
  DiaObject *o;
  Property *prop;
  LinestyleProperty *lsprop;

  g_assert_nonnull (type);
  o = type->ops->create (&p, type->default_user_data, &h1, &h2);
  g_assert_nonnull (o);

  prop = object_prop_by_name (o, "line_style");
  g_assert_nonnull (prop);
  lsprop = (LinestyleProperty *) prop;

  /* The dash length must be the standard default, not the g_new0() 0.0. */
  g_assert_cmpfloat (fabs (lsprop->dash - DEFAULT_LINESTYLE_DASHLEN), <, 1e-9);
  g_assert_cmpfloat (lsprop->dash, >, 0.0);

  prop->ops->free (prop);
  dia_object_destroy (o);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  libdia_init (DIA_MESSAGE_STDERR);
  dia_port_register_objects ();

  g_test_add_func ("/port/standard-path/dashlength-initialised",
                   test_stdpath_dashlength_initialised);

  return g_test_run ();
}
