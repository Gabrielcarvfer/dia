/* GTK4 port: regression test for a memory leak in DiaContext's message log.
 *
 * dia_context_add_message()/..._with_errno() built the message with
 * g_strdup_vprintf() and then handed it to g_strv_builder_add(), which *copies*
 * its argument -- so the freshly allocated string leaked on every diagnostic
 * emitted while loading/saving (broken files emit a lot of them). The fix takes
 * ownership with g_strv_builder_take(); this test drives both entry points and
 * relies on the ASan/LSan build to flag any remaining leak.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <glib.h>

#include "diacontext.h"
#include "dialib.h"


static void
test_add_message_no_leak (void)
{
  DiaContext *ctx = dia_context_new ("test-diacontext");

  dia_context_set_filename (ctx, "broken.dia");

  for (int i = 0; i < 8; i++) {
    dia_context_add_message (ctx, "diagnostic number %d for %s", i, "obj");
    dia_context_add_message_with_errno (ctx, 0, "errno diagnostic %d", i);
  }

  /* release flushes and frees the accumulated messages; anything the add
   * helpers over-allocated is what LSan will catch. */
  dia_context_release (ctx);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  /* send the flushed messages to stderr rather than the GUI handler */
  libdia_init (DIA_MESSAGE_STDERR);

  g_test_add_func ("/port/diacontext/add-message/no-leak",
                   test_add_message_no_leak);

  return g_test_run ();
}
