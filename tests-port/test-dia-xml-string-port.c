/* GTK4 port: regression test for GNOME/dia#140 -- Dia must not crash on highly
 * broken files. data_string() strips the leading and trailing '#' that frame a
 * stored string value ("#value#"). A malformed .dia carrying a too-short
 * string node (empty or a single character, e.g. "<dia:string>x</dia:string>")
 * made "len = strlen(p) - 1" run negative and the "str[strlen(str)-1] = 0"
 * that removes the trailing '#' wrote to str[-1], a heap-buffer-overflow that
 * aborts under ASan (and corrupts the heap otherwise).
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


/* Build a <string>CONTENT</string> node, run it through data_string() and
 * return the decoded value (or NULL). */
static char *
decode_string_node (const char *content)
{
  DiaContext *ctx = dia_context_new ("test-dia-xml-string");
  xmlDocPtr doc = xmlNewDoc ((const xmlChar *) "1.0");
  xmlNodePtr node = xmlNewDocNode (doc, NULL,
                                   (const xmlChar *) "string",
                                   (const xmlChar *) content);
  char *result;

  xmlDocSetRootElement (doc, node);
  result = data_string (node, ctx);

  xmlFreeDoc (doc);
  dia_context_release (ctx);
  return result;
}


/* Well-formed values must still round-trip exactly. */
static void
test_string_wellformed (void)
{
  char *s;

  s = decode_string_node ("##");         /* the empty string */
  g_assert_cmpstr (s, ==, "");
  g_free (s);

  s = decode_string_node ("#hello#");
  g_assert_cmpstr (s, ==, "hello");
  g_free (s);

  s = decode_string_node ("#a)b(c#");    /* parentheses are not special */
  g_assert_cmpstr (s, ==, "a)b(c");
  g_free (s);
}


/* The regression: a single-character (unframed) node must not crash. Any
 * non-crashing, non-NULL result is acceptable -- the file is broken. */
static void
test_string_single_char (void)
{
  char *s = decode_string_node ("x");
  g_assert_nonnull (s);
  g_free (s);
}


static void
test_string_single_hash (void)
{
  char *s = decode_string_node ("#");
  g_assert_nonnull (s);
  g_free (s);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  /* route data_string()'s "broken file" diagnostics to stderr instead of the
   * (uninitialised) GUI message handler */
  libdia_init (DIA_MESSAGE_STDERR);

  g_test_add_func ("/port/dia-xml/string/wellformed", test_string_wellformed);
  g_test_add_func ("/port/dia-xml/string/single-char", test_string_single_char);
  g_test_add_func ("/port/dia-xml/string/single-hash", test_string_single_hash);

  return g_test_run ();
}
