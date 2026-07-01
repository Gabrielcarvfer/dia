/* GTK4 port: regression test for GNOME/dia#140 -- Dia must not crash on highly
 * broken files. The original PostgreSQL-Autodoc report crashed while loading a
 * file whose objects were missing attributes such as obj_pos, elem_corner,
 * elem_width and elem_height. Those feed the numeric data_* node parsers, and
 * several of them dereferenced the "val" attribute unconditionally:
 *
 *   - data_int()/data_enum() called atoi((char*)val) -> atoi(NULL) SEGV;
 *   - data_real() called g_ascii_strtod(NULL, NULL) (a GLib-CRITICAL);
 *   - data_point()/data_rectangle() called g_ascii_strtod(NULL, &str) and then
 *     walked the *uninitialised* endptr looking for ',' / ';' -- undefined
 *     behaviour, an out-of-bounds read that could crash.
 *
 * data_boolean() and data_color() already guarded a NULL "val"; this brings the
 * rest in line. A concrete end-to-end repro is loading a "Standard - Box" whose
 * line_style attribute is <dia:enum/> (no val): the loader used to abort in
 * data_enum(). Here we drive the parsers directly on val-less nodes and simply
 * require that they return without crashing.
 *
 * https://gitlab.gnome.org/GNOME/dia/-/issues/140
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <glib.h>
#include <libxml/tree.h>

#include "dia_xml.h"
#include "diacontext.h"
#include "dialib.h"
#include "geometry.h"


/* Build a single <NAME/> data node with no "val"/"p1"/... attribute at all. */
static xmlNodePtr
valless_node (xmlDocPtr *doc_out, const char *name)
{
  xmlDocPtr doc = xmlNewDoc ((const xmlChar *) "1.0");
  xmlNodePtr node = xmlNewDocNode (doc, NULL, (const xmlChar *) name, NULL);
  xmlDocSetRootElement (doc, node);
  *doc_out = doc;
  return node;
}


static void
test_int_missing_val (void)
{
  DiaContext *ctx = dia_context_new ("test-dia-xml-parse");
  xmlDocPtr doc;
  xmlNodePtr node = valless_node (&doc, "int");

  /* used to be atoi(NULL) -> SEGV */
  g_assert_cmpint (data_int (node, ctx), ==, 0);

  xmlFreeDoc (doc);
  dia_context_release (ctx);
}


static void
test_enum_missing_val (void)
{
  DiaContext *ctx = dia_context_new ("test-dia-xml-parse");
  xmlDocPtr doc;
  xmlNodePtr node = valless_node (&doc, "enum");

  /* the real-world crasher: Standard - Box line_style = <dia:enum/> */
  g_assert_cmpint (data_enum (node, ctx), ==, 0);

  xmlFreeDoc (doc);
  dia_context_release (ctx);
}


static void
test_real_missing_val (void)
{
  DiaContext *ctx = dia_context_new ("test-dia-xml-parse");
  xmlDocPtr doc;
  xmlNodePtr node = valless_node (&doc, "real");

  g_assert_cmpfloat (data_real (node, ctx), ==, 0.0);

  xmlFreeDoc (doc);
  dia_context_release (ctx);
}


static void
test_point_missing_val (void)
{
  DiaContext *ctx = dia_context_new ("test-dia-xml-parse");
  xmlDocPtr doc;
  xmlNodePtr node = valless_node (&doc, "point");
  Point p = { 3.0, 4.0 };

  /* the parser must not walk an uninitialised endptr; on a broken node it
   * leaves the caller's point untouched. Just require no crash. */
  data_point (node, &p, ctx);

  xmlFreeDoc (doc);
  dia_context_release (ctx);
}


static void
test_rectangle_missing_val (void)
{
  DiaContext *ctx = dia_context_new ("test-dia-xml-parse");
  xmlDocPtr doc;
  xmlNodePtr node = valless_node (&doc, "rectangle");
  DiaRectangle r = { 1.0, 2.0, 3.0, 4.0 };

  data_rectangle (node, &r, ctx);

  xmlFreeDoc (doc);
  dia_context_release (ctx);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  /* route the "broken file" diagnostics to stderr instead of the
   * (uninitialised) GUI message handler */
  libdia_init (DIA_MESSAGE_STDERR);

  g_test_add_func ("/port/dia-xml/parse/int-missing-val", test_int_missing_val);
  g_test_add_func ("/port/dia-xml/parse/enum-missing-val", test_enum_missing_val);
  g_test_add_func ("/port/dia-xml/parse/real-missing-val", test_real_missing_val);
  g_test_add_func ("/port/dia-xml/parse/point-missing-val", test_point_missing_val);
  g_test_add_func ("/port/dia-xml/parse/rectangle-missing-val", test_rectangle_missing_val);

  return g_test_run ();
}
