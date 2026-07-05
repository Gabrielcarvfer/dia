/* Dia -- a diagram creation/manipulation program
 * Copyright (C) 1998 Alexander Larsson
 *
 * Property system for dia objects/shapes.
 * Copyright (C) 2000 James Henstridge
 * Copyright (C) 2001 Cyrille Chepelov
 *
 * Properties List Widget
 * Copyright (C) 2007, 2013  Hans Breuer
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

/*
 * GTK4: the deprecated GtkTreeView + GtkTreeStore + cell renderers (including
 * DiaCellRendererEnum) are replaced by a GtkColumnView over a GListStore of
 * DiaSdRow. Each row holds one GValue per column; every column installs a
 * factory building the right editable widget (check button / spin button /
 * drop-down / editable label). Nested PROP_TYPE_DARRAY rows keep their own
 * child GListStore, shown in a second GtkColumnView (the "branch" view).
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "properties.h"
#include "propinternals.h"
#include "prop_sdarray_widget.h"


/* ---- column kinds ------------------------------------------------------ */

typedef enum {
  COL_KIND_DARRAY,
  COL_KIND_BOOL,
  COL_KIND_INT,
  COL_KIND_ENUM,
  COL_KIND_REAL,
  COL_KIND_STRING,
  COL_KIND_MULTISTRING,
  COL_KIND_UNKNOWN
} ColKind;

static ColKind
_col_kind (const Property *p)
{
  GQuark q = p->type_quark;

  if (q == g_quark_from_static_string (PROP_TYPE_DARRAY))      return COL_KIND_DARRAY;
  if (q == g_quark_from_static_string (PROP_TYPE_BOOL))        return COL_KIND_BOOL;
  if (q == g_quark_from_static_string (PROP_TYPE_INT))         return COL_KIND_INT;
  if (q == g_quark_from_static_string (PROP_TYPE_ENUM))        return COL_KIND_ENUM;
  if (q == g_quark_from_static_string (PROP_TYPE_REAL))        return COL_KIND_REAL;
  if (q == g_quark_from_static_string (PROP_TYPE_STRING))      return COL_KIND_STRING;
  if (q == g_quark_from_static_string (PROP_TYPE_MULTISTRING)) return COL_KIND_MULTISTRING;

  return COL_KIND_UNKNOWN;
}

static GType
_col_value_type (ColKind kind)
{
  switch (kind) {
    case COL_KIND_BOOL:        return G_TYPE_BOOLEAN;
    case COL_KIND_INT:
    case COL_KIND_ENUM:        return G_TYPE_INT;
    case COL_KIND_REAL:        return G_TYPE_DOUBLE;
    case COL_KIND_STRING:
    case COL_KIND_MULTISTRING: return G_TYPE_STRING;
    case COL_KIND_DARRAY:      return G_TYPE_OBJECT;
    case COL_KIND_UNKNOWN:
    default:                   return G_TYPE_POINTER;
  }
}


typedef struct {
  ColKind       kind;
  int           index;
  const char   *title;
  const char   *tooltip;
  PropEnumData *enumdata;     /* enum */
  PropNumData  *numdata;      /* real */
} ColumnDesc;

typedef struct {
  int            ncols;
  ColumnDesc    *cols;
  int            branch_index;   /* column holding the nested array, or -1 */
  ArrayProperty *branch_prop;    /* template for nested arrays */
} SdMeta;

static void
sd_meta_free (gpointer data)
{
  SdMeta *meta = data;
  g_free (meta->cols);
  g_free (meta);
}


/* ---- row object -------------------------------------------------------- */

#define DIA_TYPE_SD_ROW (dia_sd_row_get_type ())
G_DECLARE_FINAL_TYPE (DiaSdRow, dia_sd_row, DIA, SD_ROW, GObject)

struct _DiaSdRow {
  GObject   parent_instance;
  guint     ncols;
  GValue   *cells;
};

G_DEFINE_TYPE (DiaSdRow, dia_sd_row, G_TYPE_OBJECT)

static void
dia_sd_row_finalize (GObject *object)
{
  DiaSdRow *self = DIA_SD_ROW (object);

  for (guint i = 0; i < self->ncols; i++) {
    if (G_IS_VALUE (&self->cells[i])) {
      g_value_unset (&self->cells[i]);
    }
  }
  g_free (self->cells);

  G_OBJECT_CLASS (dia_sd_row_parent_class)->finalize (object);
}

static void
dia_sd_row_class_init (DiaSdRowClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = dia_sd_row_finalize;
}

static void dia_sd_row_init (DiaSdRow *self) {}

static SdMeta *store_meta (GListStore *store);

static DiaSdRow *
dia_sd_row_new (GListStore *store)
{
  SdMeta *meta = store_meta (store);
  DiaSdRow *row = g_object_new (DIA_TYPE_SD_ROW, NULL);

  row->ncols = meta->ncols;
  row->cells = g_new0 (GValue, meta->ncols);
  for (int i = 0; i < meta->ncols; i++) {
    g_value_init (&row->cells[i], _col_value_type (meta->cols[i].kind));
  }

  return row;
}

static gboolean    row_get_bool   (DiaSdRow *r, int i) { return g_value_get_boolean (&r->cells[i]); }
static void        row_set_bool   (DiaSdRow *r, int i, gboolean v) { g_value_set_boolean (&r->cells[i], v); }
static int         row_get_int    (DiaSdRow *r, int i) { return g_value_get_int (&r->cells[i]); }
static void        row_set_int    (DiaSdRow *r, int i, int v) { g_value_set_int (&r->cells[i], v); }
static double      row_get_double (DiaSdRow *r, int i) { return g_value_get_double (&r->cells[i]); }
static void        row_set_double (DiaSdRow *r, int i, double v) { g_value_set_double (&r->cells[i], v); }
static const char *row_get_string (DiaSdRow *r, int i) { return g_value_get_string (&r->cells[i]); }
static void        row_set_string (DiaSdRow *r, int i, const char *v) { g_value_set_string (&r->cells[i], v ? v : ""); }
static gpointer    row_get_object (DiaSdRow *r, int i) { return g_value_get_object (&r->cells[i]); }
static void        row_set_object (DiaSdRow *r, int i, gpointer v) { g_value_set_object (&r->cells[i], v); }


/* ---- model construction ------------------------------------------------ */

static SdMeta *
store_meta (GListStore *store)
{
  return g_object_get_data (G_OBJECT (store), "meta");
}

static GListStore *
create_sdarray_store (ArrayProperty *prop)
{
  GListStore *store = g_list_store_new (DIA_TYPE_SD_ROW);
  int columns = prop->ex_props->len;
  SdMeta *meta = g_new0 (SdMeta, 1);

  meta->ncols = columns;
  meta->cols = g_new0 (ColumnDesc, columns);
  meta->branch_index = -1;
  meta->branch_prop = NULL;

  for (int i = 0; i < columns; i++) {
    Property *p = g_ptr_array_index (prop->ex_props, i);
    ColumnDesc *cd = &meta->cols[i];

    cd->index = i;
    cd->kind = _col_kind (p);
    cd->title = p->descr->description;
    cd->tooltip = p->descr->tooltip;

    switch (cd->kind) {
      case COL_KIND_ENUM:
        cd->enumdata = p->descr->extra_data;
        break;
      case COL_KIND_REAL:
        cd->numdata = p->descr->extra_data;
        break;
      case COL_KIND_DARRAY:
        meta->branch_index = i;
        meta->branch_prop = (ArrayProperty *) p;
        break;
      case COL_KIND_UNKNOWN:
        g_warning (G_STRLOC "No model type for '%s'\n", p->descr->name);
        break;
      case COL_KIND_BOOL:
      case COL_KIND_INT:
      case COL_KIND_STRING:
      case COL_KIND_MULTISTRING:
      default:
        break;
    }
  }

  g_object_set_data_full (G_OBJECT (store), "meta", meta, sd_meta_free);

  return store;
}


static void
mark_modified (GListStore *sink)
{
  if (sink) {
    g_object_set_data (G_OBJECT (sink), "modified", GINT_TO_POINTER (1));
  }
}


/* ---- per column factory context ---------------------------------------- */

typedef struct {
  ColumnDesc    *desc;
  GListStore    *sink;        /* master store: where "modified" is recorded */
  GtkStringList *enum_model;  /* enum: label list (owned) */
} FactoryCtx;

static void
factory_ctx_free (gpointer data)
{
  FactoryCtx *ctx = data;
  g_clear_object (&ctx->enum_model);
  g_free (ctx);
}


/* -- bool (check button) -- */

static void
bool_toggled (GtkCheckButton *btn, FactoryCtx *ctx)
{
  DiaSdRow *row = g_object_get_data (G_OBJECT (btn), "row");
  if (!row) return;
  row_set_bool (row, ctx->desc->index, gtk_check_button_get_active (btn));
  mark_modified (ctx->sink);
}

static void
bool_setup (GtkSignalListItemFactory *f, GtkListItem *li, FactoryCtx *ctx)
{
  GtkWidget *btn = gtk_check_button_new ();
  gulong id;

  gtk_widget_set_halign (btn, GTK_ALIGN_CENTER);
  id = g_signal_connect (btn, "toggled", G_CALLBACK (bool_toggled), ctx);
  g_object_set_data (G_OBJECT (btn), "handler", GSIZE_TO_POINTER (id));
  gtk_list_item_set_child (li, btn);
}

static void
bool_bind (GtkSignalListItemFactory *f, GtkListItem *li, FactoryCtx *ctx)
{
  DiaSdRow *row = gtk_list_item_get_item (li);
  GtkWidget *btn = gtk_list_item_get_child (li);
  gulong id = GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (btn), "handler"));

  g_object_set_data (G_OBJECT (btn), "row", row);
  g_signal_handler_block (btn, id);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (btn), row_get_bool (row, ctx->desc->index));
  g_signal_handler_unblock (btn, id);
}


/* -- int / real (spin button) -- */

static void
spin_changed (GtkSpinButton *spin, FactoryCtx *ctx)
{
  DiaSdRow *row = g_object_get_data (G_OBJECT (spin), "row");
  if (!row) return;
  if (ctx->desc->kind == COL_KIND_REAL) {
    row_set_double (row, ctx->desc->index, gtk_spin_button_get_value (spin));
  } else {
    row_set_int (row, ctx->desc->index, gtk_spin_button_get_value_as_int (spin));
  }
  mark_modified (ctx->sink);
}

static void
spin_setup (GtkSignalListItemFactory *f, GtkListItem *li, FactoryCtx *ctx)
{
  GtkWidget *spin;
  gulong id;

  if (ctx->desc->kind == COL_KIND_REAL && ctx->desc->numdata) {
    PropNumData *nd = ctx->desc->numdata;
    spin = gtk_spin_button_new_with_range (nd->min, nd->max, nd->step);
    gtk_spin_button_set_digits (GTK_SPIN_BUTTON (spin), 2);
  } else {
    spin = gtk_spin_button_new_with_range (G_MININT, G_MAXINT, 1);
    gtk_spin_button_set_digits (GTK_SPIN_BUTTON (spin), 0);
  }

  id = g_signal_connect (spin, "value-changed", G_CALLBACK (spin_changed), ctx);
  g_object_set_data (G_OBJECT (spin), "handler", GSIZE_TO_POINTER (id));
  gtk_list_item_set_child (li, spin);
}

static void
spin_bind (GtkSignalListItemFactory *f, GtkListItem *li, FactoryCtx *ctx)
{
  DiaSdRow *row = gtk_list_item_get_item (li);
  GtkWidget *spin = gtk_list_item_get_child (li);
  gulong id = GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (spin), "handler"));

  g_object_set_data (G_OBJECT (spin), "row", row);
  g_signal_handler_block (spin, id);
  if (ctx->desc->kind == COL_KIND_REAL) {
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), row_get_double (row, ctx->desc->index));
  } else {
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), row_get_int (row, ctx->desc->index));
  }
  g_signal_handler_unblock (spin, id);
}


/* -- enum (drop down) -- */

static int
enum_value_to_index (PropEnumData *e, int value)
{
  for (int i = 0; e && e[i].name != NULL; i++) {
    if ((int) e[i].enumv == value) {
      return i;
    }
  }
  return -1;
}

static void
enum_changed (GtkDropDown *dd, GParamSpec *pspec, FactoryCtx *ctx)
{
  DiaSdRow *row = g_object_get_data (G_OBJECT (dd), "row");
  guint sel;

  if (!row) return;
  sel = gtk_drop_down_get_selected (dd);
  if (sel == GTK_INVALID_LIST_POSITION) return;

  row_set_int (row, ctx->desc->index, ctx->desc->enumdata[sel].enumv);
  mark_modified (ctx->sink);
}

static void
enum_setup (GtkSignalListItemFactory *f, GtkListItem *li, FactoryCtx *ctx)
{
  GtkWidget *dd = gtk_drop_down_new (g_object_ref (G_LIST_MODEL (ctx->enum_model)), NULL);
  gulong id;

  id = g_signal_connect (dd, "notify::selected", G_CALLBACK (enum_changed), ctx);
  g_object_set_data (G_OBJECT (dd), "handler", GSIZE_TO_POINTER (id));
  gtk_list_item_set_child (li, dd);
}

static void
enum_bind (GtkSignalListItemFactory *f, GtkListItem *li, FactoryCtx *ctx)
{
  DiaSdRow *row = gtk_list_item_get_item (li);
  GtkWidget *dd = gtk_list_item_get_child (li);
  gulong id = GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (dd), "handler"));
  int idx = enum_value_to_index (ctx->desc->enumdata, row_get_int (row, ctx->desc->index));

  g_object_set_data (G_OBJECT (dd), "row", row);
  g_signal_handler_block (dd, id);
  gtk_drop_down_set_selected (GTK_DROP_DOWN (dd),
                              idx < 0 ? GTK_INVALID_LIST_POSITION : (guint) idx);
  g_signal_handler_unblock (dd, id);
}


/* -- string / multistring (editable label) -- */

static void
text_changed (GtkEditable *editable, FactoryCtx *ctx)
{
  DiaSdRow *row = g_object_get_data (G_OBJECT (editable), "row");
  if (!row) return;
  row_set_string (row, ctx->desc->index, gtk_editable_get_text (editable));
  mark_modified (ctx->sink);
}

static void
text_setup (GtkSignalListItemFactory *f, GtkListItem *li, FactoryCtx *ctx)
{
  GtkWidget *label = gtk_editable_label_new ("");
  gulong id;

  id = g_signal_connect (label, "changed", G_CALLBACK (text_changed), ctx);
  g_object_set_data (G_OBJECT (label), "handler", GSIZE_TO_POINTER (id));
  gtk_list_item_set_child (li, label);
}

static void
text_bind (GtkSignalListItemFactory *f, GtkListItem *li, FactoryCtx *ctx)
{
  DiaSdRow *row = gtk_list_item_get_item (li);
  GtkWidget *label = gtk_list_item_get_child (li);
  gulong id = GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (label), "handler"));
  const char *value = row_get_string (row, ctx->desc->index);

  g_object_set_data (G_OBJECT (label), "row", row);
  g_signal_handler_block (label, id);
  gtk_editable_set_text (GTK_EDITABLE (label), value ? value : "");
  g_signal_handler_unblock (label, id);
}


/* ---- build the columns of a view --------------------------------------- */

static void
_build_columns (GtkColumnView *view, SdMeta *meta, GListStore *sink)
{
  for (int i = 0; i < meta->ncols; i++) {
    ColumnDesc *cd = &meta->cols[i];
    GtkListItemFactory *factory;
    GtkColumnViewColumn *col;
    FactoryCtx *ctx;

    if (cd->kind == COL_KIND_DARRAY || cd->kind == COL_KIND_UNKNOWN) {
      continue; /* not a visible column */
    }

    ctx = g_new0 (FactoryCtx, 1);
    ctx->desc = cd;
    ctx->sink = sink;

    factory = gtk_signal_list_item_factory_new ();

    switch (cd->kind) {
      case COL_KIND_BOOL:
        g_signal_connect (factory, "setup", G_CALLBACK (bool_setup), ctx);
        g_signal_connect (factory, "bind", G_CALLBACK (bool_bind), ctx);
        break;
      case COL_KIND_INT:
      case COL_KIND_REAL:
        g_signal_connect (factory, "setup", G_CALLBACK (spin_setup), ctx);
        g_signal_connect (factory, "bind", G_CALLBACK (spin_bind), ctx);
        break;
      case COL_KIND_ENUM: {
        ctx->enum_model = gtk_string_list_new (NULL);
        for (int k = 0; cd->enumdata && cd->enumdata[k].name != NULL; k++) {
          gtk_string_list_append (ctx->enum_model, _(cd->enumdata[k].name));
        }
        g_signal_connect (factory, "setup", G_CALLBACK (enum_setup), ctx);
        g_signal_connect (factory, "bind", G_CALLBACK (enum_bind), ctx);
        break;
      }
      case COL_KIND_STRING:
      case COL_KIND_MULTISTRING:
        g_signal_connect (factory, "setup", G_CALLBACK (text_setup), ctx);
        g_signal_connect (factory, "bind", G_CALLBACK (text_bind), ctx);
        break;
      case COL_KIND_DARRAY:
      case COL_KIND_UNKNOWN:
      default:
        break;
    }

    /* the context lives as long as the factory */
    g_object_set_data_full (G_OBJECT (factory), "ctx", ctx, factory_ctx_free);

    col = gtk_column_view_column_new (cd->title, factory);
    gtk_column_view_column_set_expand (col, cd->kind == COL_KIND_STRING ||
                                            cd->kind == COL_KIND_MULTISTRING);
    gtk_column_view_append_column (view, col);
    g_object_unref (col);
  }
}


/* ---- branch handling --------------------------------------------------- */

static void
_update_branch (GtkSingleSelection *selection,
                GParamSpec         *pspec,
                GtkColumnView      *master_view)
{
  GtkSingleSelection *branch_sel = g_object_get_data (G_OBJECT (master_view), "branch-sel");
  GListStore *store = g_object_get_data (G_OBJECT (master_view), "master-store");
  SdMeta *meta = store_meta (store);
  DiaSdRow *row;

  if (!branch_sel) {
    return;
  }

  row = gtk_single_selection_get_selected_item (selection);
  if (row && meta->branch_index >= 0) {
    GListStore *child = row_get_object (row, meta->branch_index);

    if (!child) {
      child = create_sdarray_store (meta->branch_prop);
      row_set_object (row, meta->branch_index, child);
      g_object_unref (child);
      child = row_get_object (row, meta->branch_index);
    }
    gtk_single_selection_set_model (branch_sel, G_LIST_MODEL (child));
  } else {
    gtk_single_selection_set_model (branch_sel, NULL);
  }
}


/* ---- row action buttons ------------------------------------------------ */

static GtkSingleSelection *
view_selection (GtkColumnView *view)
{
  return GTK_SINGLE_SELECTION (gtk_column_view_get_model (view));
}

static GListStore *
view_store (GtkColumnView *view)
{
  GtkSingleSelection *sel = view_selection (view);
  GListModel *model = sel ? gtk_single_selection_get_model (sel) : NULL;
  return model ? G_LIST_STORE (model) : NULL;
}

static void
_insert_row_callback (GtkWidget *button, GtkColumnView *view)
{
  GtkSingleSelection *sel = view_selection (view);
  GListStore *store = view_store (view);
  GListStore *sink = g_object_get_data (G_OBJECT (view), "sink");
  DiaSdRow *row;
  guint pos, at, n;

  if (!store) return;

  n = g_list_model_get_n_items (G_LIST_MODEL (store));
  pos = gtk_single_selection_get_selected (sel);
  at = (pos == GTK_INVALID_LIST_POSITION) ? n : pos + 1;

  row = dia_sd_row_new (store);
  g_list_store_insert (store, at, row);
  g_object_unref (row);

  gtk_single_selection_set_selected (sel, at);
  mark_modified (sink);
}

static void
_remove_row_callback (GtkWidget *button, GtkColumnView *view)
{
  GtkSingleSelection *sel = view_selection (view);
  GListStore *store = view_store (view);
  GListStore *sink = g_object_get_data (G_OBJECT (view), "sink");
  guint pos;

  if (!store) return;
  pos = gtk_single_selection_get_selected (sel);
  if (pos == GTK_INVALID_LIST_POSITION) return;

  g_list_store_remove (store, pos);
  mark_modified (sink);
}

static void
_move_row (GtkColumnView *view, gboolean up)
{
  GtkSingleSelection *sel = view_selection (view);
  GListStore *store = view_store (view);
  GListStore *sink = g_object_get_data (G_OBJECT (view), "sink");
  guint pos, n, target;
  DiaSdRow *row;

  if (!store) return;
  pos = gtk_single_selection_get_selected (sel);
  if (pos == GTK_INVALID_LIST_POSITION) return;
  n = g_list_model_get_n_items (G_LIST_MODEL (store));

  if (up) {
    if (pos == 0) return;
    target = pos - 1;
  } else {
    if (pos + 1 >= n) return;
    target = pos + 1;
  }

  row = g_list_model_get_item (G_LIST_MODEL (store), pos);
  g_list_store_remove (store, pos);
  g_list_store_insert (store, target, row);
  g_object_unref (row);

  gtk_single_selection_set_selected (sel, target);
  mark_modified (sink);
}

static void
_upper_row_callback (GtkWidget *button, GtkColumnView *view)
{
  _move_row (view, TRUE);
}

static void
_lower_row_callback (GtkWidget *button, GtkColumnView *view)
{
  _move_row (view, FALSE);
}

static void
_update_button (GtkSingleSelection *selection, GParamSpec *pspec, GtkWidget *button)
{
  gtk_widget_set_sensitive (button,
                            gtk_single_selection_get_selected (selection) != GTK_INVALID_LIST_POSITION);
}

static GtkWidget *
_make_button_box_for_view (GtkColumnView *view, GtkColumnView *master_view)
{
  static struct {
    const char *icon;
    GCallback   callback;
  } _button_data[] = {
    { "list-add-symbolic",    G_CALLBACK (_insert_row_callback) },
    { "list-remove-symbolic", G_CALLBACK (_remove_row_callback) },
    { "go-up-symbolic",       G_CALLBACK (_upper_row_callback) },
    { "go-down-symbolic",     G_CALLBACK (_lower_row_callback) },
    { NULL, NULL }
  };
  GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *button;

  for (int i = 0; _button_data[i].icon != NULL; ++i) {
    button = gtk_button_new_from_icon_name (_button_data[i].icon);
    gtk_widget_set_sensitive (button, FALSE);
    g_signal_connect (button, "clicked", _button_data[i].callback, view);

    if (i != 0) { /* remove / up / down depend on this view's selection */
      g_signal_connect (view_selection (view), "notify::selected",
                        G_CALLBACK (_update_button), button);
    } else if (master_view) { /* branch add depends on the master's selection */
      g_signal_connect (view_selection (master_view), "notify::selected",
                        G_CALLBACK (_update_button), button);
    } else { /* master add is always enabled */
      gtk_widget_set_sensitive (button, TRUE);
    }
    gtk_box_append (GTK_BOX (vbox), button);
  }
  return vbox;
}

static GtkWidget *
_make_scrollable (GtkWidget *view)
{
  GtkWidget *sw = gtk_scrolled_window_new ();

  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (sw), view);
  gtk_widget_set_vexpand (sw, TRUE);
  gtk_widget_set_hexpand (sw, TRUE);

  return sw;
}


/*!
 * PropertyType_GetWidget: create a widget capable of editing the property
 */
WIDGET *
_arrayprop_get_widget (ArrayProperty *prop, PropDialog *dialog)
{
  GListStore *store;
  SdMeta *meta;
  GtkWidget *view;
  GtkSingleSelection *sel;
  GtkWidget *branch_view = NULL;

  store = create_sdarray_store (prop);
  meta = store_meta (store);

  view = gtk_column_view_new (NULL);
  sel = gtk_single_selection_new (G_LIST_MODEL (g_object_ref (store)));
  gtk_single_selection_set_autoselect (sel, FALSE);
  gtk_single_selection_set_can_unselect (sel, TRUE);
  gtk_single_selection_set_selected (sel, GTK_INVALID_LIST_POSITION);
  gtk_column_view_set_model (GTK_COLUMN_VIEW (view), GTK_SELECTION_MODEL (sel));
  g_object_unref (sel);
  gtk_widget_set_vexpand (view, TRUE);

  g_object_set_data (G_OBJECT (view), "master-store", store);
  g_object_set_data (G_OBJECT (view), "sink", store);

  _build_columns (GTK_COLUMN_VIEW (view), meta, store);

  if (meta->branch_index >= 0) {
    GtkSingleSelection *branch_sel;
    SdMeta *branch_meta;
    GListStore *branch_template;

    branch_view = gtk_column_view_new (NULL);
    branch_sel = gtk_single_selection_new (NULL);
    gtk_single_selection_set_autoselect (branch_sel, FALSE);
    gtk_single_selection_set_can_unselect (branch_sel, TRUE);
    gtk_column_view_set_model (GTK_COLUMN_VIEW (branch_view),
                               GTK_SELECTION_MODEL (branch_sel));
    g_object_unref (branch_sel);
    gtk_widget_set_vexpand (branch_view, TRUE);

    /* a store just to describe the branch columns; its SdMeta (referenced by
     * the branch column factories) must outlive the view, so keep it alive. */
    branch_template = create_sdarray_store (meta->branch_prop);
    branch_meta = store_meta (branch_template);
    if (branch_meta->branch_index >= 0) {
      g_warning (G_STRLOC " Only one nesting level of PROP_TYPE_DARRAY supported");
    }
    _build_columns (GTK_COLUMN_VIEW (branch_view), branch_meta, store /* sink */);
    g_object_set_data_full (G_OBJECT (branch_view), "branch-template",
                            branch_template, g_object_unref);

    /* branch edits still record "modified" on the master store */
    g_object_set_data (G_OBJECT (branch_view), "sink", store);
    g_object_set_data (G_OBJECT (view), "branch-sel", branch_sel);
    g_object_set_data (G_OBJECT (view), "branch-view", branch_view);

    g_signal_connect (sel, "notify::selected",
                      G_CALLBACK (_update_branch), view);
  }

  {
    GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *vbox = _make_button_box_for_view (GTK_COLUMN_VIEW (view), NULL);

    gtk_box_append (GTK_BOX (hbox), vbox);
    if (!branch_view) {
      gtk_box_append (GTK_BOX (hbox), _make_scrollable (view));
    } else {
      GtkWidget *hbox2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
      GtkWidget *vbox2 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
      GtkWidget *vbox3 = _make_button_box_for_view (GTK_COLUMN_VIEW (branch_view),
                                                    GTK_COLUMN_VIEW (view));
      GtkWidget *sw_view = _make_scrollable (view);
      GtkWidget *sw_branch = _make_scrollable (branch_view);

      gtk_widget_set_hexpand (sw_view, TRUE);
      gtk_box_append (GTK_BOX (vbox2), sw_view);
      gtk_box_append (GTK_BOX (vbox2), gtk_label_new (_("Parameters")));

      gtk_box_append (GTK_BOX (hbox2), vbox3);
      gtk_widget_set_hexpand (sw_branch, TRUE);
      gtk_box_append (GTK_BOX (hbox2), sw_branch);
      gtk_box_append (GTK_BOX (vbox2), hbox2);
      gtk_widget_set_hexpand (vbox2, TRUE);
      gtk_box_append (GTK_BOX (hbox), vbox2);
    }
    g_object_set_data (G_OBJECT (hbox), "tree-view", view);
    /* keep the master store alive for the lifetime of the widget */
    g_object_set_data_full (G_OBJECT (hbox), "master-store-ref",
                            store, g_object_unref);
    gtk_widget_set_vexpand (hbox, TRUE);
    return hbox;
  }
}


/*!
 * \brief Transfer from the property to the view model
 */
static void
_write_store (GListStore *store, ArrayProperty *prop)
{
  SdMeta *meta = store_meta (store);
  int cols = prop->ex_props->len;
  int rows = prop->records->len;

  for (int j = 0; j < rows; ++j) {
    GPtrArray *r = g_ptr_array_index (prop->records, j);
    DiaSdRow *row = dia_sd_row_new (store);

    for (int i = 0; i < cols; ++i) {
      Property *p = g_ptr_array_index (r, i);

      switch (meta->cols[i].kind) {
        case COL_KIND_DARRAY: {
          GListStore *child = create_sdarray_store ((ArrayProperty *) p);
          _write_store (child, (ArrayProperty *) p);
          row_set_object (row, i, child);
          g_object_unref (child);
          break;
        }
        case COL_KIND_BOOL:
          row_set_bool (row, i, ((BoolProperty *) p)->bool_data);
          break;
        case COL_KIND_INT:
          row_set_int (row, i, ((IntProperty *) p)->int_data);
          break;
        case COL_KIND_ENUM:
          row_set_int (row, i, ((EnumProperty *) p)->enum_data);
          break;
        case COL_KIND_REAL:
          row_set_double (row, i, ((RealProperty *) p)->real_data);
          break;
        case COL_KIND_STRING:
        case COL_KIND_MULTISTRING:
          row_set_string (row, i, ((StringProperty *) p)->string_data);
          break;
        case COL_KIND_UNKNOWN:
        default:
          break;
      }
    }

    g_list_store_append (store, row);
    g_object_unref (row);
  }
}


/*!
 * PropertyType_ResetWidget: get the value of the property into the widget
 */
void
_arrayprop_reset_widget (ArrayProperty *prop, WIDGET *widget)
{
  GtkWidget *view = g_object_get_data (G_OBJECT (widget), "tree-view");
  GListStore *store = g_object_get_data (G_OBJECT (view), "master-store");

  g_list_store_remove_all (store);
  _write_store (store, prop);
  g_object_set_data (G_OBJECT (store), "modified", GINT_TO_POINTER (0));

  /* select the first row if any */
  if (g_list_model_get_n_items (G_LIST_MODEL (store)) > 0) {
    gtk_single_selection_set_selected (view_selection (GTK_COLUMN_VIEW (view)), 0);
  }
}


static gboolean
_array_prop_adjust_len (ArrayProperty *prop, guint len)
{
  guint i;
  guint num_props = prop->ex_props->len;

  if (prop->records->len == len) {
    return FALSE;
  }

  /* see also: pydia-property.c */
  for (i = len; i < prop->records->len; ++i) {
    GPtrArray *record = g_ptr_array_index (prop->records, i);

    for (guint j = 0; j < num_props; j++) {
      Property *inner = g_ptr_array_index (record, j);

      inner->ops->free (inner);
    }
    g_ptr_array_free (record, TRUE);
  }
  for (i = prop->records->len; i < len; ++i) {
    GPtrArray *record = g_ptr_array_new ();

    for (guint j = 0; j < num_props; j++) {
      Property *ex = g_ptr_array_index (prop->ex_props, j);
      Property *inner = ex->ops->copy (ex);

      g_ptr_array_add (record, inner);
    }

    g_ptr_array_add (prop->records, record);
  }
  g_ptr_array_set_size (prop->records, len);

  return TRUE;
}


/*!
 * \brief Transfer from the view model to the property
 */
static void
_read_store (GListStore *store, ArrayProperty *prop)
{
  SdMeta *meta = store_meta (store);
  int cols = prop->ex_props->len;
  guint n = g_list_model_get_n_items (G_LIST_MODEL (store));
  int rows;

  if (_array_prop_adjust_len (prop, n)) {
    g_object_set_data (G_OBJECT (store), "modified", GINT_TO_POINTER (1));
  }
  rows = prop->records->len;

  for (int j = 0; j < rows; ++j) {
    GPtrArray *r = g_ptr_array_index (prop->records, j);
    DiaSdRow *row = g_list_model_get_item (G_LIST_MODEL (store), j);

    for (int i = 0; i < cols; ++i) {
      Property *p = g_ptr_array_index (r, i);

      switch (meta->cols[i].kind) {
        case COL_KIND_DARRAY: {
          GListStore *child = row_get_object (row, i);
          if (child) {
            _read_store (child, (ArrayProperty *) p);
          }
          break;
        }
        case COL_KIND_BOOL:
          ((BoolProperty *) p)->bool_data = row_get_bool (row, i);
          break;
        case COL_KIND_INT:
          ((IntProperty *) p)->int_data = row_get_int (row, i);
          break;
        case COL_KIND_ENUM:
          ((EnumProperty *) p)->enum_data = row_get_int (row, i);
          break;
        case COL_KIND_REAL:
          ((RealProperty *) p)->real_data = row_get_double (row, i);
          break;
        case COL_KIND_STRING:
        case COL_KIND_MULTISTRING: {
          StringProperty *pst = (StringProperty *) p;
          const char *value = row_get_string (row, i);
          g_clear_pointer (&pst->string_data, g_free);
          pst->string_data = g_strdup (value);
          break;
        }
        case COL_KIND_UNKNOWN:
        default:
          break;
      }
    }

    g_object_unref (row);
  }
}


/*!
 * PropertyType_SetFromWidget: set the value of the property from the
 * current value of the widget
 */
void
_arrayprop_set_from_widget (ArrayProperty *prop, WIDGET *widget)
{
  GtkWidget *view = g_object_get_data (G_OBJECT (widget), "tree-view");
  GListStore *store = g_object_get_data (G_OBJECT (view), "master-store");

  _read_store (store, prop);

  /* enough to replace prophandler_connect() ? */
  if (g_object_get_data (G_OBJECT (store), "modified")) {
    prop->common.experience &= ~PXP_NOTSET;
  }
}
