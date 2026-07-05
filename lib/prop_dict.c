/* Dia -- a diagram creation/manipulation program -*- c -*-
 * Copyright (C) 1998 Alexander Larsson
 *
 * Property system for dia objects/shapes.
 * Copyright (C) 2000 James Henstridge
 * Copyright (C) 2001 Cyrille Chepelov
 * Copyright (C) 2009 Hans Breuer
 *
 * Property types for dictonaries.
 * These dictionaries are simple key/value pairs both of type string.
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
#define WIDGET GtkWidget
#include "properties.h"
#include "propinternals.h"

typedef struct _WellKnownKeys {
  const gchar *name;
  const gchar *display_name;
} WellKnownKeys;

/* a list of wel known keys with their display name */
static WellKnownKeys _well_known[] = {
  { "author", N_("Author") },
  { "id", N_("Identifier") },
  { "creation", N_("Creation date") },
  { "modification", N_("Modification date") },
  { "url", N_("URL") },
  { NULL, NULL }
};

static DictProperty *
dictprop_new(const PropDescription *pdesc, PropDescToPropPredicate reason)
{
  DictProperty *prop = g_new0(DictProperty,1);

  initialize_property(&prop->common, pdesc, reason);
  prop->dict = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  return prop;
}

static void
dictprop_free(DictProperty *prop)
{
  if (prop->dict)
    g_hash_table_destroy(prop->dict);
  g_clear_pointer (&prop, g_free);
}

static void
_keyvalue_copy (gpointer key,
                gpointer value,
                gpointer user_data)
{
  gchar *name = (gchar *)key;
  gchar *val = (gchar *)value;
  GHashTable *dest = (GHashTable *)user_data;

  g_hash_table_insert (dest, g_strdup (name), g_strdup (val));
}
static DictProperty *
dictprop_copy(DictProperty *src)
{
  DictProperty *prop =
    (DictProperty *)src->common.ops->new_prop(src->common.descr,
                                              src->common.reason);
  if (src->dict)
    g_hash_table_foreach (src->dict, _keyvalue_copy, prop->dict);

  return prop;
}

static void
dictprop_load(DictProperty *prop, AttributeNode attr, DataNode data, DiaContext *ctx)
{
  DataNode kv;
  guint nvals = attribute_num_data(attr);
  if (!nvals)
    return;

  kv = attribute_first_data (data);
  while (kv) {
    xmlChar *key = xmlGetProp(kv, (const xmlChar *)"name");

    if (key) {
      gchar *value = data_string(attribute_first_data (kv), ctx);
      if (value)
        g_hash_table_insert (prop->dict, g_strdup((gchar *)key), value);
      xmlFree (key);
    } else {
      g_warning ("Dictionary key missing");
    }
    kv = data_next(kv);
  }
}

static GHashTable *
_hash_dup (const GHashTable *src)
{
  GHashTable *dest = NULL;
  if (src) {
    dest = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    g_hash_table_foreach ((GHashTable *)src, _keyvalue_copy, dest);
  }
  return dest;
}
typedef struct
{
  ObjectNode  node;
  DiaContext *ctx;
} DictUserData;
static void
_keyvalue_save (gpointer key,
                gpointer value,
                gpointer user_data)
{
  DictUserData *ud = (DictUserData *)user_data;
  gchar *name = (gchar *)key;
  gchar *val = (gchar *)value;
  ObjectNode node = ud->node;
  DiaContext *ctx = ud->ctx;

  data_add_string(new_attribute(node, name), val, ctx);
}
static void
dictprop_save(DictProperty *prop, AttributeNode attr, DiaContext *ctx)
{
  DictUserData ud;
  ud.node = data_add_composite(attr, "dict", ctx);
  ud.ctx = ctx;
  if (prop->dict)
    g_hash_table_foreach (prop->dict, _keyvalue_save, &ud);
}

static void
dictprop_get_from_offset(DictProperty *prop,
                         void *base, guint offset, guint offset2)
{
  prop->dict = _hash_dup (struct_member(base,offset,GHashTable *));
}

static void
dictprop_set_from_offset(DictProperty *prop,
                         void *base, guint offset, guint offset2)
{
  GHashTable *dest = struct_member(base,offset,GHashTable *);
  if (dest)
    g_hash_table_destroy (dest);
  struct_member(base,offset, GHashTable *) = _hash_dup (prop->dict);
}

/* GUI stuff */
/*
 * GTK4: the deprecated GtkTreeView + GtkTreeStore + editable text cell
 * renderer are replaced by a GtkColumnView over a GListStore of DiaDictItem.
 * The value column is a GtkEditableLabel bound bidirectionally to the item's
 * "value" property; edits flip a "modified" flag stored on the store.
 */
#define TREE_MODEL_KEY "dict-model"

#define DIA_TYPE_DICT_ITEM (dia_dict_item_get_type ())
G_DECLARE_FINAL_TYPE (DiaDictItem, dia_dict_item, DIA, DICT_ITEM, GObject)

struct _DiaDictItem {
  GObject   parent_instance;
  char     *key;
  char     *value;
  gboolean  editable;
};

enum {
  ITEM_PROP_0,
  ITEM_PROP_KEY,
  ITEM_PROP_VALUE,
  ITEM_PROP_EDITABLE,
  ITEM_N_PROPS,
};
static GParamSpec *item_pspecs[ITEM_N_PROPS];

G_DEFINE_TYPE (DiaDictItem, dia_dict_item, G_TYPE_OBJECT)

static void
dia_dict_item_finalize (GObject *object)
{
  DiaDictItem *self = DIA_DICT_ITEM (object);
  g_clear_pointer (&self->key, g_free);
  g_clear_pointer (&self->value, g_free);
  G_OBJECT_CLASS (dia_dict_item_parent_class)->finalize (object);
}

static void
dia_dict_item_get_property (GObject *object, guint id, GValue *value, GParamSpec *pspec)
{
  DiaDictItem *self = DIA_DICT_ITEM (object);
  switch (id) {
    case ITEM_PROP_KEY:      g_value_set_string (value, self->key); break;
    case ITEM_PROP_VALUE:    g_value_set_string (value, self->value); break;
    case ITEM_PROP_EDITABLE: g_value_set_boolean (value, self->editable); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
  }
}

static void
dia_dict_item_set_property (GObject *object, guint id, const GValue *value, GParamSpec *pspec)
{
  DiaDictItem *self = DIA_DICT_ITEM (object);
  switch (id) {
    case ITEM_PROP_KEY:
      g_free (self->key);
      self->key = g_value_dup_string (value);
      break;
    case ITEM_PROP_VALUE:
      g_free (self->value);
      self->value = g_value_dup_string (value);
      break;
    case ITEM_PROP_EDITABLE:
      self->editable = g_value_get_boolean (value);
      break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
  }
}

static void
dia_dict_item_class_init (DiaDictItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = dia_dict_item_finalize;
  object_class->get_property = dia_dict_item_get_property;
  object_class->set_property = dia_dict_item_set_property;

  item_pspecs[ITEM_PROP_KEY] =
    g_param_spec_string ("key", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  item_pspecs[ITEM_PROP_VALUE] =
    g_param_spec_string ("value", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  item_pspecs[ITEM_PROP_EDITABLE] =
    g_param_spec_boolean ("editable", NULL, NULL, TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, ITEM_N_PROPS, item_pspecs);
}

static void dia_dict_item_init (DiaDictItem *self) { self->editable = TRUE; }


static void
mark_modified (GObject *item, GParamSpec *pspec, gpointer store)
{
  g_object_set_data (G_OBJECT (store), "modified", GINT_TO_POINTER (1));
}

static void
dict_add_item (GListStore *store, const char *key, const char *value)
{
  DiaDictItem *item = g_object_new (DIA_TYPE_DICT_ITEM,
                                    "key", key,
                                    "value", value ? value : "",
                                    "editable", TRUE,
                                    NULL);
  /* connect before appending so the initial value doesn't count as a change */
  g_signal_connect (item, "notify::value", G_CALLBACK (mark_modified), store);
  g_list_store_append (store, item);
  g_object_unref (item);
}


static GListStore *
_create_model (DictProperty *prop)
{
  return g_list_store_new (DIA_TYPE_DICT_ITEM);
}


static void
key_setup (GtkSignalListItemFactory *f, GtkListItem *li, gpointer d)
{
  GtkWidget *label = gtk_label_new (NULL);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_list_item_set_child (li, label);
}

static void
key_bind (GtkSignalListItemFactory *f, GtkListItem *li, gpointer d)
{
  DiaDictItem *item = gtk_list_item_get_item (li);
  gtk_label_set_text (GTK_LABEL (gtk_list_item_get_child (li)), item->key);
}

static void
value_setup (GtkSignalListItemFactory *f, GtkListItem *li, gpointer d)
{
  GtkWidget *label = gtk_editable_label_new ("");
  gtk_list_item_set_child (li, label);
}

static void
value_bind (GtkSignalListItemFactory *f, GtkListItem *li, gpointer d)
{
  DiaDictItem *item = gtk_list_item_get_item (li);
  GtkWidget *label = gtk_list_item_get_child (li);
  GBinding *binding;

  gtk_editable_set_editable (GTK_EDITABLE (label), item->editable);
  binding = g_object_bind_property (item, "value", label, "text",
                                    G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
  g_object_set_data (G_OBJECT (li), "binding", binding);
}

static void
value_unbind (GtkSignalListItemFactory *f, GtkListItem *li, gpointer d)
{
  GBinding *binding = g_object_get_data (G_OBJECT (li), "binding");
  if (binding)
    g_binding_unbind (binding);
  g_object_set_data (G_OBJECT (li), "binding", NULL);
}


static GtkWidget *
_create_view (GListStore *store)
{
  GtkWidget *widget;
  GtkWidget *column_view;
  GtkColumnViewColumn *col;
  GtkListItemFactory *factory;
  GtkSortListModel *sort_model;
  GtkNoSelection *selection;
  GtkSorter *view_sorter;

  widget = gtk_scrolled_window_new ();
  gtk_widget_set_vexpand (widget, TRUE);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (widget),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  /* sortable view over the store */
  column_view = gtk_column_view_new (NULL);
  view_sorter = gtk_column_view_get_sorter (GTK_COLUMN_VIEW (column_view));
  sort_model = gtk_sort_list_model_new (G_LIST_MODEL (g_object_ref (store)),
                                        g_object_ref (view_sorter));
  selection = gtk_no_selection_new (G_LIST_MODEL (sort_model));
  gtk_column_view_set_model (GTK_COLUMN_VIEW (column_view),
                             GTK_SELECTION_MODEL (selection));
  g_object_unref (selection);
  gtk_widget_set_vexpand (column_view, TRUE);

  /* Key column (read-only label) */
  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (key_setup), NULL);
  g_signal_connect (factory, "bind", G_CALLBACK (key_bind), NULL);
  col = gtk_column_view_column_new (_("Key"), factory);
  gtk_column_view_column_set_sorter (col,
    GTK_SORTER (gtk_string_sorter_new (gtk_property_expression_new (DIA_TYPE_DICT_ITEM, NULL, "key"))));
  gtk_column_view_append_column (GTK_COLUMN_VIEW (column_view), col);
  g_object_unref (col);

  /* Value column (editable) */
  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (value_setup), NULL);
  g_signal_connect (factory, "bind", G_CALLBACK (value_bind), NULL);
  g_signal_connect (factory, "unbind", G_CALLBACK (value_unbind), NULL);
  col = gtk_column_view_column_new (_("Value"), factory);
  gtk_column_view_column_set_expand (col, TRUE);
  gtk_column_view_column_set_sorter (col,
    GTK_SORTER (gtk_string_sorter_new (gtk_property_expression_new (DIA_TYPE_DICT_ITEM, NULL, "value"))));
  gtk_column_view_append_column (GTK_COLUMN_VIEW (column_view), col);
  g_object_unref (col);

  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (widget), column_view);

  /* keep the raw store around for reset/read-back */
  g_object_set_data_full (G_OBJECT (widget), TREE_MODEL_KEY,
                          g_object_ref (store), g_object_unref);

  return widget;
}
static GtkWidget *
dictprop_get_widget (DictProperty *prop, PropDialog *dialog)
{
  GtkWidget *ret;
  GListStore *store = _create_model (prop);
  ret = _create_view (store);
  g_object_unref (store);
  /* We maintain our own changed state via the "modified" flag. */
  return ret;
}
static void
dictprop_reset_widget(DictProperty *prop, GtkWidget *widget)
{
  GListStore *model = g_object_get_data (G_OBJECT (widget), TREE_MODEL_KEY);
  WellKnownKeys *wkk;
  GHashTableIter hiter;
  gpointer hkey, hval;

  /* should it be empty */
  g_list_store_remove_all (model);

  /* add everything we have */
  if (!prop->dict)
    prop->dict = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  g_hash_table_iter_init (&hiter, prop->dict);
  while (g_hash_table_iter_next (&hiter, &hkey, &hval)) {
    dict_add_item (model, hkey, hval);
  }

  g_object_set_data (G_OBJECT (model), "modified", GINT_TO_POINTER (0));

  /* also add the well known ? */
  for (wkk = _well_known; wkk->name != NULL; ++wkk) {
    gchar *val;

    if (g_hash_table_lookup (prop->dict, wkk->name))
      continue;

    val = g_hash_table_lookup (prop->dict, wkk->name);
    dict_add_item (model, wkk->name, val ? val : "");
  }
}
static void
dictprop_set_from_widget(DictProperty *prop, GtkWidget *widget)
{
  GListStore *model = g_object_get_data (G_OBJECT (widget), TREE_MODEL_KEY);
  guint n = g_list_model_get_n_items (G_LIST_MODEL (model));
  gboolean modified = g_object_get_data (G_OBJECT (model), "modified") != NULL;

  for (guint i = 0; i < n; i++) {
    DiaDictItem *item = g_list_model_get_item (G_LIST_MODEL (model), i);
    const char *key = item->key;
    const char *val = item->value;

    if (key && val) {
      if (!prop->dict)
        prop->dict = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

      if (strlen (val))
        g_hash_table_insert (prop->dict, g_strdup (key), g_strdup (val));
      else /* delete stuff which has no value any longer */
        g_hash_table_remove (prop->dict, key);
      /* enough to replace prophandler_connect() ? */
      if (modified)
        prop->common.experience &= ~PXP_NOTSET;
    }

    g_object_unref (item);
  }
}
static const PropertyOps dictprop_ops = {
  (PropertyType_New) dictprop_new,
  (PropertyType_Free) dictprop_free,
  (PropertyType_Copy) dictprop_copy,
  (PropertyType_Load) dictprop_load,
  (PropertyType_Save) dictprop_save,
  (PropertyType_GetWidget) dictprop_get_widget,
  (PropertyType_ResetWidget) dictprop_reset_widget,
  (PropertyType_SetFromWidget) dictprop_set_from_widget,

  (PropertyType_CanMerge) noopprop_can_merge,
  (PropertyType_GetFromOffset) dictprop_get_from_offset,
  (PropertyType_SetFromOffset) dictprop_set_from_offset
};

void
prop_dicttypes_register(void)
{
  prop_type_register(PROP_TYPE_DICT, &dictprop_ops);
}

GHashTable *
data_dict (DataNode data, DiaContext *ctx)
{
  GHashTable *ht = NULL;
  int nvals = attribute_num_data (data);

  if (nvals) {
    DataNode kv = attribute_first_data (data);
    gchar *val = NULL;

    ht = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    while (kv) {
      xmlChar *key = xmlGetProp(kv, (const xmlChar *)"name");

      if (key) {
        val = data_string (attribute_first_data (kv), ctx);
        if (val)
          g_hash_table_insert (ht, g_strdup ((gchar *)key), val);
	xmlFree (key);
      }
      kv = data_next (kv);
    }
  }
  return ht;
}

void
data_add_dict (AttributeNode attr, GHashTable *data, DiaContext *ctx)
{
  DictUserData ud;
  ud.node = data_add_composite(attr, "dict", ctx);
  ud.ctx = ctx;

  g_hash_table_foreach (data, _keyvalue_save, &ud);
}
