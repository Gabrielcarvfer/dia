/* GTK4 port: regression test for a truncation bug in lib/connectionpoint.c's
 * find_slope_directions(). The function decides which of the four cardinal
 * connection-point directions are "open" (point away from the object) for a
 * sloped object edge, given the edge's direction vector. It classifies the
 * edge by the magnitude of its slope, dy/dx:
 *
 *   slope < 2   -> flat enough to expose a north/south direction
 *   slope > 0.5 -> steep enough to expose an east/west direction
 *
 * The slope was stored in a `gint`, so fabs(dy/dx) was truncated to a whole
 * number before the comparisons. For any edge whose slope magnitude lies in
 * (0.5, 1.0) -- roughly a 27deg..45deg edge -- the value truncated to 0, so the
 * "slope > 0.5" test wrongly failed and the east/west direction was dropped.
 * polyshape.c / beziershape.c feed every sloped vertex through this function to
 * set its connection-point directions, so connectors auto-routing to such an
 * edge were denied a horizontal approach. Storing the slope in a `real` fixes
 * it (found by auditing the connection-point snapping/direction math).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <glib.h>

#include "connectionpoint.h"

/* The regression: a +0.7 slope (between 0.5 and 1.0) must expose EAST as well
 * as NORTH. Truncation to int dropped the EAST bit. */
static void
test_slope_gentle_down_right (void)
{
  Point from = { 0.0, 0.0 };
  Point to   = { 1.0, 0.7 };
  gint dirs = find_slope_directions (from, to);

  g_assert_true (dirs & DIR_NORTH);
  g_assert_true (dirs & DIR_EAST);   /* used to be missing */
  g_assert_false (dirs & DIR_SOUTH);
  g_assert_false (dirs & DIR_WEST);
}


/* The mirror case: slope -0.7 must expose WEST as well as NORTH. */
static void
test_slope_gentle_up_right (void)
{
  Point from = { 0.0, 0.0 };
  Point to   = { 1.0, -0.7 };
  gint dirs = find_slope_directions (from, to);

  g_assert_true (dirs & DIR_NORTH);
  g_assert_true (dirs & DIR_WEST);   /* used to be missing */
  g_assert_false (dirs & DIR_SOUTH);
  g_assert_false (dirs & DIR_EAST);
}


/* A shallow slope (< 0.5) must expose only north/south, never east/west -- this
 * side of the threshold worked before and must keep working. */
static void
test_slope_shallow (void)
{
  Point from = { 0.0, 0.0 };
  Point to   = { 1.0, 0.3 };
  gint dirs = find_slope_directions (from, to);

  g_assert_true (dirs & DIR_NORTH);
  g_assert_false (dirs & DIR_EAST);
  g_assert_false (dirs & DIR_WEST);
  g_assert_false (dirs & DIR_SOUTH);
}


/* A near-diagonal slope in [1, 2) must expose both a vertical and a horizontal
 * direction. */
static void
test_slope_diagonal (void)
{
  Point from = { 0.0, 0.0 };
  Point to   = { 1.0, 1.5 };
  gint dirs = find_slope_directions (from, to);

  g_assert_true (dirs & DIR_NORTH);
  g_assert_true (dirs & DIR_EAST);
}


/* A steep slope (> 2) must expose only east/west. */
static void
test_slope_steep (void)
{
  Point from = { 0.0, 0.0 };
  Point to   = { 1.0, 2.5 };
  gint dirs = find_slope_directions (from, to);

  g_assert_true (dirs & DIR_EAST);
  g_assert_false (dirs & DIR_NORTH);
  g_assert_false (dirs & DIR_SOUTH);
}


/* Perfectly axis-aligned edges use the early-return branches; sanity-check them
 * so the test also pins that behaviour. */
static void
test_axis_aligned (void)
{
  Point origin = { 0.0, 0.0 };

  g_assert_cmpint (find_slope_directions (origin, (Point) { 1.0, 0.0 }), ==, DIR_NORTH);
  g_assert_cmpint (find_slope_directions (origin, (Point) { -1.0, 0.0 }), ==, DIR_SOUTH);
  g_assert_cmpint (find_slope_directions (origin, (Point) { 0.0, 1.0 }), ==, DIR_EAST);
  g_assert_cmpint (find_slope_directions (origin, (Point) { 0.0, -1.0 }), ==, DIR_WEST);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/port/connpoint/slope/gentle-down-right", test_slope_gentle_down_right);
  g_test_add_func ("/port/connpoint/slope/gentle-up-right", test_slope_gentle_up_right);
  g_test_add_func ("/port/connpoint/slope/shallow", test_slope_shallow);
  g_test_add_func ("/port/connpoint/slope/diagonal", test_slope_diagonal);
  g_test_add_func ("/port/connpoint/slope/steep", test_slope_steep);
  g_test_add_func ("/port/connpoint/slope/axis-aligned", test_axis_aligned);

  return g_test_run ();
}
