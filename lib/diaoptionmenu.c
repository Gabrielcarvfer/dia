/* Dia -- a diagram creation/manipulation program -*- c -*-
 * Copyright (C) 1998 Alexander Larsson
 *
 * Property system for dia objects/shapes.
 * Copyright (C) 2000 James Henstridge
 * Copyright (C) 2001 Cyrille Chepelov
 *
 * Copyright (C) 2010 Hans Breuer
 * Property type for affine transformation.
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
#include <config.h>

#include "diaoptionmenu.h"

/**
 * SECTION:diaoptionmenu
 * @title: DiaOptionMenu
 * @short_description: name -> value selector
 *
 * GtkOptionMenu replacement specialized for Dia's use
 * of name (menu entry) to value (int)
 *
 * GTK4: implemented as a #GtkWidget wrapping a #GtkDropDown (the deprecated
 * #GtkComboBox + #GtkListStore + cell renderers are gone). Connect to the
 * #DiaOptionMenu::changed signal to observe selection changes.
 */


/* internal name -> value item held in the drop-down's GListModel */
#define DIA_TYPE_OPTION_ITEM (dia_option_item_get_type ())
G_DECLARE_FINAL_TYPE (DiaOptionItem, dia_option_item, DIA, OPTION_ITEM, GObject)

struct _DiaOptionItem {
  GObject parent_instance;
  char   *name;
  int     value;
};

G_DEFINE_TYPE (DiaOptionItem, dia_option_item, G_TYPE_OBJECT)

static void
dia_option_item_finalize (GObject *object)
{
  DiaOptionItem *self = DIA_OPTION_ITEM (object);
  g_clear_pointer (&self->name, g_free);
  G_OBJECT_CLASS (dia_option_item_parent_class)->finalize (object);
}

static void
dia_option_item_class_init (DiaOptionItemClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = dia_option_item_finalize;
}

static void dia_option_item_init (DiaOptionItem *self) {}

static DiaOptionItem *
dia_option_item_new (const char *name, int value)
{
  DiaOptionItem *item = g_object_new (DIA_TYPE_OPTION_ITEM, NULL);
  item->name = g_strdup (name);
  item->value = value;
  return item;
}


typedef struct _DiaOptionMenuPrivate DiaOptionMenuPrivate;
struct _DiaOptionMenuPrivate {
  GtkWidget  *dropdown;
  GListStore *model;
};

G_DEFINE_TYPE_WITH_PRIVATE (DiaOptionMenu, dia_option_menu, GTK_TYPE_WIDGET)

enum {
  CHANGED,
  LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };


static void
dia_option_menu_dispose (GObject *object)
{
  DiaOptionMenu *self = DIA_OPTION_MENU (object);
  DiaOptionMenuPrivate *priv = dia_option_menu_get_instance_private (self);

  g_clear_pointer (&priv->dropdown, gtk_widget_unparent);

  G_OBJECT_CLASS (dia_option_menu_parent_class)->dispose (object);
}


static void
dia_option_menu_class_init (DiaOptionMenuClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = dia_option_menu_dispose;

  gtk_widget_class_set_layout_manager_type (GTK_WIDGET_CLASS (klass),
                                            GTK_TYPE_BIN_LAYOUT);

  /**
   * DiaOptionMenu::changed:
   *
   * Emitted when the selected item changes.
   */
  signals[CHANGED] = g_signal_new ("changed",
                                   G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_FIRST,
                                   0, NULL, NULL,
                                   g_cclosure_marshal_VOID__VOID,
                                   G_TYPE_NONE, 0);
}


static void
setup_item (GtkSignalListItemFactory *factory,
            GtkListItem              *list_item,
            gpointer                  data)
{
  GtkWidget *label = gtk_label_new (NULL);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_list_item_set_child (list_item, label);
}


static void
bind_item (GtkSignalListItemFactory *factory,
           GtkListItem              *list_item,
           gpointer                  data)
{
  DiaOptionItem *item = gtk_list_item_get_item (list_item);
  GtkWidget *label = gtk_list_item_get_child (list_item);

  gtk_label_set_text (GTK_LABEL (label), item->name);
}


static void
selection_changed (GtkDropDown *dropdown, GParamSpec *pspec, gpointer data)
{
  g_signal_emit (DIA_OPTION_MENU (data), signals[CHANGED], 0);
}


static void
dia_option_menu_init (DiaOptionMenu *self)
{
  DiaOptionMenuPrivate *priv = dia_option_menu_get_instance_private (self);
  GtkListItemFactory *factory;

  priv->model = g_list_store_new (DIA_TYPE_OPTION_ITEM);

  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (setup_item), NULL);
  g_signal_connect (factory, "bind", G_CALLBACK (bind_item), NULL);

  priv->dropdown = gtk_drop_down_new (G_LIST_MODEL (priv->model), NULL);
  gtk_drop_down_set_factory (GTK_DROP_DOWN (priv->dropdown), factory);
  g_object_unref (factory);

  g_signal_connect (priv->dropdown,
                    "notify::selected",
                    G_CALLBACK (selection_changed),
                    self);

  gtk_widget_set_parent (priv->dropdown, GTK_WIDGET (self));
}


GtkWidget *
dia_option_menu_new (void)
{
  return g_object_new (DIA_TYPE_OPTION_MENU, NULL);
}


/**
 * dia_option_menu_add_item:
 * @self: the #DiaOptionMenu
 * @name: the item name
 * @value: the item value
 *
 * Convenient form of gtk_menu_append and more
 */
void
dia_option_menu_add_item (DiaOptionMenu *self,
                          const char    *name,
                          int            value)
{
  DiaOptionMenuPrivate *priv;
  DiaOptionItem *item;

  g_return_if_fail (DIA_IS_OPTION_MENU (self));

  priv = dia_option_menu_get_instance_private (self);

  item = dia_option_item_new (name, value);
  g_list_store_append (priv->model, item);
  g_object_unref (item);
}


/**
 * dia_option_menu_set_active:
 * @self: the #DiaOptionMenu
 * @active: the new value
 *
 * drop in replacement gtk_option_menu_set_history
 */
void
dia_option_menu_set_active (DiaOptionMenu *self, int active)
{
  DiaOptionMenuPrivate *priv;
  guint n;

  g_return_if_fail (DIA_IS_OPTION_MENU (self));

  priv = dia_option_menu_get_instance_private (self);

  n = g_list_model_get_n_items (G_LIST_MODEL (priv->model));
  for (guint i = 0; i < n; i++) {
    DiaOptionItem *item = g_list_model_get_item (G_LIST_MODEL (priv->model), i);
    gboolean match = item->value == active;
    g_object_unref (item);
    if (match) {
      gtk_drop_down_set_selected (GTK_DROP_DOWN (priv->dropdown), i);
      break;
    }
  }
}


/**
 * dia_option_menu_get_active
 * @self: the #DiaOptionMenu
 *
 * drop in replacement gtk_option_menu_get_history
 */
int
dia_option_menu_get_active (DiaOptionMenu *self)
{
  DiaOptionMenuPrivate *priv;
  DiaOptionItem *item;

  g_return_val_if_fail (DIA_IS_OPTION_MENU (self), -1);

  priv = dia_option_menu_get_instance_private (self);

  item = gtk_drop_down_get_selected_item (GTK_DROP_DOWN (priv->dropdown));
  if (item) {
    return item->value;
  }

  g_return_val_if_reached (-1);
}
