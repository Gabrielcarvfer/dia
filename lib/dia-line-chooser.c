/* Dia -- an diagram creation/manipulation program
 * Copyright (C) 1998 Alexander Larsson
 *
 * dialinechooser.c -- Copyright (C) 1999 James Henstridge.
 *                     Copyright (C) 2004 Hubert Figuiere
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

#include <glib/gi18n-lib.h>

#include "dia-line-style-selector.h"
#include "dia-line-chooser.h"
#include "dia-line-preview.h"

struct _DiaLineChooser {
  GtkButton button;
  DiaLinePreview *preview;
  DiaLineStyle lstyle;
  double dash_length;

  GtkWidget *popover;

  DiaChangeLineCallback callback;
  gpointer user_data;

  GtkWidget *dialog;
  DiaLineStyleSelector *selector;
};

G_DEFINE_TYPE (DiaLineChooser, dia_line_chooser, GTK_TYPE_BUTTON)


static void
dia_line_chooser_dispose (GObject *object)
{
  DiaLineChooser *self = DIA_LINE_CHOOSER (object);

  g_clear_pointer (&self->popover, gtk_widget_unparent);

  G_OBJECT_CLASS (dia_line_chooser_parent_class)->dispose (object);
}


/* GTK4: the chooser is a GtkButton; clicking pops up the line-style popover. */
static void
dia_line_chooser_clicked (GtkButton *button)
{
  DiaLineChooser *self = DIA_LINE_CHOOSER (button);

  if (self->popover) {
    gtk_popover_popup (GTK_POPOVER (self->popover));
  }
}


static void
dia_line_chooser_show_dialog (GtkButton *button, DiaLineChooser *self)
{
  gtk_popover_popdown (GTK_POPOVER (self->popover));
  gtk_widget_set_visible (self->dialog, TRUE);
}


static void
dia_line_chooser_class_init (DiaLineChooserClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkButtonClass *button_class = GTK_BUTTON_CLASS (klass);

  object_class->dispose = dia_line_chooser_dispose;

  button_class->clicked = dia_line_chooser_clicked;
}


static void
dia_line_chooser_dialog_response (GtkWidget      *dialog,
                                  int             response_id,
                                  DiaLineChooser *lchooser)
{
  DiaLineStyle new_style;
  double new_dash;

  if (response_id == GTK_RESPONSE_OK) {
    dia_line_style_selector_get_linestyle (lchooser->selector,
                                           &new_style,
                                           &new_dash);

    if (new_style != lchooser->lstyle || new_dash != lchooser->dash_length) {
      lchooser->lstyle = new_style;
      lchooser->dash_length = new_dash;
      dia_line_preview_set_style (lchooser->preview, new_style);

      if (lchooser->callback)
        (* lchooser->callback) (new_style, new_dash, lchooser->user_data);
    }
  } else {
    dia_line_style_selector_set_linestyle (lchooser->selector,
                                           lchooser->lstyle,
                                           lchooser->dash_length);
  }

  gtk_widget_set_visible (lchooser->dialog, FALSE);
}


static void
dia_line_chooser_change_line_style (GtkButton *button, DiaLineChooser *lchooser)
{
  DiaLineStyle lstyle = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "line-style"));

  dia_line_chooser_set_line_style (lchooser, lstyle, lchooser->dash_length);
  gtk_popover_popdown (GTK_POPOVER (lchooser->popover));
}


void
dia_line_chooser_set_line_style (DiaLineChooser *lchooser,
                                 DiaLineStyle    lstyle,
                                 double          dashlength)
{
  if (lstyle != lchooser->lstyle) {
    dia_line_preview_set_style (lchooser->preview, lstyle);
    lchooser->lstyle = lstyle;
    dia_line_style_selector_set_linestyle (lchooser->selector,
                                           lchooser->lstyle,
                                           lchooser->dash_length);
  }

  lchooser->dash_length = dashlength;

  if (lchooser->callback)
    (* lchooser->callback) (lchooser->lstyle,
                            lchooser->dash_length,
                            lchooser->user_data);
}


static void
dia_line_chooser_dialog_ok (GtkButton *button, DiaLineChooser *lchooser)
{
  dia_line_chooser_dialog_response (lchooser->dialog, GTK_RESPONSE_OK, lchooser);
}


static void
dia_line_chooser_dialog_cancel (GtkButton *button, DiaLineChooser *lchooser)
{
  dia_line_chooser_dialog_response (lchooser->dialog, GTK_RESPONSE_CANCEL, lchooser);
}


static gboolean
dia_line_chooser_dialog_close (GtkWindow *window, DiaLineChooser *lchooser)
{
  dia_line_chooser_dialog_response (lchooser->dialog, GTK_RESPONSE_CANCEL, lchooser);

  return TRUE; /* keep the window alive for reuse */
}


static void
dia_line_chooser_init (DiaLineChooser *lchooser)
{
  GtkWidget *wid;
  GtkWidget *mi, *ln, *box;
  GtkWidget *content;
  GtkWidget *button_box;
  GtkWidget *cancel_button;
  GtkWidget *ok_button;
  int i;

  lchooser->lstyle = DIA_LINE_STYLE_SOLID;
  lchooser->dash_length = DEFAULT_LINESTYLE_DASHLEN;

  wid = dia_line_preview_new (DIA_LINE_STYLE_SOLID);
  gtk_button_set_child (GTK_BUTTON (lchooser), wid);
  lchooser->preview = DIA_LINE_PREVIEW (wid);

  /* GTK4: GtkDialog is deprecated; use a plain GtkWindow with our own
   * Cancel/OK buttons dispatching to the existing response handler. */
  lchooser->dialog = gtk_window_new ();
  gtk_window_set_title (GTK_WINDOW (lchooser->dialog), _("Line Style Properties"));
  g_signal_connect (G_OBJECT (lchooser->dialog), "close-request",
                    G_CALLBACK (dia_line_chooser_dialog_close), lchooser);

  content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_top (content, 12);
  gtk_widget_set_margin_bottom (content, 12);
  gtk_widget_set_margin_start (content, 12);
  gtk_widget_set_margin_end (content, 12);
  gtk_window_set_child (GTK_WINDOW (lchooser->dialog), content);

  wid = dia_line_style_selector_new ();
  gtk_widget_set_vexpand (wid, TRUE);
  gtk_box_append (GTK_BOX (content), wid);
  lchooser->selector = DIA_LINE_STYLE_SELECTOR (wid);

  button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_halign (button_box, GTK_ALIGN_END);
  gtk_box_append (GTK_BOX (content), button_box);

  cancel_button = gtk_button_new_with_mnemonic (_("_Cancel"));
  gtk_box_append (GTK_BOX (button_box), cancel_button);
  g_signal_connect (G_OBJECT (cancel_button), "clicked",
                    G_CALLBACK (dia_line_chooser_dialog_cancel), lchooser);

  ok_button = gtk_button_new_with_mnemonic (_("_OK"));
  gtk_widget_add_css_class (ok_button, "suggested-action");
  gtk_box_append (GTK_BOX (button_box), ok_button);
  g_signal_connect (G_OBJECT (ok_button), "clicked",
                    G_CALLBACK (dia_line_chooser_dialog_ok), lchooser);
  gtk_window_set_default_widget (GTK_WINDOW (lchooser->dialog), ok_button);

  /* GTK4: GtkMenu is gone. Build a popover with a vertical list of flat
   * buttons, each showing a line-style preview. */
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  lchooser->popover = gtk_popover_new ();
  gtk_popover_set_child (GTK_POPOVER (lchooser->popover), box);
  gtk_widget_set_parent (lchooser->popover, GTK_WIDGET (lchooser));

  for (i = 0; i <= DIA_LINE_STYLE_DOTTED; i++) {
    mi = gtk_button_new ();
    gtk_button_set_has_frame (GTK_BUTTON (mi), FALSE);
    g_object_set_data (G_OBJECT (mi),
                      "line-style",
                      GINT_TO_POINTER (i));
    ln = dia_line_preview_new (i);
    gtk_button_set_child (GTK_BUTTON (mi), ln);
    g_signal_connect (G_OBJECT (mi),
                      "clicked", G_CALLBACK (dia_line_chooser_change_line_style),
                      lchooser);
    gtk_box_append (GTK_BOX (box), mi);
  }
  mi = gtk_button_new_with_label (_("Details…"));
  gtk_button_set_has_frame (GTK_BUTTON (mi), FALSE);
  g_signal_connect (G_OBJECT (mi),
                    "clicked", G_CALLBACK (dia_line_chooser_show_dialog),
                    lchooser);
  gtk_box_append (GTK_BOX (box), mi);
}


GtkWidget *
dia_line_chooser_new (DiaChangeLineCallback callback,
                      gpointer              user_data)
{
  DiaLineChooser *chooser = g_object_new (DIA_TYPE_LINE_CHOOSER, NULL);

  chooser->callback = callback;
  chooser->user_data = user_data;

  return GTK_WIDGET (chooser);
}
