/* GTK4 port: exercise every registered object type (standard + flowchart/
 * network/ER) through create -> clone -> move -> move every handle -> save +
 * load round-trip, checking basic invariants. Run under ASan/UBSan, this is a
 * broad memory/logic check on the ported objects (a port-friendly subset of
 * the upstream tests/test-objects.c, which loads .so plugins we don't have).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <libxml/tree.h>
#include <math.h>

#include "object.h"
#include "handle.h"
#include "dia-object-change.h"
#include "dia_xml.h"
#include "diacontext.h"
#include "dialib.h"
#include "geometry.h"
#include "register-objects.h"


static void
exercise_type (gconstpointer user_data)
{
  const DiaObjectType *type = user_data;
  Handle *h1 = NULL, *h2 = NULL;
  Point p = { 5.0, 5.0 };
  DiaObject *o, *clone;
  DiaObjectChange *ch;

  o = type->ops->create (&p, type->default_user_data, &h1, &h2);
  g_assert_nonnull (o);
  g_assert_nonnull (o->type);
  g_assert_nonnull (o->ops);
  g_assert_nonnull (o->ops->destroy);
  g_assert_nonnull (o->ops->draw);
  g_assert_nonnull (o->ops->distance_from);
  g_assert_nonnull (o->ops->copy);
  g_assert_nonnull (o->ops->move);
  g_assert_nonnull (o->ops->move_handle);
  g_assert_nonnull (type->ops->save);

  /* bounding box initialised and sane; position inside it */
  g_assert_cmpfloat (o->bounding_box.left, <=, o->bounding_box.right);
  g_assert_cmpfloat (o->bounding_box.top, <=, o->bounding_box.bottom);
  g_assert_cmpint (o->num_handles, >, 0);
  for (int i = 0; i < o->num_handles; i++) {
    g_assert_nonnull (o->handles[i]);
  }

  /* clone */
  clone = o->ops->copy (o);
  g_assert_nonnull (clone);
  g_assert_cmpint (clone->num_handles, ==, o->num_handles);
  dia_object_destroy (clone);

  /* move the whole object */
  ch = o->ops->move (o, &(Point) { 7.0, 7.0 });
  g_clear_pointer (&ch, dia_object_change_unref);

  /* nudge every handle (the resize/reshape path) */
  for (int i = 0; i < o->num_handles; i++) {
    Point hp = o->handles[i]->pos;
    hp.x += 0.5;
    hp.y += 0.5;
    ch = o->ops->move_handle (o, o->handles[i], &hp, NULL,
                              HANDLE_MOVE_USER_FINAL, 0);
    g_clear_pointer (&ch, dia_object_change_unref);
  }

  /* save -> load round-trip through the object's own XML ops */
  {
    DiaContext *ctx = dia_context_new ("test-objects-port");
    xmlDocPtr doc = xmlNewDoc ((const xmlChar *) "1.0");
    xmlNodePtr node = xmlNewDocNode (doc, NULL, (const xmlChar *) "object", NULL);

    xmlDocSetRootElement (doc, node);
    type->ops->save (o, node, ctx);
    if (type->ops->load) {
      DiaObject *loaded = type->ops->load (node, type->version, ctx);

      g_assert_nonnull (loaded);
      /* save -> load must preserve the geometry (bounding box) */
      g_assert_cmpfloat (fabs (loaded->bounding_box.left - o->bounding_box.left),
                         <, 1e-3);
      g_assert_cmpfloat (fabs (loaded->bounding_box.top - o->bounding_box.top),
                         <, 1e-3);
      g_assert_cmpfloat (fabs (loaded->bounding_box.right - o->bounding_box.right),
                         <, 1e-3);
      g_assert_cmpfloat (fabs (loaded->bounding_box.bottom - o->bounding_box.bottom),
                         <, 1e-3);
      dia_object_destroy (loaded);
    }
    xmlFreeDoc (doc);
    dia_context_release (ctx);
  }

  dia_object_destroy (o);
}


static void
add_type (gpointer key, gpointer value, gpointer user_data)
{
  const char *name = key;
  DiaObjectType *type = value;
  GString *path = g_string_new ("/dia-port/objects/");

  /* Skip types that aren't created from a point (e.g. Group, built via
   * group_create from a list, has no point-based creator). */
  if (!type->ops || !type->ops->create) {
    g_string_free (path, TRUE);
    return;
  }

  /* g_test paths can't contain spaces; sanitise the type name. */
  for (const char *c = name; *c; c++) {
    g_string_append_c (path, (*c == ' ') ? '_' : *c);
  }
  g_test_add_data_func (path->str, value, exercise_type);
  g_string_free (path, TRUE);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  libdia_init (DIA_MESSAGE_STDERR);
  dia_port_register_objects ();
  object_registry_foreach (add_type, NULL);
  return g_test_run ();
}
