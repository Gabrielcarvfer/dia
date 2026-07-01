/* GTK4 port: regression test for a colour-parsing bug in lib/dia_svg.c, exposed
 * while hardening SVG/shape import fidelity (see the open "garbled SVG import"
 * reports #384 and #12). dia_svg_parse_color() has a dedicated branch for the
 * SVG "rgba(r,g,b,a)" syntax, but it ran sscanf() on "str+4" -- the offset for
 * "rgb(" (four chars), not "rgba(" (five). The leftover '(' made sscanf match
 * zero fields, so *every* rgba() colour failed to parse and the element kept
 * its default (black) colour.
 *
 * A second, latent defect on the same lines: "0xFF << 24" / "a << 24" shift
 * into the sign bit of a plain int (signed overflow, undefined behaviour, and
 * flagged by the port's default UBSan build).
 *
 * https://gitlab.gnome.org/GNOME/dia/-/issues/384
 * https://gitlab.gnome.org/GNOME/dia/-/issues/12
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <glib.h>
#include <math.h>

#include "dia-colour.h"
#include "dia_svg.h"


/* The regression: rgba() used to fail to parse entirely. */
static void
test_rgba_parses (void)
{
  Color c = { -1, -1, -1, -1 };

  g_assert_true (dia_svg_parse_color ("rgba(255,0,0,255)", &c));
  g_assert_cmpfloat (fabs (c.red   - 1.0), <, 1e-3);
  g_assert_cmpfloat (fabs (c.green - 0.0), <, 1e-3);
  g_assert_cmpfloat (fabs (c.blue  - 0.0), <, 1e-3);
}


static void
test_rgba_channels (void)
{
  Color c = { -1, -1, -1, -1 };

  g_assert_true (dia_svg_parse_color ("rgba(0,128,64,255)", &c));
  g_assert_cmpfloat (fabs (c.red   - 0.0),       <, 1e-3);
  g_assert_cmpfloat (fabs (c.green - 128 / 255.0), <, 1e-3);
  g_assert_cmpfloat (fabs (c.blue  -  64 / 255.0), <, 1e-3);
}


/* The plain rgb() form must keep working (and the alpha shift must not have
 * mangled the full-alpha value). */
static void
test_rgb_still_works (void)
{
  Color c = { -1, -1, -1, -1 };

  g_assert_true (dia_svg_parse_color ("rgb(255,0,0)", &c));
  g_assert_cmpfloat (fabs (c.red   - 1.0), <, 1e-3);
  g_assert_cmpfloat (fabs (c.green - 0.0), <, 1e-3);
  g_assert_cmpfloat (fabs (c.blue  - 0.0), <, 1e-3);

  g_assert_true (dia_svg_parse_color ("rgb(0,255,0)", &c));
  g_assert_cmpfloat (fabs (c.green - 1.0), <, 1e-3);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/port/dia-svg/color/rgba-parses", test_rgba_parses);
  g_test_add_func ("/port/dia-svg/color/rgba-channels", test_rgba_channels);
  g_test_add_func ("/port/dia-svg/color/rgb-still-works", test_rgb_still_works);

  return g_test_run ();
}
