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

#include "dia-arrow-preview.h"
#include "dia-arrow-selector.h"
#include "dia-size-selector.h"

/*
 * GTK4: the GtkComboBox + GtkListStore + DiaArrowCellRenderer stack is
 * deprecated. This selector is a GtkDropDown backed by a GListModel of
 * DiaArrowItem, each rendered by a DiaArrowPreview widget in the factory.
 */

#define DIA_TYPE_ARROW_ITEM (dia_arrow_item_get_type ())
G_DECLARE_FINAL_TYPE (DiaArrowItem, dia_arrow_item, DIA, ARROW_ITEM, GObject)

struct _DiaArrowItem {
  GObject parent_instance;
  ArrowType type;
};

G_DEFINE_TYPE (DiaArrowItem, dia_arrow_item, G_TYPE_OBJECT)

static void dia_arrow_item_class_init (DiaArrowItemClass *klass) {}
static void dia_arrow_item_init (DiaArrowItem *self) {}

static DiaArrowItem *
dia_arrow_item_new (ArrowType type)
{
  DiaArrowItem *item = g_object_new (DIA_TYPE_ARROW_ITEM, NULL);
  item->type = type;
  return item;
}


struct _DiaArrowSelector {
  GtkBox vbox;

  GtkBox *sizebox;
  GtkLabel *sizelabel;
  DiaSizeSelector *size;

  GtkWidget    *combo;
  GListStore   *arrow_store;
};

G_DEFINE_TYPE (DiaArrowSelector, dia_arrow_selector, GTK_TYPE_BOX)

enum {
    DAS_VALUE_CHANGED,
    DAS_LAST_SIGNAL
};

static guint das_signals[DAS_LAST_SIGNAL] = {0};


static void
dia_arrow_selector_class_init (DiaArrowSelectorClass *klass)
{
  /* The GListModel behind the GtkDropDown is owned by the drop-down
   * (transfer full), so no explicit finalize is needed. */
  das_signals[DAS_VALUE_CHANGED] = g_signal_new ("value_changed",
                                                 G_TYPE_FROM_CLASS (klass),
                                                 G_SIGNAL_RUN_FIRST,
                                                 0, NULL, NULL,
                                                 g_cclosure_marshal_VOID__VOID,
                                                 G_TYPE_NONE, 0);
}


static void
set_size_sensitivity (DiaArrowSelector *as)
{
  DiaArrowItem *active = gtk_drop_down_get_selected_item (GTK_DROP_DOWN (as->combo));

  if (active) {
    gtk_widget_set_sensitive (GTK_WIDGET (as->sizelabel),
                              active->type != ARROW_NONE);
    gtk_widget_set_sensitive (GTK_WIDGET (as->size),
                              active->type != ARROW_NONE);
  } else {
    gtk_widget_set_sensitive (GTK_WIDGET (as->sizelabel), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (as->size), FALSE);
  }
}


static void
arrow_type_change_callback (GtkDropDown *widget, GParamSpec *pspec, gpointer userdata)
{
  set_size_sensitivity (DIA_ARROW_SELECTOR (userdata));
  g_signal_emit (DIA_ARROW_SELECTOR (userdata),
                 das_signals[DAS_VALUE_CHANGED],
                 0);
}


static void
arrow_setup_item (GtkSignalListItemFactory *factory,
                  GtkListItem              *list_item,
                  gpointer                  data)
{
  gtk_list_item_set_child (list_item, dia_arrow_preview_new (ARROW_NONE, TRUE));
}


static void
arrow_bind_item (GtkSignalListItemFactory *factory,
                 GtkListItem              *list_item,
                 gpointer                  data)
{
  DiaArrowItem *item = gtk_list_item_get_item (list_item);
  GtkWidget *preview = gtk_list_item_get_child (list_item);

  dia_arrow_preview_set_arrow (DIA_ARROW_PREVIEW (preview), item->type, TRUE);
}


static void
arrow_size_change_callback(DiaSizeSelector *size, gpointer userdata)
{
  g_signal_emit (DIA_ARROW_SELECTOR (userdata),
                 das_signals[DAS_VALUE_CHANGED],
                 0);
}


static void
dia_arrow_selector_init (DiaArrowSelector *as)
{
  GtkWidget *box;
  GtkWidget *label;
  GtkWidget *size;
  GtkListItemFactory *factory;

  gtk_orientable_set_orientation (GTK_ORIENTABLE(as), GTK_ORIENTATION_VERTICAL);
  as->arrow_store = g_list_store_new (DIA_TYPE_ARROW_ITEM);

  for (int i = ARROW_NONE; i < MAX_ARROW_TYPE; ++i) {
    DiaArrowItem *item = dia_arrow_item_new (arrow_type_from_index (i));
    g_list_store_append (as->arrow_store, item);
    g_object_unref (item);
  }

  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (arrow_setup_item), NULL);
  g_signal_connect (factory, "bind", G_CALLBACK (arrow_bind_item), NULL);

  as->combo = gtk_drop_down_new (G_LIST_MODEL (as->arrow_store), NULL);
  gtk_drop_down_set_factory (GTK_DROP_DOWN (as->combo), factory);
  g_object_unref (factory);

  g_signal_connect (as->combo,
                    "notify::selected",
                    G_CALLBACK (arrow_type_change_callback),
                    as);

  gtk_box_append (GTK_BOX (as), as->combo);
  gtk_widget_set_visible (as->combo, TRUE);

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  as->sizebox = GTK_BOX(box);

  label = gtk_label_new (_("Size: "));
  as->sizelabel = GTK_LABEL (label);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_box_append (GTK_BOX (box), label);
  gtk_widget_set_visible (label, TRUE);

  size = dia_size_selector_new (0.0, 0.0);
  as->size = DIA_SIZE_SELECTOR (size);
  gtk_widget_set_hexpand (size, TRUE);
  gtk_box_append (GTK_BOX (box), size);
  gtk_widget_set_visible (size, TRUE);
  g_signal_connect (size,
                    "value-changed", G_CALLBACK (arrow_size_change_callback),
                    as);

  set_size_sensitivity (as);
  gtk_widget_set_hexpand (box, TRUE);
  gtk_box_append (GTK_BOX (as), box);

  gtk_widget_set_visible (box, TRUE);
}


GtkWidget *
dia_arrow_selector_new (void)
{
  return g_object_new (DIA_TYPE_ARROW_SELECTOR, NULL);
}


Arrow
dia_arrow_selector_get_arrow (DiaArrowSelector *as)
{
  Arrow at;
  DiaArrowItem *active = gtk_drop_down_get_selected_item (GTK_DROP_DOWN (as->combo));

  if (active) {
    at.type = active->type;
  } else {
    at.type = ARROW_NONE;
  }

  dia_size_selector_get_size (as->size, &at.width, &at.length);

  return at;
}


void
dia_arrow_selector_set_arrow (DiaArrowSelector *as,
                              Arrow             arrow)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (as->arrow_store));

  for (guint i = 0; i < n; i++) {
    DiaArrowItem *item = g_list_model_get_item (G_LIST_MODEL (as->arrow_store), i);
    gboolean match = item->type == arrow.type;
    g_object_unref (item);
    if (match) {
      gtk_drop_down_set_selected (GTK_DROP_DOWN (as->combo), i);
      break;
    }
  }

  dia_size_selector_set_size (DIA_SIZE_SELECTOR (as->size), arrow.width, arrow.length);
}
