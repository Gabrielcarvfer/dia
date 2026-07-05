/* Dia -- an diagram creation/manipulation program
 * Copyright (C) 1998 Alexander Larsson
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
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include "dia-line-preview.h"
#include "dia-line-style-selector.h"

/*
 * GTK4: the GtkComboBox + GtkListStore + DiaLineCellRenderer stack is
 * deprecated. This selector is a GtkDropDown backed by a GListModel of
 * DiaLineStyleItem, each rendered by a DiaLinePreview widget in the factory.
 */

#define DIA_TYPE_LINE_STYLE_ITEM (dia_line_style_item_get_type ())
G_DECLARE_FINAL_TYPE (DiaLineStyleItem, dia_line_style_item, DIA, LINE_STYLE_ITEM, GObject)

struct _DiaLineStyleItem {
  GObject parent_instance;
  DiaLineStyle style;
};

G_DEFINE_TYPE (DiaLineStyleItem, dia_line_style_item, G_TYPE_OBJECT)

static void dia_line_style_item_class_init (DiaLineStyleItemClass *klass) {}
static void dia_line_style_item_init (DiaLineStyleItem *self) {}

static DiaLineStyleItem *
dia_line_style_item_new (DiaLineStyle style)
{
  DiaLineStyleItem *item = g_object_new (DIA_TYPE_LINE_STYLE_ITEM, NULL);
  item->style = style;
  return item;
}


struct _DiaLineStyleSelector {
  GtkBox vbox;

  GtkLabel *lengthlabel;
  GtkSpinButton *dashlength;

  GtkWidget    *combo;
  GListStore   *line_store;
};

G_DEFINE_TYPE (DiaLineStyleSelector, dia_line_style_selector, GTK_TYPE_BOX)

enum {
  DLS_VALUE_CHANGED,
  DLS_LAST_SIGNAL
};

static guint dls_signals[DLS_LAST_SIGNAL] = { 0 };

static void
dia_line_style_selector_class_init (DiaLineStyleSelectorClass *klass)
{
  dls_signals[DLS_VALUE_CHANGED] = g_signal_new ("value-changed",
                                                 G_TYPE_FROM_CLASS (klass),
                                                 G_SIGNAL_RUN_FIRST,
                                                 0, NULL, NULL,
                                                 g_cclosure_marshal_VOID__VOID,
                                                 G_TYPE_NONE, 0);
}


static void
set_linestyle_sensitivity (DiaLineStyleSelector *fs)
{
  DiaLineStyleItem *item = gtk_drop_down_get_selected_item (GTK_DROP_DOWN (fs->combo));

  if (item) {
    DiaLineStyle line = item->style;

    gtk_widget_set_sensitive (GTK_WIDGET (fs->lengthlabel),
                              line != DIA_LINE_STYLE_SOLID);
    gtk_widget_set_sensitive (GTK_WIDGET (fs->dashlength),
                              line != DIA_LINE_STYLE_SOLID);
  } else {
    gtk_widget_set_sensitive (GTK_WIDGET (fs->lengthlabel), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (fs->dashlength), FALSE);
  }
}


static void
linestyle_type_change_callback (GtkDropDown *menu, GParamSpec *pspec, gpointer data)
{
  set_linestyle_sensitivity (DIA_LINE_STYLE_SELECTOR (data));
  g_signal_emit (DIA_LINE_STYLE_SELECTOR (data),
                 dls_signals[DLS_VALUE_CHANGED],
                 0);
}


static void
linestyle_setup_item (GtkSignalListItemFactory *factory,
                      GtkListItem              *list_item,
                      gpointer                  data)
{
  gtk_list_item_set_child (list_item, dia_line_preview_new (DIA_LINE_STYLE_SOLID));
}


static void
linestyle_bind_item (GtkSignalListItemFactory *factory,
                     GtkListItem              *list_item,
                     gpointer                  data)
{
  DiaLineStyleItem *item = gtk_list_item_get_item (list_item);
  GtkWidget *preview = gtk_list_item_get_child (list_item);

  dia_line_preview_set_style (DIA_LINE_PREVIEW (preview), item->style);
}


static void
linestyle_dashlength_change_callback (GtkSpinButton *sb, gpointer data)
{
  g_signal_emit (DIA_LINE_STYLE_SELECTOR (data),
                 dls_signals[DLS_VALUE_CHANGED],
                 0);
}


static void
dia_line_style_selector_init (DiaLineStyleSelector *fs)
{
  GtkWidget *label;
  GtkWidget *length;
  GtkWidget *box;
  GtkAdjustment *adj;
  GtkListItemFactory *factory;

  gtk_orientable_set_orientation (GTK_ORIENTABLE(fs), GTK_ORIENTATION_VERTICAL);
  fs->line_store = g_list_store_new (DIA_TYPE_LINE_STYLE_ITEM);

  for (int i = 0; i <= DIA_LINE_STYLE_DOTTED; i++) {
    DiaLineStyleItem *item = dia_line_style_item_new (i);
    g_list_store_append (fs->line_store, item);
    g_object_unref (item);
  }

  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (linestyle_setup_item), NULL);
  g_signal_connect (factory, "bind", G_CALLBACK (linestyle_bind_item), NULL);

  fs->combo = gtk_drop_down_new (G_LIST_MODEL (fs->line_store), NULL);
  gtk_drop_down_set_factory (GTK_DROP_DOWN (fs->combo), factory);
  g_object_unref (factory);

  g_signal_connect (fs->combo,
                    "notify::selected",
                    G_CALLBACK (linestyle_type_change_callback),
                    fs);

  gtk_box_append (GTK_BOX (fs), fs->combo);
  gtk_widget_set_visible (fs->combo, TRUE);

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  /*  fs->sizebox = GTK_HBOX(box); */

  label = gtk_label_new(_("Dash length: "));
  fs->lengthlabel = GTK_LABEL (label);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_box_append (GTK_BOX (box), label);
  gtk_widget_set_visible (label, TRUE);

  adj = GTK_ADJUSTMENT (gtk_adjustment_new (0.1, 0.00, 10.0, 0.1, 1.0, 0));
  length = gtk_spin_button_new (adj, DEFAULT_LINESTYLE_DASHLEN, 2);
  gtk_spin_button_set_wrap (GTK_SPIN_BUTTON(length), TRUE);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON(length), TRUE);
  fs->dashlength = GTK_SPIN_BUTTON (length);
  gtk_widget_set_hexpand (length, TRUE);
  gtk_box_append (GTK_BOX (box), length);
  gtk_widget_set_visible (length, TRUE);

  g_signal_connect (G_OBJECT (length),
                    "changed", G_CALLBACK (linestyle_dashlength_change_callback),
                    fs);

  set_linestyle_sensitivity (fs);
  gtk_widget_set_hexpand (box, TRUE);
  gtk_box_append (GTK_BOX (fs), box);
  gtk_widget_set_visible (box, TRUE);
}


GtkWidget *
dia_line_style_selector_new (void)
{
  return g_object_new (DIA_TYPE_LINE_STYLE_SELECTOR, NULL);
}


void
dia_line_style_selector_get_linestyle (DiaLineStyleSelector *fs,
                                       DiaLineStyle         *ls,
                                       double               *dl)
{
  DiaLineStyleItem *item = gtk_drop_down_get_selected_item (GTK_DROP_DOWN (fs->combo));

  if (item) {
    *ls = item->style;
  } else {
    *ls = DIA_LINE_STYLE_DEFAULT;
  }

  if (dl != NULL) {
    *dl = gtk_spin_button_get_value (fs->dashlength);
  }
}


void
dia_line_style_selector_set_linestyle (DiaLineStyleSelector *as,
                                       DiaLineStyle          linestyle,
                                       double                dashlength)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (as->line_store));

  for (guint i = 0; i < n; i++) {
    DiaLineStyleItem *item = g_list_model_get_item (G_LIST_MODEL (as->line_store), i);
    gboolean match = item->style == linestyle;
    g_object_unref (item);
    if (match) {
      gtk_drop_down_set_selected (GTK_DROP_DOWN (as->combo), i);
      break;
    }
  }

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (as->dashlength), dashlength);
}
