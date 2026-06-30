/* GTK4 port: regression test for the "text cut off when exporting" bug class
 * (GNOME/dia#374) as it manifests on the compiled flowchart shapes.
 *
 * The flowchart shapes have a "Text fitting" property. With the default
 * "When Needed" the shape grows to contain its label, but with "Never"
 * (DIA_TEXT_FIT_NEVER) the label may be wider than the shape and is drawn
 * overflowing it. Their *_update_data() used to size the object's bounding
 * box from the shape rectangle only (element_update_boundingbox()), never
 * unioning the text's bounding box -- so the overflowing text fell outside
 * obj->bounding_box. Because the cairo export surface is sized to the diagram
 * extents (the union of object bounding boxes), the text was then hard-clipped
 * at the image edge on PNG/SVG/PDF export.
 *
 * We create each flowchart shape with a tiny box, "Never" fitting and a long
 * label, then assert the object's bounding box is wide enough to contain the
 * label (i.e. the text is included, not clipped).
 *
 * https://gitlab.gnome.org/GNOME/dia/-/issues/374
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <math.h>

#include <glib.h>
#include <glib-object.h>

#include "object.h"
#include "handle.h"
#include "properties.h"
#include "prop_text.h"
#include "prop_inttypes.h"
#include "prop_geomtypes.h"
#include "dia-enums.h"
#include "dialib.h"
#include "geometry.h"
#include "register-objects.h"

/* 60 wide glyphs: many cm of text at any sane font size, vastly wider than the
 * 1 cm shape below, so the threshold check is robust to the test font. */
#define LONG_LABEL "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"


static void
test_flowchart_text_in_bbox (gconstpointer data)
{
  const char *type_name = data;
  DiaObjectType *type = object_get_type ((char *) type_name);
  Point p = { 5.0, 5.0 };
  Handle *h1 = NULL, *h2 = NULL;
  DiaObject *o;
  double bbox_w;

  PropDescription descs[] = {
    { "text",         PROP_TYPE_TEXT },
    { "text_fitting", PROP_TYPE_ENUM },
    { "elem_width",   PROP_TYPE_REAL },
    { "elem_height",  PROP_TYPE_REAL },
    PROP_DESC_END
  };
  GPtrArray *props;
  TextProperty *tp;
  EnumProperty *ep;
  RealProperty *rw, *rh;

  g_assert_nonnull (type);
  o = type->ops->create (&p, type->default_user_data, &h1, &h2);
  g_assert_nonnull (o);

  props = prop_list_from_descs (descs, pdtpp_true);
  g_assert_cmpuint (props->len, ==, 4);

  tp = g_ptr_array_index (props, 0);
  g_free (tp->text_data);
  tp->text_data = g_strdup (LONG_LABEL);   /* keep the shape's default font */

  ep = g_ptr_array_index (props, 1);
  ep->enum_data = DIA_TEXT_FIT_NEVER;       /* don't grow the shape to fit */

  rw = g_ptr_array_index (props, 2);
  rw->real_data = 1.0;
  rh = g_ptr_array_index (props, 3);
  rh->real_data = 1.0;

  dia_object_set_properties (o, props);     /* triggers *_update_data() */
  prop_list_free (props);

  /* The 1 cm shape stays small (fitting == Never) but the label is many cm
   * wide and fully drawn; the bounding box must contain it. Pre-fix the box
   * came out ~1 cm wide and the label was clipped on export. */
  bbox_w = o->bounding_box.right - o->bounding_box.left;
  g_assert_cmpfloat (bbox_w, >, 5.0);

  dia_object_destroy (o);
}


int
main (int argc, char **argv)
{
  const char *types[] = {
    "Flowchart - Box",
    "Flowchart - Diamond",
    "Flowchart - Ellipse",
    "Flowchart - Parallelogram",
  };

  g_test_init (&argc, &argv, NULL);
  libdia_init (DIA_MESSAGE_STDERR);
  dia_port_register_objects ();

  for (gsize i = 0; i < G_N_ELEMENTS (types); i++) {
    char *path = g_strdup_printf ("/port/objects/flowchart-text-bbox/%s",
                                  types[i] + strlen ("Flowchart - "));
    g_test_add_data_func (path, types[i], test_flowchart_text_in_bbox);
    g_free (path);
  }

  return g_test_run ();
}
