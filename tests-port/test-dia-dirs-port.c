/* GTK4 port: regression test for GNOME/dia#456 (and its Win32 twin #392) --
 * dia_relativize_filename() used a plain string-prefix check to decide whether
 * a file lives inside the diagram's directory. Two sibling directories that
 * merely share a name prefix ("/foo/bar" vs "/foo/barbaz") passed that check,
 * so the byte offset used to strip the shared prefix cut the slave path in the
 * middle of a component and returned a bogus, leading-characters-chopped path
 * (e.g. "/foo/barbaz/img.png" -> "/img.png"). That broken "relative" path was
 * then written into the .dia / SVG, so the embedded image was lost on reload.
 *
 * The relativization must only strip the prefix when it ends on a path
 * component boundary, otherwise it must give up (return NULL) so the caller
 * falls back to the absolute path.
 *
 * https://gitlab.gnome.org/GNOME/dia/-/issues/456
 * https://gitlab.gnome.org/GNOME/dia/-/issues/392
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <glib.h>

#include "dia_dirs.h"


static void
test_relativize_same_dir (void)
{
  /* image next to the diagram -> just the basename */
  char *rel = dia_relativize_filename ("/foo/bar/diagram.dia",
                                       "/foo/bar/img.png");
  g_assert_cmpstr (rel, ==, "img.png");
  g_free (rel);
}


static void
test_relativize_subdir (void)
{
  /* image in a subdirectory of the diagram -> subpath */
  char *rel = dia_relativize_filename ("/foo/bar/diagram.dia",
                                       "/foo/bar/sub/img.png");
  g_assert_cmpstr (rel, ==, "sub/img.png");
  g_free (rel);
}


static void
test_relativize_sibling_prefix (void)
{
  /* The regression: "/foo/barbaz" is a *sibling* of "/foo/bar", not a child,
   * even though its name starts with "bar". It must not be relativized (the
   * result used to be the corrupt "/img.png"). */
  char *rel = dia_relativize_filename ("/foo/bar/diagram.dia",
                                       "/foo/barbaz/img.png");
  g_assert_null (rel);
}


static void
test_relativize_unrelated (void)
{
  /* completely unrelated tree -> not relativizable */
  char *rel = dia_relativize_filename ("/foo/bar/diagram.dia",
                                       "/other/img.png");
  g_assert_null (rel);
}


static void
test_relativize_roundtrip (void)
{
  /* When relativize succeeds, absolutize(master, rel) must resolve back to the
   * original file (this is exactly what image.c save/load relies on). The
   * caller only absolutizes a *relative* result; a NULL result means the
   * absolute path is stored verbatim, so there is nothing to round-trip. */
  const char *master = "/foo/bar/diagram.dia";
  const char *slaves[] = {
    "/foo/bar/img.png",
    "/foo/bar/sub/img.png",
  };

  for (gsize i = 0; i < G_N_ELEMENTS (slaves); i++) {
    char *rel = dia_relativize_filename (master, slaves[i]);
    char *abs;

    g_assert_nonnull (rel);
    abs = dia_absolutize_filename (master, rel);
    g_assert_cmpstr (abs, ==, slaves[i]);

    g_free (rel);
    g_free (abs);
  }
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/port/dia-dirs/relativize/same-dir",
                   test_relativize_same_dir);
  g_test_add_func ("/port/dia-dirs/relativize/subdir",
                   test_relativize_subdir);
  g_test_add_func ("/port/dia-dirs/relativize/sibling-prefix",
                   test_relativize_sibling_prefix);
  g_test_add_func ("/port/dia-dirs/relativize/unrelated",
                   test_relativize_unrelated);
  g_test_add_func ("/port/dia-dirs/relativize/roundtrip",
                   test_relativize_roundtrip);

  return g_test_run ();
}
