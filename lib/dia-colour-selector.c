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

#include "dia-colour-selector-private.h"
#include "dia-lib-private-enums.h"
#include "diamarshal.h"
#include "persistence.h"

#include "dia-colour-selector.h"

#define PERSIST_NAME "color-menu"

/*
 * GTK4: the deprecated GtkComboBox + GtkListStore + DiaColourCellRenderer
 * (previously assembled from a GtkBuilder template) are replaced by a
 * GtkDropDown over a GListModel of DiaColourItem, rendered by a swatch + label
 * factory. "More Colours…" opens the async GtkColorDialog (GTK 4.10+). The
 * old inter-section separators are dropped (GtkDropDown has no separators).
 */

#define DIA_TYPE_COLOUR_ITEM (dia_colour_item_get_type ())
G_DECLARE_FINAL_TYPE (DiaColourItem, dia_colour_item, DIA, COLOUR_ITEM, GObject)

struct _DiaColourItem {
  GObject                parent_instance;
  DiaColour              colour;
  gboolean               has_colour;
  char                  *text;
  DiaColourSelectorItem  special;
};

G_DEFINE_TYPE (DiaColourItem, dia_colour_item, G_TYPE_OBJECT)

static void
dia_colour_item_finalize (GObject *object)
{
  DiaColourItem *self = DIA_COLOUR_ITEM (object);
  g_clear_pointer (&self->text, g_free);
  G_OBJECT_CLASS (dia_colour_item_parent_class)->finalize (object);
}

static void
dia_colour_item_class_init (DiaColourItemClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = dia_colour_item_finalize;
}

static void dia_colour_item_init (DiaColourItem *self) {}

static DiaColourItem *
dia_colour_item_new_colour (const DiaColour *colour, const char *text)
{
  DiaColourItem *item = g_object_new (DIA_TYPE_COLOUR_ITEM, NULL);
  item->colour = *colour;
  item->has_colour = TRUE;
  item->text = g_strdup (text);
  item->special = DIA_SELECTOR_ITEM_COLOUR;
  return item;
}

static DiaColourItem *
dia_colour_item_new_special (DiaColourSelectorItem special, const char *text)
{
  DiaColourItem *item = g_object_new (DIA_TYPE_COLOUR_ITEM, NULL);
  item->has_colour = FALSE;
  item->text = g_strdup (text);
  item->special = special;
  return item;
}


struct _DiaColourSelector {
  GtkBox          hbox;

  DiaColour      *current;
  gboolean        use_alpha;

  GtkWidget      *combo;         /* GtkDropDown */
  GListStore     *colour_store;  /* borrowed (owned by combo) */

  GCancellable   *cancellable;   /* for the async colour dialog */
  gboolean        in_reset;      /* suppress re-entrancy while rebuilding */
};


G_DEFINE_TYPE (DiaColourSelector, dia_colour_selector, GTK_TYPE_BOX)


enum {
  PROP_0,
  PROP_CURRENT,
  PROP_USE_ALPHA,
  N_PROPS,
};
static GParamSpec *pspecs[N_PROPS];


enum {
  VALUE_CHANGED,
  N_SIGNALS,
};
static guint signals[N_SIGNALS] = { 0 };


static void
dia_colour_selector_dispose (GObject *object)
{
  DiaColourSelector *self = DIA_COLOUR_SELECTOR (object);

  if (self->cancellable) {
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
  }

  g_clear_pointer (&self->current, dia_colour_free);

  G_OBJECT_CLASS (dia_colour_selector_parent_class)->dispose (object);
}


static void
dia_colour_selector_get_property (GObject    *object,
                                  guint       property_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  DiaColourSelector *self = DIA_COLOUR_SELECTOR (object);
  DiaColour colour;

  switch (property_id) {
    case PROP_CURRENT:
      dia_colour_selector_get_colour (self, &colour);
      g_value_set_boxed (value, &colour);
      break;
    case PROP_USE_ALPHA:
      g_value_set_boolean (value, self->use_alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
dia_colour_selector_set_property (GObject      *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  DiaColourSelector *self = DIA_COLOUR_SELECTOR (object);

  switch (property_id) {
    case PROP_CURRENT:
      dia_colour_selector_set_colour (self, g_value_get_boxed (value));
      break;
    case PROP_USE_ALPHA:
      dia_colour_selector_set_use_alpha (self, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
add_colour (DiaColourSelector *self, const char *hex)
{
  DiaColour colour;
  DiaColourItem *item;

  dia_colour_parse (&colour, hex);
  item = dia_colour_item_new_colour (&colour, hex);
  g_list_store_append (self->colour_store, item);
  g_object_unref (item);
}


/* Index of the "More Colours…" action item (marks the end of the colours). */
static guint
more_index (DiaColourSelector *self)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (self->colour_store));

  for (guint i = 0; i < n; i++) {
    DiaColourItem *item = g_list_model_get_item (G_LIST_MODEL (self->colour_store), i);
    gboolean is_more = item->special == DIA_SELECTOR_ITEM_MORE;
    g_object_unref (item);
    if (is_more) {
      return i;
    }
  }

  return n;
}


/* ---- swatch + label factory ------------------------------------------- */

static void
swatch_draw (GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
  DiaColour *colour = g_object_get_data (G_OBJECT (area), "dia-colour");
  GdkRGBA rgba;

  if (!colour) {
    return;
  }

  dia_colour_as_gdk (colour, &rgba);
  gdk_cairo_set_source_rgba (cr, &rgba);
  cairo_rectangle (cr, 0, 0, width, height);
  cairo_fill_preserve (cr);
  cairo_set_source_rgba (cr, 0, 0, 0, 0.4);
  cairo_set_line_width (cr, 1);
  cairo_stroke (cr);
}


static void
setup_item (GtkSignalListItemFactory *factory,
            GtkListItem              *list_item,
            gpointer                  data)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *swatch = gtk_drawing_area_new ();
  GtkWidget *label = gtk_label_new (NULL);

  gtk_widget_set_size_request (swatch, 20, 14);
  gtk_widget_set_valign (swatch, GTK_ALIGN_CENTER);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (swatch), swatch_draw, NULL, NULL);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);

  gtk_box_append (GTK_BOX (box), swatch);
  gtk_box_append (GTK_BOX (box), label);
  gtk_list_item_set_child (list_item, box);
}


static void
bind_item (GtkSignalListItemFactory *factory,
           GtkListItem              *list_item,
           gpointer                  data)
{
  DiaColourItem *item = gtk_list_item_get_item (list_item);
  GtkWidget *box = gtk_list_item_get_child (list_item);
  GtkWidget *swatch = gtk_widget_get_first_child (box);
  GtkWidget *label = gtk_widget_get_last_child (box);

  if (item->has_colour) {
    g_object_set_data (G_OBJECT (swatch), "dia-colour", &item->colour);
    gtk_widget_set_visible (swatch, TRUE);
    gtk_widget_queue_draw (swatch);
  } else {
    g_object_set_data (G_OBJECT (swatch), "dia-colour", NULL);
    gtk_widget_set_visible (swatch, FALSE);
  }

  gtk_label_set_text (GTK_LABEL (label), item->text ? item->text : "");
}


/* ---- colour dialog ---------------------------------------------------- */

static void
colour_chosen (GObject *source, GAsyncResult *result, gpointer user_data)
{
  GtkColorDialog *dialog = GTK_COLOR_DIALOG (source);
  DiaColourSelector *self = DIA_COLOUR_SELECTOR (user_data);
  GError *error = NULL;
  GdkRGBA *rgba;

  rgba = gtk_color_dialog_choose_rgba_finish (dialog, result, &error);

  if (rgba) {
    DiaColour colour;
    dia_colour_from_gdk (&colour, rgba);
    dia_colour_selector_set_colour (self, &colour);
    gdk_rgba_free (rgba);
  } else {
    /* cancelled or dismissed -- restore the previous selection */
    if (self->current) {
      dia_colour_selector_set_colour (self, self->current);
    }
    g_clear_error (&error);
  }
}


static inline void
more_colours (DiaColourSelector *self)
{
  GtkColorDialog *dialog;
  GtkWidget *parent;
  GdkRGBA rgba = { 0, 0, 0, 1 };

  parent = GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (self)));

  dialog = gtk_color_dialog_new ();
  gtk_color_dialog_set_title (dialog, _("Select color"));
  gtk_color_dialog_set_with_alpha (dialog, self->use_alpha);

  if (self->current) {
    dia_colour_as_gdk (self->current, &rgba);
  }

  if (self->cancellable) {
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
  }
  self->cancellable = g_cancellable_new ();

  gtk_color_dialog_choose_rgba (dialog,
                                parent ? GTK_WINDOW (parent) : NULL,
                                &rgba,
                                self->cancellable,
                                colour_chosen,
                                self);

  g_object_unref (dialog);
}


static void
changed (GtkDropDown *widget, GParamSpec *pspec, gpointer user_data)
{
  DiaColourSelector *self = DIA_COLOUR_SELECTOR (user_data);
  DiaColourItem *active;

  if (self->in_reset) {
    return;
  }

  active = gtk_drop_down_get_selected_item (widget);
  if (!active) {
    return;
  }

  switch (active->special) {
    case DIA_SELECTOR_ITEM_COLOUR:
      g_clear_pointer (&self->current, dia_colour_free);
      self->current = dia_colour_copy (&active->colour);

      g_signal_emit (self, signals[VALUE_CHANGED], 0);
      g_object_notify_by_pspec (G_OBJECT (self), pspecs[PROP_CURRENT]);
      break;
    case DIA_SELECTOR_ITEM_MORE:
      more_colours (self);
      break;
    case DIA_SELECTOR_ITEM_RESET:
      persistent_list_clear (PERSIST_NAME);

      self->in_reset = TRUE;
      /* Drop every custom colour: the colours past the five defaults, up to
       * the "More Colours…" item. */
      while (g_list_model_get_n_items (G_LIST_MODEL (self->colour_store)) > 5) {
        DiaColourItem *item = g_list_model_get_item (G_LIST_MODEL (self->colour_store), 5);
        gboolean removable = item->special == DIA_SELECTOR_ITEM_COLOUR;
        g_object_unref (item);
        if (!removable) {
          break;
        }
        g_list_store_remove (self->colour_store, 5);
      }
      self->in_reset = FALSE;

      if (self->current) {
        dia_colour_selector_set_colour (self, self->current);
      } else {
        gtk_drop_down_set_selected (GTK_DROP_DOWN (self->combo), 0);
      }
      break;
    case DIA_SELECTOR_ITEM_SEPARATOR:
    default:
      g_return_if_reached ();
  }
}


static void
dia_colour_selector_class_init (DiaColourSelectorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = dia_colour_selector_dispose;
  object_class->get_property = dia_colour_selector_get_property;
  object_class->set_property = dia_colour_selector_set_property;

  pspecs[PROP_CURRENT] =
    g_param_spec_boxed ("current", NULL, NULL,
                        DIA_TYPE_COLOUR,
                        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  pspecs[PROP_USE_ALPHA] =
    g_param_spec_boolean ("use-alpha", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, pspecs);


  signals[VALUE_CHANGED] =
    g_signal_new ("value-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  0, NULL, NULL,
                  dia_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
  g_signal_set_va_marshaller (signals[VALUE_CHANGED],
                              G_TYPE_FROM_CLASS (klass),
                              dia_marshal_VOID__VOIDv);
}


static void
dia_colour_selector_init (DiaColourSelector *self)
{
  GList *tmplist;
  GtkListItemFactory *factory;
  DiaColourItem *item;

  self->colour_store = g_list_store_new (DIA_TYPE_COLOUR_ITEM);

  add_colour (self, "#000000");
  add_colour (self, "#FFFFFF");
  add_colour (self, "#FF0000");
  add_colour (self, "#00FF00");
  add_colour (self, "#0000FF");

  persistence_register_list (PERSIST_NAME);

  for (tmplist = persistent_list_get_glist (PERSIST_NAME);
       tmplist != NULL; tmplist = g_list_next (tmplist)) {
    add_colour (self, tmplist->data);
  }

  item = dia_colour_item_new_special (DIA_SELECTOR_ITEM_MORE, _("More Colors…"));
  g_list_store_append (self->colour_store, item);
  g_object_unref (item);

  item = dia_colour_item_new_special (DIA_SELECTOR_ITEM_RESET, _("Reset Menu"));
  g_list_store_append (self->colour_store, item);
  g_object_unref (item);

  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (setup_item), NULL);
  g_signal_connect (factory, "bind", G_CALLBACK (bind_item), NULL);

  self->combo = gtk_drop_down_new (G_LIST_MODEL (self->colour_store), NULL);
  gtk_drop_down_set_factory (GTK_DROP_DOWN (self->combo), factory);
  g_object_unref (factory);
  gtk_widget_set_hexpand (self->combo, TRUE);

  g_signal_connect (self->combo, "notify::selected",
                    G_CALLBACK (changed), self);

  gtk_box_append (GTK_BOX (self), self->combo);
}


void
dia_colour_selector_get_colour (DiaColourSelector  *self,
                                DiaColour          *colour)
{
  g_return_if_fail (DIA_IS_COLOUR_SELECTOR (self));
  g_return_if_fail (colour != NULL);

  if (G_UNLIKELY (!self->current)) {
    colour->red = colour->green = colour->blue = 0.0;
    colour->alpha = 1.0;
    return;
  }

  colour->red = self->current->red;
  colour->blue = self->current->blue;
  colour->green = self->current->green;
  colour->alpha = self->current->alpha;
}


void
dia_colour_selector_set_colour (DiaColourSelector  *self,
                                DiaColour          *colour)
{
  guint n;
  guint insert_at;

  g_return_if_fail (DIA_IS_COLOUR_SELECTOR (self));
  g_return_if_fail (colour != NULL);

  if (self->current && dia_colour_equals (self->current, colour)) {
    return;
  }

  n = g_list_model_get_n_items (G_LIST_MODEL (self->colour_store));
  for (guint i = 0; i < n; i++) {
    DiaColourItem *item = g_list_model_get_item (G_LIST_MODEL (self->colour_store), i);
    gboolean match = item->special == DIA_SELECTOR_ITEM_COLOUR &&
                     dia_colour_equals (&item->colour, colour);
    g_object_unref (item);
    if (match) {
      gtk_drop_down_set_selected (GTK_DROP_DOWN (self->combo), i);
      return;
    }
  }

  /* Not in the list: add it as a custom colour before "More Colours…". */
  {
    char *text = dia_colour_to_string (colour);
    DiaColourItem *item = dia_colour_item_new_colour (colour, text);

    persistent_list_add (PERSIST_NAME, text);

    insert_at = more_index (self);
    g_list_store_insert (self->colour_store, insert_at, item);
    g_object_unref (item);
    g_clear_pointer (&text, g_free);

    gtk_drop_down_set_selected (GTK_DROP_DOWN (self->combo), insert_at);
  }
}


void
dia_colour_selector_set_use_alpha (DiaColourSelector *self,
                                   gboolean           use_alpha)
{
  g_return_if_fail (DIA_IS_COLOUR_SELECTOR (self));

  if (self->use_alpha == use_alpha) {
    return;
  }

  self->use_alpha = use_alpha;
  g_object_notify_by_pspec (G_OBJECT (self), pspecs[PROP_USE_ALPHA]);
}


GtkWidget *
dia_colour_selector_new (void)
{
  return g_object_new (DIA_TYPE_COLOUR_SELECTOR, NULL);
}
