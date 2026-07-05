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

#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include "dia-font-selector.h"
#include "font.h"
#include "persistence.h"

#define PERSIST_NAME "font-menu"

struct _DiaFontSelector {
  GtkBox hbox;
};

/*
 * GTK4: the deprecated GtkComboBox + GtkTreeStore + cell renderers are gone.
 * Both selectors are GtkDropDowns. The font list is flat and searchable
 * (GtkStringList) instead of the old nested "Other Fonts" submenu; the style
 * list is a GListModel of DiaFontStyleItem (label -> DiaFontStyle id).
 */

#define DIA_TYPE_FONT_STYLE_ITEM (dia_font_style_item_get_type ())
G_DECLARE_FINAL_TYPE (DiaFontStyleItem, dia_font_style_item, DIA, FONT_STYLE_ITEM, GObject)

struct _DiaFontStyleItem {
  GObject parent_instance;
  char   *label;
  int     id;
};

G_DEFINE_TYPE (DiaFontStyleItem, dia_font_style_item, G_TYPE_OBJECT)

static void
dia_font_style_item_finalize (GObject *object)
{
  DiaFontStyleItem *self = DIA_FONT_STYLE_ITEM (object);
  g_clear_pointer (&self->label, g_free);
  G_OBJECT_CLASS (dia_font_style_item_parent_class)->finalize (object);
}

static void
dia_font_style_item_class_init (DiaFontStyleItemClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = dia_font_style_item_finalize;
}

static void dia_font_style_item_init (DiaFontStyleItem *self) {}

static DiaFontStyleItem *
dia_font_style_item_new (const char *label, int id)
{
  DiaFontStyleItem *item = g_object_new (DIA_TYPE_FONT_STYLE_ITEM, NULL);
  item->label = g_strdup (label);
  item->id = id;
  return item;
}


typedef struct _DiaFontSelectorPrivate DiaFontSelectorPrivate;
struct _DiaFontSelectorPrivate {
  GtkWidget     *fonts;         /* GtkDropDown */
  GtkStringList *fonts_store;   /* borrowed (owned by fonts) */

  GtkWidget     *styles;        /* GtkDropDown */
  GListStore    *styles_store;  /* borrowed (owned by styles) */

  char          *current;
  int            current_style;
};

G_DEFINE_TYPE_WITH_PRIVATE (DiaFontSelector, dia_font_selector, GTK_TYPE_BOX)

enum {
  VALUE_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };


/* New and improved font selector:  Contains the three standard fonts
 * and an 'Other fonts...' entry that opens the font dialog.  The fonts
 * selected in the font dialog are persistently added to the menu.
 *
 * +----------------+
 * | Sans           |
 * | Serif          |
 * | Monospace      |
 * | -------------- |
 * | Bodini         |
 * | CurlyGothic    |
 * | OldWestern     |
 * | -------------- |
 * | Other fonts... |
 * +----------------+
 */

enum {
  STYLE_COL_LABEL,
  STYLE_COL_ID,
  STYLE_N_COL,
};


enum {
  FONT_COL_FAMILY,
  FONT_N_COL,
};


static void
dia_font_selector_finalize (GObject *object)
{
  DiaFontSelector *self = DIA_FONT_SELECTOR (object);
  DiaFontSelectorPrivate *priv = dia_font_selector_get_instance_private (self);

  /* fonts_store / styles_store are owned by their GtkDropDowns. */
  g_clear_pointer (&priv->current, g_free);

  G_OBJECT_CLASS (dia_font_selector_parent_class)->finalize (object);
}


static void
dia_font_selector_class_init (DiaFontSelectorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = dia_font_selector_finalize;

  signals[VALUE_CHANGED] = g_signal_new ("value-changed",
                                         G_TYPE_FROM_CLASS (klass),
                                         G_SIGNAL_RUN_FIRST,
                                         0, NULL, NULL,
                                         g_cclosure_marshal_VOID__VOID,
                                         G_TYPE_NONE, 0);
}


static int
sort_fonts (const void *p1, const void *p2)
{
  const gchar *n1 = pango_font_family_get_name (PANGO_FONT_FAMILY (*(void**)p1));
  const gchar *n2 = pango_font_family_get_name (PANGO_FONT_FAMILY (*(void**)p2));
  return g_ascii_strcasecmp (n1, n2);
}


static char *style_labels[] = {
  "Normal",
  "Oblique",
  "Italic",
  "Ultralight",
  "Ultralight-Oblique",
  "Ultralight-Italic",
  "Light",
  "Light-Oblique",
  "Light-Italic",
  "Medium",
  "Medium-Oblique",
  "Medium-Italic",
  "Demibold",
  "Demibold-Oblique",
  "Demibold-Italic",
  "Bold",
  "Bold-Oblique",
  "Bold-Italic",
  "Ultrabold",
  "Ultrabold-Oblique",
  "Ultrabold-Italic",
  "Heavy",
  "Heavy-Oblique",
  "Heavy-Italic"
};


static PangoFontFamily *
get_family_from_name (GtkWidget *widget, const gchar *fontname)
{
  /* The installed font set does not change during a session, so enumerate the
   * (potentially large) family list once and cache it. Re-listing on every
   * lookup was the dominant cost when the property dialog re-syncs widgets. */
  static PangoFontFamily **families = NULL;
  static int n_families = 0;
  int i;

  if (families == NULL) {
    pango_context_list_families (dia_font_get_context (),
                                 &families, &n_families);
  }

  for (i = 0; i < n_families; i++) {
    if (!(g_ascii_strcasecmp (pango_font_family_get_name (families[i]), fontname))) {
      return families[i];
    }
  }
  g_warning (_("Couldn't find font family for %s\n"), fontname);
  return NULL;
}


static void
set_styles (DiaFontSelector *fs,
            const gchar     *name,
            DiaFontStyle     dia_style)
{
  PangoFontFamily *pff;
  DiaFontSelectorPrivate *priv;
  PangoFontFace **faces = NULL;
  int nfaces = 0;
  int i = 0;
  long stylebits = 0;

  g_return_if_fail (DIA_IS_FONT_SELECTOR (fs));

  priv = dia_font_selector_get_instance_private (fs);

  pff = get_family_from_name (GTK_WIDGET (fs), name);

  pango_font_family_list_faces (pff, &faces, &nfaces);

  for (i = 0; i < nfaces; i++) {
    PangoFontDescription *pfd = pango_font_face_describe (faces[i]);
    PangoStyle style = pango_font_description_get_style (pfd);
    PangoWeight weight = pango_font_description_get_weight (pfd);
    /*
     * This is a quick and dirty way to pick the styles present,
     * sort them and avoid duplicates.
     * We set a bit for each style present, bit (weight*3+style)
     * From style_labels, we pick #(weight*3+style)
     * where weight and style are the Dia types.
     */
    /* Account for DIA_WEIGHT_NORMAL hack */
    int weightnr = (weight-200)/100;
    if (weightnr < 2) weightnr ++;
    else if (weightnr == 2) weightnr = 0;
    stylebits |= 1 << (3*weightnr + style);
    pango_font_description_free (pfd);
  }

  g_clear_pointer (&faces, g_free);

  if (stylebits == 0) {
    g_warning ("'%s' has no style!",
               pango_font_family_get_name (pff) ? pango_font_family_get_name (pff) : "(null font)");
  }

  g_list_store_remove_all (priv->styles_store);

  for (i = DIA_FONT_NORMAL; i <= (DIA_FONT_HEAVY | DIA_FONT_ITALIC); i+=4) {
    DiaFontStyleItem *item;
    guint pos;

    /*
     * bad hack continued ...
     */
    int weight = DIA_FONT_STYLE_GET_WEIGHT (i) >> 4;
    int slant = DIA_FONT_STYLE_GET_SLANT (i) >> 2;

    if (DIA_FONT_STYLE_GET_SLANT (i) > DIA_FONT_ITALIC) {
      continue;
    }

    if (!(stylebits & (1 << (3 * weight + slant)))) {
      continue;
    }

    item = dia_font_style_item_new (style_labels[3 * weight + slant], i);
    pos = g_list_model_get_n_items (G_LIST_MODEL (priv->styles_store));
    g_list_store_append (priv->styles_store, item);
    g_object_unref (item);

    if (dia_style == i || (i == DIA_FONT_NORMAL && dia_style == -1)) {
      gtk_drop_down_set_selected (GTK_DROP_DOWN (priv->styles), pos);
    }
  }

  gtk_widget_set_sensitive (GTK_WIDGET (priv->styles),
                            g_list_model_get_n_items (G_LIST_MODEL (priv->styles_store)) > 1);
}


/* Case-insensitive lookup of a family name in the flat font list. */
static guint
font_find_family (DiaFontSelectorPrivate *priv, const char *family)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (priv->fonts_store));

  for (guint i = 0; i < n; i++) {
    const char *s = gtk_string_list_get_string (priv->fonts_store, i);
    if (g_ascii_strcasecmp (s, family) == 0) {
      return i;
    }
  }

  return GTK_INVALID_LIST_POSITION;
}


static void
font_changed (GtkDropDown     *widget,
              GParamSpec      *pspec,
              DiaFontSelector *self)
{
  DiaFontSelectorPrivate *priv;
  guint pos;
  const char *family;

  g_return_if_fail (DIA_IS_FONT_SELECTOR (self));

  priv = dia_font_selector_get_instance_private (self);

  pos = gtk_drop_down_get_selected (widget);
  if (pos == GTK_INVALID_LIST_POSITION) {
    return;
  }

  family = gtk_string_list_get_string (priv->fonts_store, pos);

  g_clear_pointer (&priv->current, g_free);
  priv->current = g_strdup (family);

  set_styles (self, family, -1);
  g_signal_emit (G_OBJECT (self), signals[VALUE_CHANGED], 0);

  if (g_strcmp0 (family, "sans") != 0 &&
      g_strcmp0 (family, "serif") != 0 &&
      g_strcmp0 (family, "monospace") != 0) {
    persistent_list_add (PERSIST_NAME, family);
  }
}


static void
style_changed (GtkDropDown     *widget,
               GParamSpec      *pspec,
               DiaFontSelector *self)
{
  DiaFontSelectorPrivate *priv;
  DiaFontStyleItem *active;

  g_return_if_fail (DIA_IS_FONT_SELECTOR (self));

  priv = dia_font_selector_get_instance_private (self);

  active = gtk_drop_down_get_selected_item (GTK_DROP_DOWN (priv->styles));
  if (active) {
    priv->current_style = active->id;
  } else {
    priv->current_style = 0;
  }

  g_signal_emit (G_OBJECT (self), signals[VALUE_CHANGED], 0);
}


static void
style_setup_item (GtkSignalListItemFactory *factory,
                  GtkListItem              *list_item,
                  gpointer                  data)
{
  GtkWidget *label = gtk_label_new (NULL);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_list_item_set_child (list_item, label);
}


static void
style_bind_item (GtkSignalListItemFactory *factory,
                 GtkListItem              *list_item,
                 gpointer                  data)
{
  DiaFontStyleItem *item = gtk_list_item_get_item (list_item);
  GtkWidget *label = gtk_list_item_get_child (list_item);

  gtk_label_set_text (GTK_LABEL (label), item->label);
}


static void
dia_font_selector_init (DiaFontSelector *fs)
{
  DiaFontSelectorPrivate *priv;
  PangoFontFamily **families;
  int n_families,i;
  GtkListItemFactory *style_factory;
  GList *tmplist;

  g_return_if_fail (DIA_IS_FONT_SELECTOR (fs));

  priv = dia_font_selector_get_instance_private (fs);

  /* Flat, searchable font list: the three standard aliases first, then any
   * persisted custom families, then every system family (sorted, deduped). */
  priv->fonts_store = gtk_string_list_new (NULL);
  gtk_string_list_append (priv->fonts_store, "sans");
  gtk_string_list_append (priv->fonts_store, "serif");
  gtk_string_list_append (priv->fonts_store, "monospace");

  persistence_register_list (PERSIST_NAME);

  for (tmplist = persistent_list_get_glist (PERSIST_NAME);
       tmplist != NULL; tmplist = g_list_next (tmplist)) {
    if (font_find_family (priv, tmplist->data) == GTK_INVALID_LIST_POSITION) {
      gtk_string_list_append (priv->fonts_store, tmplist->data);
    }
  }

  pango_context_list_families (dia_font_get_context (),
                               &families,
                               &n_families);

  qsort (families,
         n_families,
         sizeof (PangoFontFamily *),
         sort_fonts);

  for (i = 0; i < n_families; i++) {
    const char *name = pango_font_family_get_name (families[i]);
    if (font_find_family (priv, name) == GTK_INVALID_LIST_POSITION) {
      gtk_string_list_append (priv->fonts_store, name);
    }
  }
  g_clear_pointer (&families, g_free);

  /* The expression tells the drop-down what text to match while searching
   * (each item is a GtkStringObject); without it enable-search is a no-op and
   * the popup won't scroll to / highlight the current font. */
  priv->fonts = gtk_drop_down_new (G_LIST_MODEL (priv->fonts_store),
                                   gtk_property_expression_new (GTK_TYPE_STRING_OBJECT,
                                                                NULL, "string"));
  gtk_drop_down_set_enable_search (GTK_DROP_DOWN (priv->fonts), TRUE);
  gtk_widget_set_hexpand (priv->fonts, TRUE);

  g_signal_connect (priv->fonts,
                    "notify::selected",
                    G_CALLBACK (font_changed),
                    fs);

  priv->styles_store = g_list_store_new (DIA_TYPE_FONT_STYLE_ITEM);

  style_factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (style_factory, "setup", G_CALLBACK (style_setup_item), NULL);
  g_signal_connect (style_factory, "bind", G_CALLBACK (style_bind_item), NULL);

  priv->styles = gtk_drop_down_new (G_LIST_MODEL (priv->styles_store), NULL);
  gtk_drop_down_set_factory (GTK_DROP_DOWN (priv->styles), style_factory);
  g_object_unref (style_factory);

  g_signal_connect (priv->styles,
                    "notify::selected",
                    G_CALLBACK (style_changed),
                    fs);

  gtk_box_append (GTK_BOX (fs), GTK_WIDGET (priv->fonts));
  gtk_box_append (GTK_BOX (fs), GTK_WIDGET (priv->styles));
}


GtkWidget *
dia_font_selector_new (void)
{
  return g_object_new (DIA_TYPE_FONT_SELECTOR, NULL);
}


/**
 * dia_font_selector_set_font:
 *
 * Set the current font to be shown in the font selector.
 */
void
dia_font_selector_set_font (DiaFontSelector *self, DiaFont *font)
{
  DiaFontSelectorPrivate *priv;
  const gchar *fontname = dia_font_get_family (font);
  DiaFontStyle style = dia_font_get_style (font);
  guint pos;

  g_return_if_fail (DIA_IS_FONT_SELECTOR (self));

  priv = dia_font_selector_get_instance_private (self);

  /* The property dialog resets every widget on each edit (e.g. changing the
   * font size). Re-selecting the same family would needlessly re-enumerate the
   * Pango families/faces and rebuild the style list, which is slow. Skip it
   * when nothing actually changed. */
  if (priv->current &&
      g_ascii_strcasecmp (priv->current, fontname) == 0 &&
      priv->current_style == (int) style) {
    return;
  }

  pos = font_find_family (priv, fontname);
  if (pos == GTK_INVALID_LIST_POSITION) {
    /* An unknown family: add it so it can be shown and selected. */
    pos = g_list_model_get_n_items (G_LIST_MODEL (priv->fonts_store));
    gtk_string_list_append (priv->fonts_store, fontname);
  }
  gtk_drop_down_set_selected (GTK_DROP_DOWN (priv->fonts), pos);

  set_styles (self, fontname, style);

  /* Record the resolved state so a re-sync with the same font short-circuits.
   * (font_changed only fires when the *selection* changes, so it can't be
   * relied on to set this when the font is already selected.) */
  g_clear_pointer (&priv->current, g_free);
  priv->current = g_strdup (fontname);
  priv->current_style = (int) style;
}


DiaFont *
dia_font_selector_get_font (DiaFontSelector *self)
{
  DiaFontSelectorPrivate *priv;
  DiaFontStyle style;
  DiaFont *font;
  DiaFontStyleItem *style_item;
  const char *fontname = NULL;
  guint pos;

  g_return_val_if_fail (DIA_IS_FONT_SELECTOR (self), NULL);

  priv = dia_font_selector_get_instance_private (self);

  pos = gtk_drop_down_get_selected (GTK_DROP_DOWN (priv->fonts));
  if (pos != GTK_INVALID_LIST_POSITION) {
    fontname = gtk_string_list_get_string (priv->fonts_store, pos);
  } else {
    g_warning ("No font selected");
  }

  style_item = gtk_drop_down_get_selected_item (GTK_DROP_DOWN (priv->styles));
  style = style_item ? style_item->id : 0;

  font = dia_font_new (fontname, style, 1.0);

  return font;
}

