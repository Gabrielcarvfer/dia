/* GTK4 port: regression test for GNOME/dia#363 -- a UML class template's formal
 * parameter is drawn as "<name> : <type>" only when a type/kind is actually
 * given. uml_formal_parameter_get_string() keyed the ":" on "type != NULL", but
 * the properties dialog stores an *empty string* (not NULL) for a parameter
 * typed with a name and a blank type, so a plain type parameter "T" was drawn
 * as "T:" -- a dangling colon that is neither UML-conformant nor expected.
 *
 * The type parameter's kind is optional (uml.h: "type; Can be NULL => Type
 * parameter"); an empty string must behave exactly like NULL.
 *
 * https://gitlab.gnome.org/GNOME/dia/-/issues/363
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <glib.h>

#include "uml.h"


static char *
formal_string (const char *name, const char *type)
{
  UMLFormalParameter *p = uml_formal_parameter_new ();
  char *s;

  p->name = name ? g_strdup (name) : NULL;
  p->type = type ? g_strdup (type) : NULL;
  s = uml_formal_parameter_get_string (p);
  uml_formal_parameter_unref (p);
  return s;
}


/* The regression: an empty (but non-NULL) type must not add a ":". */
static void
test_empty_type_no_colon (void)
{
  char *s = formal_string ("T", "");
  g_assert_cmpstr (s, ==, "T");
  g_free (s);
}


/* A NULL type is a pure type parameter -- also just the name. */
static void
test_null_type_no_colon (void)
{
  char *s = formal_string ("T", NULL);
  g_assert_cmpstr (s, ==, "T");
  g_free (s);
}


/* A real type still round-trips as "name:type". */
static void
test_real_type_kept (void)
{
  char *s = formal_string ("T", "int");
  g_assert_cmpstr (s, ==, "T:int");
  g_free (s);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/port/uml/formalparam/empty-type-no-colon",
                   test_empty_type_no_colon);
  g_test_add_func ("/port/uml/formalparam/null-type-no-colon",
                   test_null_type_no_colon);
  g_test_add_func ("/port/uml/formalparam/real-type-kept",
                   test_real_type_kept);

  return g_test_run ();
}
