/* GTK4 port: regression test for a division-by-zero in the custom-shape XML
 * parser's aspect-ratio derivation. objects/custom/shape_info.c:update_bounds()
 * derives the missing default dimension of a .shape from the drawing's aspect
 * ratio, e.g. default_height = (default_width / shape_width) * shape_height when
 * only <default-width> is given. When the drawing has a zero extent in the
 * dividing direction -- a purely vertical shape (width 0), a purely horizontal
 * one (height 0), or a shape with no drawable elements at all -- the code
 * divided by zero and stored a non-finite (inf/nan) value in default_width /
 * default_height. That degenerate size then propagated into a newly created
 * custom object (an "infinitely tall" element). This is the same "don't choke
 * on a plausible hand-edited .shape" category as the existing poly-without-
 * points test. The fix only derives the dimension when the divisor extent is
 * strictly positive, otherwise leaving 0.0 so the getter falls back to the
 * DEFAULT_WIDTH/DEFAULT_HEIGHT constant.
 *
 * no-tracker-but-real
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>

#include "dialib.h"
#include "shape_info.h"


static ShapeInfo *
load_shape_from_string (const char *content)
{
  char *dir = g_dir_make_tmp ("dia-shape-aspect-XXXXXX", NULL);
  char *path = g_build_filename (dir, "test.shape", NULL);
  ShapeInfo *info;

  g_assert_true (g_file_set_contents (path, content, -1, NULL));
  info = shape_info_load (path);

  g_unlink (path);
  g_free (path);
  g_free (dir);
  return info;
}


/* <default-width> given, drawing is a single vertical line (zero width): the
 * derived default height must not be a division-by-zero infinity. */
static void
test_zero_width_default (void)
{
  const char *shape =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<shape xmlns=\"http://www.daa.com.au/~james/dia-shape-ns\"\n"
    "       xmlns:svg=\"http://www.w3.org/2000/svg\">\n"
    "  <name>Test - ZeroWidthAspect</name>\n"
    "  <icon>none.png</icon>\n"
    "  <connections><point x=\"0\" y=\"0\"/></connections>\n"
    "  <default-width>3</default-width>\n"
    "  <svg:svg>\n"
    "    <svg:line x1=\"1\" y1=\"0\" x2=\"1\" y2=\"5\"/>\n"
    "  </svg:svg>\n"
    "</shape>\n";
  ShapeInfo *info = load_shape_from_string (shape);

  g_assert_nonnull (info);
  /* the raw derived value must stay finite (pre-fix it was +inf) */
  g_assert_true (isfinite (info->default_height));
  /* the getter must return a usable positive size */
  g_assert_true (isfinite (shape_info_get_default_height (info)));
  g_assert_cmpfloat (shape_info_get_default_height (info), >, 0.0);
}


/* Symmetric case: <default-height> given, drawing is a single horizontal line
 * (zero height); the derived default width must stay finite. */
static void
test_zero_height_default (void)
{
  const char *shape =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<shape xmlns=\"http://www.daa.com.au/~james/dia-shape-ns\"\n"
    "       xmlns:svg=\"http://www.w3.org/2000/svg\">\n"
    "  <name>Test - ZeroHeightAspect</name>\n"
    "  <icon>none.png</icon>\n"
    "  <connections><point x=\"0\" y=\"0\"/></connections>\n"
    "  <default-height>3</default-height>\n"
    "  <svg:svg>\n"
    "    <svg:line x1=\"0\" y1=\"1\" x2=\"5\" y2=\"1\"/>\n"
    "  </svg:svg>\n"
    "</shape>\n";
  ShapeInfo *info = load_shape_from_string (shape);

  g_assert_nonnull (info);
  g_assert_true (isfinite (info->default_width));
  g_assert_true (isfinite (shape_info_get_default_width (info)));
  g_assert_cmpfloat (shape_info_get_default_width (info), >, 0.0);
}


/* Positive control: a well-proportioned drawing (4 wide x 2 tall) with only a
 * <default-width> must still derive the height from the real aspect ratio. */
static void
test_aspect_still_derived (void)
{
  const char *shape =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<shape xmlns=\"http://www.daa.com.au/~james/dia-shape-ns\"\n"
    "       xmlns:svg=\"http://www.w3.org/2000/svg\">\n"
    "  <name>Test - NormalAspect</name>\n"
    "  <icon>none.png</icon>\n"
    "  <connections><point x=\"0\" y=\"0\"/></connections>\n"
    "  <default-width>8</default-width>\n"
    "  <svg:svg>\n"
    "    <svg:rect x=\"0\" y=\"0\" width=\"4\" height=\"2\"/>\n"
    "  </svg:svg>\n"
    "</shape>\n";
  ShapeInfo *info = load_shape_from_string (shape);

  g_assert_nonnull (info);
  /* height = (default_width / width) * height = (8 / 4) * 2 = 4 */
  g_assert_true (isfinite (info->default_height));
  g_assert_cmpfloat (fabs (info->default_height - 4.0), <, 1e-6);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  libdia_init (DIA_MESSAGE_STDERR);

  g_test_add_func ("/port/custom-shape/aspect-zero-width",
                   test_zero_width_default);
  g_test_add_func ("/port/custom-shape/aspect-zero-height",
                   test_zero_height_default);
  g_test_add_func ("/port/custom-shape/aspect-still-derived",
                   test_aspect_still_derived);

  return g_test_run ();
}
