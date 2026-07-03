/* test-io.c -- Unit test for Dia's document loading (lib/dia-io.c)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <glib.h>
#include <libxml/tree.h>

#include "dialib.h"
#include "diacontext.h"
#include "dia-io.h"


typedef struct {
  const char *fixture;    /* file under tests/io/ */
  gboolean    compressed; /* expected value of the was_compressed out-param */
} LoadCase;

/* Every fixture is the same one-layer diagram, differing only in how the
 * bytes are framed on disk. All must load to a <diagram> root regardless of
 * libxml version (2.14+ no longer transparently decompresses input). */
static const LoadCase load_cases[] = {
  /* plain UTF-8 XML, first byte '<' */
  { "plain.dia",              FALSE },
  /* gzip'd, first bytes 0x1f 0x8b — must be transparently decompressed */
  { "compressed.dia",         TRUE  },
  /* Regression: valid XML with leading whitespace in the prolog (no <?xml
   * declaration), so the first byte is neither '<' nor the gzip magic. The
   * old "compressed == not '<'" sniff misrouted this into the decompressor
   * and failed the load. */
  { "leading-whitespace.dia", FALSE },
};


static void
test_io_load (gconstpointer _p)
{
  const LoadCase *tc = _p;
  char *path = g_test_build_filename (G_TEST_DIST, "io", tc->fixture, NULL);
  DiaContext *ctx = dia_context_new ("test-io");
  gboolean was_compressed = !tc->compressed; /* seed with the wrong value */
  xmlDocPtr doc = dia_io_load_document (path, ctx, &was_compressed);
  xmlNodePtr root;

  g_assert_nonnull (doc);

  root = xmlDocGetRootElement (doc);
  g_assert_nonnull (root);
  g_assert_cmpstr ((const char *) root->name, ==, "diagram");

  g_assert_cmpint (was_compressed, ==, tc->compressed);

  xmlFreeDoc (doc);
  dia_context_release (ctx);
  g_clear_pointer (&path, g_free);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  libdia_init (DIA_MESSAGE_STDERR);

  for (size_t i = 0; i < G_N_ELEMENTS (load_cases); i++) {
    char *name = g_strdup_printf ("/dia/io/load/%s", load_cases[i].fixture);

    g_test_add_data_func (name, &load_cases[i], test_io_load);

    g_clear_pointer (&name, g_free);
  }

  return g_test_run ();
}
