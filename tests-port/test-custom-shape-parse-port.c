/* GTK4 port: regression test for a NULL-dereference crash in the custom-shape
 * XML parser. objects/custom/shape_info.c parses <svg:polyline>/<svg:polygon>
 * by reading their "points" attribute with xmlGetProp() and then walking the
 * returned string with "while (tmp[0] != '\0')". xmlGetProp() returns NULL when
 * the attribute is absent, so a shape whose polyline/polygon carries no "points"
 * (odd, but a plausible hand-edited .shape) dereferenced a NULL pointer and
 * crashed Dia while loading the shape -- the same "don't crash on broken input"
 * category as GNOME/dia#140. A "tmp &&" guard makes the empty element parse to a
 * zero-point poly instead. This test also checks a well-formed polyline still
 * parses, so the guard did not break normal parsing.
 *
 * no-tracker-but-real
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <glib.h>
#include <glib/gstdio.h>

#include "dialib.h"
#include "shape_info.h"


/* Write @content to a fresh .shape file in a temp dir and load it. The path is
 * returned via @path_out (caller frees) so the temp dir can be removed. */
static ShapeInfo *
load_shape_from_string (const char *content, char **path_out)
{
  char *dir = g_dir_make_tmp ("dia-shape-test-XXXXXX", NULL);
  char *path = g_build_filename (dir, "test.shape", NULL);
  ShapeInfo *info;

  g_assert_true (g_file_set_contents (path, content, -1, NULL));
  info = shape_info_load (path);

  *path_out = path;
  g_free (dir);
  return info;
}


static GraphicElement *
find_element (ShapeInfo *info, GraphicElementType type)
{
  for (GList *l = info->display_list; l; l = l->next) {
    GraphicElement *el = l->data;
    if (el->type == type)
      return el;
  }
  return NULL;
}


/* The regression: a <polyline>/<polygon> with no "points" attribute must not
 * crash the loader. Reaching the asserts is the proof (pre-fix it SEGVs). */
static void
test_poly_without_points (void)
{
  const char *shape =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<shape xmlns=\"http://www.daa.com.au/~james/dia-shape-ns\"\n"
    "       xmlns:svg=\"http://www.w3.org/2000/svg\">\n"
    "  <name>Test - PolyNoPoints</name>\n"
    "  <icon>none.png</icon>\n"
    "  <connections><point x=\"0\" y=\"0\"/></connections>\n"
    "  <svg:svg>\n"
    "    <svg:polyline/>\n"
    "    <svg:polygon/>\n"
    "    <svg:rect x=\"0\" y=\"0\" width=\"2\" height=\"2\"/>\n"
    "  </svg:svg>\n"
    "</shape>\n";
  char *path = NULL;
  ShapeInfo *info = load_shape_from_string (shape, &path);
  GraphicElement *poly;

  g_assert_nonnull (info);
  g_assert_cmpstr (info->name, ==, "Test - PolyNoPoints");
  /* the empty polyline/polygon parse to zero-point polys, the rect still parses */
  poly = find_element (info, GE_POLYLINE);
  g_assert_nonnull (poly);
  g_assert_cmpint (poly->polyline.npoints, ==, 0);
  poly = find_element (info, GE_POLYGON);
  g_assert_nonnull (poly);
  g_assert_cmpint (poly->polygon.npoints, ==, 0);
  g_assert_nonnull (find_element (info, GE_RECT));

  g_unlink (path);
  g_free (path);
}


/* Positive control: a well-formed polyline still parses to the right points. */
static void
test_poly_with_points (void)
{
  const char *shape =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<shape xmlns=\"http://www.daa.com.au/~james/dia-shape-ns\"\n"
    "       xmlns:svg=\"http://www.w3.org/2000/svg\">\n"
    "  <name>Test - PolyWithPoints</name>\n"
    "  <icon>none.png</icon>\n"
    "  <connections><point x=\"0\" y=\"0\"/></connections>\n"
    "  <svg:svg>\n"
    "    <svg:polyline points=\"0,0 1,0 1,1\"/>\n"
    "  </svg:svg>\n"
    "</shape>\n";
  char *path = NULL;
  ShapeInfo *info = load_shape_from_string (shape, &path);
  GraphicElement *poly;

  g_assert_nonnull (info);
  poly = find_element (info, GE_POLYLINE);
  g_assert_nonnull (poly);
  g_assert_cmpint (poly->polyline.npoints, ==, 3);
  g_assert_cmpfloat (poly->polyline.points[0].x, ==, 0.0);
  g_assert_cmpfloat (poly->polyline.points[1].x, ==, 1.0);
  g_assert_cmpfloat (poly->polyline.points[2].y, ==, 1.0);

  g_unlink (path);
  g_free (path);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  libdia_init (DIA_MESSAGE_STDERR);

  g_test_add_func ("/port/custom-shape/poly-without-points",
                   test_poly_without_points);
  g_test_add_func ("/port/custom-shape/poly-with-points",
                   test_poly_with_points);

  return g_test_run ();
}
