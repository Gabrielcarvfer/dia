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
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright © 2019 Zander Brown <zbrown@gnome.org>
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include <gtk/gtk.h>

#include "dia-simple-list.h"


/*
 * GTK4: the deprecated GtkTreeView + GtkListStore + GtkTreeSelection stack is
 * replaced by a GtkListView over a GtkStringList wrapped in a
 * GtkSingleSelection. GtkListView is final, so DiaSimpleList is a GtkWidget
 * composing a GtkScrolledWindow -> GtkListView.
 */
typedef struct _DiaSimpleListPrivate DiaSimpleListPrivate;
struct _DiaSimpleListPrivate {
  GtkWidget          *scrolled;   /* owned child */
  GtkWidget          *list_view;  /* borrowed (child of scrolled) */
  GtkStringList      *store;      /* borrowed (owned by selection) */
  GtkSingleSelection *selection;  /* owned */
};


G_DEFINE_TYPE_WITH_PRIVATE (DiaSimpleList, dia_simple_list, GTK_TYPE_WIDGET)


enum {
  SELECTION_CHANGED,
  LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0, };


static void
dia_simple_list_dispose (GObject *object)
{
  DiaSimpleList *self = DIA_SIMPLE_LIST (object);
  DiaSimpleListPrivate *priv = dia_simple_list_get_instance_private (self);

  g_clear_pointer (&priv->scrolled, gtk_widget_unparent);

  G_OBJECT_CLASS (dia_simple_list_parent_class)->dispose (object);
}


static void
dia_simple_list_class_init (DiaSimpleListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  /* The GtkSingleSelection is owned by the GtkListView (transfer full), which
   * is owned by the scrolled window unparented in dispose. */
  object_class->dispose = dia_simple_list_dispose;

  gtk_widget_class_set_layout_manager_type (GTK_WIDGET_CLASS (klass),
                                            GTK_TYPE_BIN_LAYOUT);

  signals[SELECTION_CHANGED] = g_signal_new ("selection-changed",
                                             G_TYPE_FROM_CLASS (klass),
                                             G_SIGNAL_RUN_FIRST,
                                             0,
                                             NULL, NULL, NULL,
                                             G_TYPE_NONE,
                                             0);
}


static void
selection_changed (GtkSingleSelection *selection,
                   GParamSpec         *pspec,
                   DiaSimpleList      *self)
{
  g_signal_emit (self, signals[SELECTION_CHANGED], 0);
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
  GtkStringObject *obj = gtk_list_item_get_item (list_item);
  GtkWidget *label = gtk_list_item_get_child (list_item);

  gtk_label_set_text (GTK_LABEL (label), gtk_string_object_get_string (obj));
}


static void
dia_simple_list_init (DiaSimpleList *self)
{
  DiaSimpleListPrivate *priv = dia_simple_list_get_instance_private (self);
  GtkListItemFactory *factory;

  priv->store = gtk_string_list_new (NULL);
  priv->selection = gtk_single_selection_new (G_LIST_MODEL (priv->store));
  gtk_single_selection_set_autoselect (priv->selection, FALSE);
  gtk_single_selection_set_can_unselect (priv->selection, TRUE);
  gtk_single_selection_set_selected (priv->selection, GTK_INVALID_LIST_POSITION);

  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (setup_item), NULL);
  g_signal_connect (factory, "bind", G_CALLBACK (bind_item), NULL);

  priv->list_view = gtk_list_view_new (GTK_SELECTION_MODEL (priv->selection),
                                       factory);

  priv->scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (priv->scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (priv->scrolled),
                                                    TRUE);
  gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (priv->scrolled),
                                              200);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (priv->scrolled),
                                 priv->list_view);
  gtk_widget_set_parent (priv->scrolled, GTK_WIDGET (self));

  g_signal_connect (priv->selection,
                    "notify::selected",
                    G_CALLBACK (selection_changed),
                    self);
}


GtkWidget *
dia_simple_list_new (void)
{
  return g_object_new (DIA_TYPE_SIMPLE_LIST, NULL);
}


void
dia_simple_list_empty (DiaSimpleList *self)
{
  DiaSimpleListPrivate *priv;

  g_return_if_fail (DIA_IS_SIMPLE_LIST (self));

  priv = dia_simple_list_get_instance_private (self);

  gtk_string_list_splice (priv->store,
                          0,
                          g_list_model_get_n_items (G_LIST_MODEL (priv->store)),
                          NULL);
}


void
dia_simple_list_append (DiaSimpleList *self,
                        const char    *item)
{
  DiaSimpleListPrivate *priv;

  g_return_if_fail (DIA_IS_SIMPLE_LIST (self));

  priv = dia_simple_list_get_instance_private (self);

  gtk_string_list_append (priv->store, item);
}


void
dia_simple_list_select (DiaSimpleList *self,
                        int            n)
{
  DiaSimpleListPrivate *priv;

  g_return_if_fail (DIA_IS_SIMPLE_LIST (self));

  priv = dia_simple_list_get_instance_private (self);

  if (n == -1) {
    gtk_single_selection_set_selected (priv->selection,
                                       GTK_INVALID_LIST_POSITION);
    return;
  }

  if ((guint) n >= g_list_model_get_n_items (G_LIST_MODEL (priv->store))) {
    g_warning ("Can't select %i", n);
    return;
  }

  gtk_single_selection_set_selected (priv->selection, n);
}


int
dia_simple_list_get_selected (DiaSimpleList *self)
{
  DiaSimpleListPrivate *priv;
  guint pos;

  g_return_val_if_fail (DIA_IS_SIMPLE_LIST (self), -1);

  priv = dia_simple_list_get_instance_private (self);

  pos = gtk_single_selection_get_selected (priv->selection);

  return pos == GTK_INVALID_LIST_POSITION ? -1 : (int) pos;
}

