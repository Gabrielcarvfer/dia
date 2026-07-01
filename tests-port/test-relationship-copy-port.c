/* GTK4 port: regression test for a copy bug in objects/ER/relationship.c.
 * relationship_copy() rebuilds the object's connection-point array by hand (the
 * generic element_copy() only copies handles, not connections), but its loop
 * copied .pos and set .object/.connected while forgetting to copy .flags -- so
 * the central connection point (connections[8], set to CP_FLAGS_MAIN in both
 * _create() and _load()) came back as plain flags==0 on the copy. The sibling
 * ER objects (attribute.c, entity.c) copy .flags in the very same loop; only
 * relationship.c dropped it. relationship_update_data() never re-sets the flag
 * (connpoint_update() writes .pos/.directions only), so a copied/pasted or
 * duplicated relationship permanently lost its main connection point, meaning a
 * line dropped on the diamond's centre no longer auto-gaps or snaps to the
 * "anyplace" main CP (CP_FLAGS_MAIN == CP_FLAG_ANYPLACE|CP_FLAG_AUTOGAP).
 *
 * no-tracker-but-real
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <glib.h>

#include "object.h"
#include "connectionpoint.h"
#include "dialib.h"
#include "register-objects.h"


#define MAIN_CP 8


/* Creating a relationship loads a font, and pango/fontconfig keep a process-
 * wide cache that LeakSanitizer reports at exit (a pre-existing system leak,
 * unrelated to this test). Suppress just those so a genuine leak in our own
 * code would still be caught. */
const char *__lsan_default_suppressions (void);

const char *
__lsan_default_suppressions (void)
{
  return "leak:libfontconfig\n"
         "leak:libpangoft2\n"
         "leak:libpango-\n"
         "leak:libglib-\n";
}


static void
test_relationship_copy_main_cp_flags (void)
{
  DiaObjectType *type = object_get_type ("ER - Relationship");
  Handle *h1 = NULL, *h2 = NULL;
  Point p = { 5.0, 5.0 };
  DiaObject *o, *clone;

  g_assert_nonnull (type);
  o = type->ops->create (&p, type->default_user_data, &h1, &h2);
  g_assert_nonnull (o);
  g_assert_cmpint (o->num_connections, >, MAIN_CP);

  /* sanity: the original's central connection point is the main one */
  g_assert_cmpint (o->connections[MAIN_CP]->flags, ==, CP_FLAGS_MAIN);

  clone = o->ops->copy (o);
  g_assert_nonnull (clone);
  g_assert_cmpint (clone->num_connections, ==, o->num_connections);

  /* the copy must keep the CP_FLAGS_MAIN flag; the bug left it at 0 */
  g_assert_cmpint (clone->connections[MAIN_CP]->flags, ==, CP_FLAGS_MAIN);

  /* every other connection point's flags must round-trip too */
  for (int i = 0; i < o->num_connections; i++) {
    g_assert_cmpint (clone->connections[i]->flags, ==,
                     o->connections[i]->flags);
  }

  dia_object_destroy (clone);
  dia_object_destroy (o);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  libdia_init (DIA_MESSAGE_STDERR);
  dia_port_register_objects ();

  g_test_add_func ("/port/relationship/copy-main-cp-flags",
                   test_relationship_copy_main_cp_flags);

  return g_test_run ();
}
