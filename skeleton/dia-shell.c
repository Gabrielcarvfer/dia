/* Dia -- GTK4 + libadwaita port
 *
 * dia-shell: the integrated-UI layout rebuilt with GTK4 widgets, plus the
 * GTK4 event model (event controllers, async dialogs) wired to interactive
 * placeholder behaviour. The real tools/canvas/menus replace these handlers
 * as the app/ port proceeds, but the plumbing (controllers, shared state,
 * queue_draw, async pickers) is the same.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <glib/gi18n.h>

#include "dia-shell.h"

/* Ported core library: draw on the canvas with the real Dia renderer. */
#include "diarenderer.h"
#include "renderer/diacairo.h"
#include "dia-colour.h"
#include "geometry.h"
#include "font.h"
#include "dia-enums.h"
#include "object.h"
#include "diagramdata.h"
#include "dia-layer.h"
#include "dia-object-change.h"
#include "dialib.h"
#include "dia-io.h"
#include "dia_xml.h"
#include "diacontext.h"
#include <libxml/tree.h>

/* Shared shell state so the event handlers can reach the widgets they update.
 * Owns nothing but itself (widget pointers are borrowed); freed with the
 * window via g_object_set_data_full(). */
typedef struct {
  GtkWidget *canvas;
  GtkWidget *hruler;
  GtkWidget *vruler;
  GtkWidget *colour_area;
  GtkWidget *status_msg;
  GtkWidget *pos_label;
  GtkWidget *zoom_label;
  double     cursor_x, cursor_y;  /* pointer position over the canvas (px) */
  gboolean   cursor_valid;
  double     zoom;        /* 1.0 == 100% */
  GdkRGBA    fg;
  GdkRGBA    bg;
  char       tool[64];

  DiagramData *diagram;   /* the real model: a DiagramData with a layer of
                           * DiaObjects, rendered via the Dia cairo renderer */
  DiaObject  *selected;   /* current selection (Modify tool) */
  Point       drag_origin; /* selected object's position at drag start */

  GPtrArray  *undo;       /* UndoOp* — the undo/redo history */
  int         undo_pos;   /* number of ops currently applied (undo[0..pos-1]) */
  GActionGroup *actions;  /* the "dia" group, to toggle undo/redo enabled */
  /* page transform (diagram cm -> widget px), recomputed each draw, so the
   * click handler can map pointer coordinates back to diagram coordinates. */
  double     page_x, page_y, page_scale;
} DiaShell;


/* Standard object types (defined in objects/standard, linked via objects-port).
 * We register them directly rather than through the plugin loader. */
extern DiaObjectType *_box_type;
extern DiaObjectType *_ellipse_type;
extern DiaObjectType *_line_type;
extern DiaObjectType *_textobj_type;
extern DiaObjectType *_polygon_type;
extern DiaObjectType *_beziergon_type;
extern DiaObjectType *_zigzagline_type;
extern DiaObjectType *_polyline_type;
extern DiaObjectType *_bezierline_type;
extern DiaObjectType *_arc_type;
extern DiaObjectType *_image_type;
extern DiaObjectType *_outline_type;

/* Extra object sets (flowchart, network, ER). Their per-set registration file
 * (flowchart.c/network.c/er.c, each a dia_plugin_init) is NOT linked to avoid a
 * symbol clash, so we reference each type struct directly and register it. */
extern DiaObjectType fc_box_type, diamond_type, fc_ellipse_type, pgram_type;
extern DiaObjectType basestation_type, bus_type, radiocell_type, wanlink_type;
extern DiaObjectType attribute_type, entity_type, participation_type, relationship_type;

static void
register_standard_object_types (void)
{
  object_register_type (_box_type);
  object_register_type (_ellipse_type);
  object_register_type (_line_type);
  object_register_type (_textobj_type);
  object_register_type (_polygon_type);
  object_register_type (_beziergon_type);
  object_register_type (_zigzagline_type);
  object_register_type (_polyline_type);
  object_register_type (_bezierline_type);
  object_register_type (_arc_type);
  object_register_type (_image_type);
  object_register_type (_outline_type);

  /* flowchart */
  object_register_type (&fc_box_type);
  object_register_type (&diamond_type);
  object_register_type (&fc_ellipse_type);
  object_register_type (&pgram_type);
  /* network */
  object_register_type (&basestation_type);
  object_register_type (&bus_type);
  object_register_type (&radiocell_type);
  object_register_type (&wanlink_type);
  /* ER */
  object_register_type (&attribute_type);
  object_register_type (&entity_type);
  object_register_type (&participation_type);
  object_register_type (&relationship_type);
}


static guint
diagram_object_count (DiaShell *self)
{
  DiaLayer *layer = dia_diagram_data_get_active_layer (self->diagram);
  return layer ? (guint) dia_layer_object_count (layer) : 0;
}


/* Create a registered object at diagram point @p and add it to the active
 * layer. Returns the object, or NULL if the type isn't registered. */
static DiaObject *
diagram_create_object (DiaShell *self, const char *type_name, Point p)
{
  DiaObjectType *type = object_get_type ((char *) type_name);
  DiaLayer *layer = dia_diagram_data_get_active_layer (self->diagram);
  Handle *h1 = NULL, *h2 = NULL;
  DiaObject *obj;

  if (!type || !layer) {
    return NULL;
  }

  obj = type->ops->create (&p, type->default_user_data, &h1, &h2);
  if (obj) {
    dia_layer_add_object (layer, obj);
  }
  return obj;
}


/* --- undo/redo -----------------------------------------------------------
 * A small history of create/move ops. Removing an object from a layer only
 * unlinks it (it isn't destroyed), so undo/redo can move objects in and out.
 */
typedef enum { OP_CREATE, OP_MOVE } OpKind;

typedef struct {
  OpKind     kind;
  DiaObject *obj;
  Point      from;   /* OP_MOVE: position before */
  Point      to;     /* OP_MOVE: position after */
} UndoOp;

static void
update_undo_actions (DiaShell *self)
{
  GAction *undo = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "undo");
  GAction *redo = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "redo");

  if (undo) {
    g_simple_action_set_enabled (G_SIMPLE_ACTION (undo), self->undo_pos > 0);
  }
  if (redo) {
    g_simple_action_set_enabled (G_SIMPLE_ACTION (redo),
                                 self->undo_pos < (int) self->undo->len);
  }
}

static void
push_op (DiaShell *self, OpKind kind, DiaObject *obj, Point from, Point to)
{
  UndoOp *op = g_new0 (UndoOp, 1);

  op->kind = kind;
  op->obj = obj;
  op->from = from;
  op->to = to;

  /* drop any redo tail, then append */
  while ((int) self->undo->len > self->undo_pos) {
    g_ptr_array_remove_index (self->undo, self->undo->len - 1);
  }
  g_ptr_array_add (self->undo, op);
  self->undo_pos = self->undo->len;
  update_undo_actions (self);
}

static void
op_apply (DiaShell *self, UndoOp *op)   /* redo direction */
{
  DiaLayer *layer = dia_diagram_data_get_active_layer (self->diagram);
  DiaObjectChange *change;

  switch (op->kind) {
    case OP_CREATE:
      dia_layer_add_object (layer, op->obj);
      break;
    case OP_MOVE:
      change = dia_object_move (op->obj, &op->to);
      g_clear_pointer (&change, dia_object_change_unref);
      break;
    default:
      break;
  }
}

static void
op_revert (DiaShell *self, UndoOp *op)  /* undo direction */
{
  DiaLayer *layer = dia_diagram_data_get_active_layer (self->diagram);
  DiaObjectChange *change;

  switch (op->kind) {
    case OP_CREATE:
      if (self->selected == op->obj) {
        self->selected = NULL;
      }
      data_unselect (self->diagram, op->obj);
      dia_layer_remove_object (layer, op->obj);
      break;
    case OP_MOVE:
      change = dia_object_move (op->obj, &op->from);
      g_clear_pointer (&change, dia_object_change_unref);
      break;
    default:
      break;
  }
}


/* The standard tool palette (mirrors app/toolbox.c tool_data). The icon is a
 * GResource path: control tools use app/icons/dia-tool-*, create tools use the
 * object pixmaps (the same images the object types reference). */
typedef struct {
  const char *name;
  const char *tooltip;
  const char *icon;
} ToolEntry;

#define ICON_TOOL(n)   "/org/gnome/Dia/icons/dia-tool-" n ".png"
#define ICON_OBJ(n)    "/org/gnome/Dia/objects/standard/" n ".png"

static const ToolEntry tool_entries[] = {
  { N_("Modify"),     N_("Modify object(s)"),              ICON_TOOL ("modify") },
  { N_("Text edit"),  N_("Edit text (F2)"),                ICON_TOOL ("text") },
  { N_("Magnify"),    N_("Magnify (M)"),                   ICON_TOOL ("zoom") },
  { N_("Scroll"),     N_("Scroll around the diagram (S)"), ICON_TOOL ("scroll") },
  { N_("Text"),       N_("Create a text object (T)"),      ICON_OBJ ("text") },
  { N_("Box"),        N_("Create a box (R)"),              ICON_OBJ ("box") },
  { N_("Ellipse"),    N_("Create an ellipse (E)"),         ICON_OBJ ("ellipse") },
  { N_("Polygon"),    N_("Create a polygon (P)"),          ICON_OBJ ("polygon") },
  { N_("Beziergon"),  N_("Create a beziergon (B)"),        ICON_OBJ ("beziergon") },
  { N_("Line"),       N_("Create a line (L)"),             ICON_OBJ ("line") },
  { N_("Arc"),        N_("Create an arc (A)"),             ICON_OBJ ("arc") },
  { N_("Zigzag"),     N_("Create a zigzag line (Z)"),      ICON_OBJ ("zigzagline") },
  { N_("Polyline"),   N_("Create a polyline"),             ICON_OBJ ("polyline") },
  { N_("Bezier"),     N_("Create a bezier line (C)"),      ICON_OBJ ("bezierline") },
  { N_("Image"),      N_("Insert an image (I)"),           ICON_OBJ ("image") },
  { N_("Outline"),    N_("Create an outline"),             ICON_OBJ ("outline") },
};


/* Give a widget a stable accessible name so the dogtail UI tests can find it
 * (GTK4 exposes this over AT-SPI). */
static void
set_a11y_label (GtkWidget *widget, const char *label)
{
  gtk_accessible_update_property (GTK_ACCESSIBLE (widget),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL, label,
                                  -1);
}


/* --- drawing callbacks --------------------------------------------------- */

/* Render the DiagramData onto the page using the REAL Dia renderer + the real
 * data_render() pipeline. The cairo context is already transformed into
 * diagram (cm) space. data_render() drives begin/end for a non-interactive
 * renderer and draws each visible layer's objects. */
static void
draw_diagram (cairo_t *cr, DiagramData *diagram)
{
  DiaRenderer *renderer;
  DiaCairoRenderer *cairo_renderer;

  renderer = g_object_new (DIA_CAIRO_TYPE_RENDERER, NULL);
  cairo_renderer = DIA_CAIRO_RENDERER (renderer);
  cairo_renderer->cr = cairo_reference (cr);
  cairo_renderer->with_alpha = TRUE;

  data_render (diagram, renderer, NULL, NULL, NULL);

  g_object_unref (renderer);
}


/* Draw small squares at each selected object's handles (the interactive
 * renderer normally does this; we draw them directly here). cr is in cm. */
static void
draw_selection_handles (cairo_t *cr, DiagramData *diagram)
{
  const double s = 0.12;   /* half handle size, cm */

  for (GList *l = diagram->selected; l; l = l->next) {
    DiaObject *obj = l->data;

    for (int i = 0; i < obj->num_handles; i++) {
      Handle *h = obj->handles[i];
      cairo_rectangle (cr, h->pos.x - s, h->pos.y - s, 2 * s, 2 * s);
    }
    cairo_set_source_rgb (cr, 0.10, 0.45, 0.90);
    cairo_fill_preserve (cr);
    cairo_set_source_rgb (cr, 1, 1, 1);
    cairo_set_line_width (cr, 0.03);
    cairo_stroke (cr);
  }
}


static void
draw_canvas (GtkDrawingArea *area,
             cairo_t        *cr,
             int             width,
             int             height,
             gpointer        user_data)
{
  DiaShell *self = user_data;
  double zoom = self ? self->zoom : 1.0;
  double page_w, page_h, px, py;

  cairo_set_source_rgb (cr, 0.6, 0.6, 0.62);
  cairo_paint (cr);

  page_w = CLAMP (width * 0.7 * zoom, 20, width * 4);
  page_h = CLAMP (height * 0.82 * zoom, 20, height * 4);
  px = (width - page_w) / 2.0;
  py = (height - page_h) / 2.0;

  cairo_rectangle (cr, px + 3, py + 3, page_w, page_h);
  cairo_set_source_rgba (cr, 0, 0, 0, 0.25);
  cairo_fill (cr);

  cairo_rectangle (cr, px, py, page_w, page_h);
  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_fill_preserve (cr);
  cairo_set_source_rgb (cr, 0.4, 0.4, 0.4);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);

  /* Remember the page transform so clicks can be mapped back to cm. */
  if (self) {
    self->page_x = px;
    self->page_y = py;
    self->page_scale = page_w / 20.0;   /* a 20cm-wide virtual page */
  }

  /* Render the diagram shapes in page coordinates (cm), clipped to the page. */
  cairo_save (cr);
  cairo_rectangle (cr, px, py, page_w, page_h);
  cairo_clip (cr);
  cairo_translate (cr, px, py);
  cairo_scale (cr, page_w / 20.0, page_w / 20.0);
  if (self && self->diagram) {
    draw_diagram (cr, self->diagram);
    draw_selection_handles (cr, self->diagram);
  }
  cairo_restore (cr);
}


static void
draw_ruler (GtkDrawingArea *area,
            cairo_t        *cr,
            int             width,
            int             height,
            gpointer        user_data)
{
  DiaShell *self = user_data;
  gboolean horizontal = (GTK_WIDGET (area) == self->hruler);
  int extent = horizontal ? width : height;
  int i;

  cairo_set_source_rgb (cr, 0.93, 0.93, 0.93);
  cairo_paint (cr);

  cairo_set_source_rgb (cr, 0.5, 0.5, 0.5);
  cairo_set_line_width (cr, 1.0);

  for (i = 0; i < extent; i += 10) {
    double t = (i % 50 == 0) ? 0.0 : (height / 2.0);
    if (horizontal) {
      cairo_move_to (cr, i + 0.5, t);
      cairo_line_to (cr, i + 0.5, height);
    } else {
      cairo_move_to (cr, t, i + 0.5);
      cairo_line_to (cr, width, i + 0.5);
    }
  }
  cairo_stroke (cr);

  /* Cursor-tracking marker: a small black triangle at the pointer's position
   * along the ruler (the canvas and rulers share a grid column/row, so the
   * canvas-relative pointer coordinate maps straight onto the ruler). */
  if (self->cursor_valid) {
    cairo_set_source_rgb (cr, 0, 0, 0);
    if (horizontal) {
      double x = self->cursor_x;
      cairo_move_to (cr, x, height);
      cairo_line_to (cr, x - 4, height - 7);
      cairo_line_to (cr, x + 4, height - 7);
    } else {
      double y = self->cursor_y;
      cairo_move_to (cr, width, y);
      cairo_line_to (cr, width - 7, y - 4);
      cairo_line_to (cr, width - 7, y + 4);
    }
    cairo_close_path (cr);
    cairo_fill (cr);
  }
}


static void
draw_color_area (GtkDrawingArea *area,
                 cairo_t        *cr,
                 int             width,
                 int             height,
                 gpointer        user_data)
{
  DiaShell *self = user_data;
  double s = MIN (width, height) * 0.62;

  cairo_rectangle (cr, width - s, height - s, s, s);
  gdk_cairo_set_source_rgba (cr, &self->bg);
  cairo_fill_preserve (cr);
  cairo_set_source_rgb (cr, 0.3, 0.3, 0.3);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);

  cairo_rectangle (cr, 0, 0, s, s);
  gdk_cairo_set_source_rgba (cr, &self->fg);
  cairo_fill_preserve (cr);
  cairo_set_source_rgb (cr, 0.3, 0.3, 0.3);
  cairo_stroke (cr);
}


/* --- event handlers ------------------------------------------------------ */

static void
on_tool_toggled (GtkToggleButton *button, DiaShell *self)
{
  const char *name;

  if (!gtk_toggle_button_get_active (button))
    return;

  name = g_object_get_data (G_OBJECT (button), "tool-name");
  g_strlcpy (self->tool, name ? name : "", sizeof (self->tool));
  gtk_label_set_text (GTK_LABEL (self->status_msg), self->tool);
}


static void
on_canvas_motion (GtkEventControllerMotion *controller,
                  double                    x,
                  double                    y,
                  DiaShell                 *self)
{
  char buf[64];

  /* track the pointer so the rulers can draw their position markers */
  self->cursor_x = x;
  self->cursor_y = y;
  self->cursor_valid = TRUE;
  if (self->hruler) gtk_widget_queue_draw (self->hruler);
  if (self->vruler) gtk_widget_queue_draw (self->vruler);

  /* report diagram (cm) coordinates: inverse of the page transform */
  if (self->page_scale > 0.0) {
    g_snprintf (buf, sizeof (buf), "%.1f, %.1f",
                (x - self->page_x) / self->page_scale,
                (y - self->page_y) / self->page_scale);
  } else {
    g_snprintf (buf, sizeof (buf), "%.1f, %.1f", x, y);
  }
  gtk_label_set_text (GTK_LABEL (self->pos_label), buf);
}


/* Map the current tool name to a registered DiaObjectType name; returns NULL
 * for non-create tools (Modify/Magnify/Scroll/…). */
static const char *
tool_to_type_name (const char *tool)
{
  if (g_strcmp0 (tool, "Box") == 0)      return "Standard - Box";
  if (g_strcmp0 (tool, "Ellipse") == 0)  return "Standard - Ellipse";
  if (g_strcmp0 (tool, "Line") == 0)     return "Standard - Line";
  if (g_strcmp0 (tool, "Text") == 0)     return "Standard - Text";
  if (g_strcmp0 (tool, "Polygon") == 0)  return "Standard - Polygon";
  if (g_strcmp0 (tool, "Beziergon") == 0) return "Standard - Beziergon";
  if (g_strcmp0 (tool, "Zigzag") == 0)   return "Standard - ZigZagLine";
  if (g_strcmp0 (tool, "Polyline") == 0) return "Standard - PolyLine";
  if (g_strcmp0 (tool, "Bezier") == 0)   return "Standard - BezierLine";
  if (g_strcmp0 (tool, "Arc") == 0)      return "Standard - Arc";
  if (g_strcmp0 (tool, "Image") == 0)    return "Standard - Image";
  if (g_strcmp0 (tool, "Outline") == 0)  return "Standard - Outline";
  return NULL;
}


/* Apply the current tool at diagram point @p: create a real DiaObject (create
 * tools) or just report the position. Shared by the canvas gesture and the
 * UI-test trigger so both exercise the same code path. */
static void
apply_tool_at (DiaShell *self, Point p)
{
  char buf[128];
  const char *type_name = tool_to_type_name (self->tool);
  DiaObject *obj = type_name ? diagram_create_object (self, type_name, p) : NULL;

  if (obj) {
    push_op (self, OP_CREATE, obj, p, p);
    gtk_widget_queue_draw (self->canvas);
    g_snprintf (buf, sizeof (buf),
                _("%s created at (%.1f, %.1f) — %u object(s)"),
                self->tool, p.x, p.y, diagram_object_count (self));
  } else {
    g_snprintf (buf, sizeof (buf), _("%s at (%.1f, %.1f)"),
                self->tool[0] ? self->tool : _("Click"), p.x, p.y);
  }

  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
}


/* Modify tool: hit-test the object under @p and make it the selection. */
static void
select_at (DiaShell *self, Point p)
{
  DiaLayer *layer = dia_diagram_data_get_active_layer (self->diagram);
  DiaObject *obj = layer ? dia_layer_find_closest_object (layer, &p, 0.5) : NULL;
  char buf[96];

  data_remove_all_selected (self->diagram);
  self->selected = obj;
  if (obj) {
    data_select (self->diagram, obj);
    g_snprintf (buf, sizeof (buf), _("Selected %s"), obj->type->name);
  } else {
    g_snprintf (buf, sizeof (buf), _("Nothing selected"));
  }
  gtk_widget_queue_draw (self->canvas);
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
}


static void
on_canvas_pressed (GtkGestureClick *gesture,
                   int              n_press,
                   double           x,
                   double           y,
                   DiaShell        *self)
{
  Point p;

  /* widget pixels -> diagram cm (inverse of the page transform) */
  if (self->page_scale <= 0.0) {
    return;
  }
  p.x = (x - self->page_x) / self->page_scale;
  p.y = (y - self->page_y) / self->page_scale;

  /* Modify selects; the create tools place an object. */
  if (g_strcmp0 (self->tool, "Modify") == 0) {
    select_at (self, p);
  } else {
    apply_tool_at (self, p);
  }
}


/* Drag with the Modify tool moves the selected object. */
static void
on_canvas_drag_begin (GtkGestureDrag *gesture,
                      double          start_x,
                      double          start_y,
                      DiaShell       *self)
{
  if (g_strcmp0 (self->tool, "Modify") == 0 && self->selected) {
    self->drag_origin = self->selected->position;
  }
}

static void
on_canvas_drag_update (GtkGestureDrag *gesture,
                       double          offset_x,
                       double          offset_y,
                       DiaShell       *self)
{
  DiaObjectChange *change;
  Point to;

  if (g_strcmp0 (self->tool, "Modify") != 0 || !self->selected ||
      self->page_scale <= 0.0) {
    return;
  }

  to.x = self->drag_origin.x + offset_x / self->page_scale;
  to.y = self->drag_origin.y + offset_y / self->page_scale;

  change = dia_object_move (self->selected, &to);
  g_clear_pointer (&change, dia_object_change_unref);
  gtk_widget_queue_draw (self->canvas);
}

static void
on_canvas_drag_end (GtkGestureDrag *gesture,
                    double          offset_x,
                    double          offset_y,
                    DiaShell       *self)
{
  Point to;

  if (g_strcmp0 (self->tool, "Modify") != 0 || !self->selected ||
      self->page_scale <= 0.0) {
    return;
  }

  /* Record the whole drag as a single undoable move. */
  to.x = self->drag_origin.x + offset_x / self->page_scale;
  to.y = self->drag_origin.y + offset_y / self->page_scale;
  if (to.x != self->drag_origin.x || to.y != self->drag_origin.y) {
    push_op (self, OP_MOVE, self->selected, self->drag_origin, to);
  }
}


/* UI-test hook: synthetic input isn't deliverable under WSLg, so when
 * DIA_UITEST is set we expose a button that applies the current tool at a
 * fixed page point — the same path a real canvas click takes — so the dogtail
 * suite can verify tool->shape creation via an AT-SPI action. */
static void
on_uitest_apply_tool (GtkButton *button, DiaShell *self)
{
  apply_tool_at (self, (Point){ 8.0, 6.0 });
  gtk_widget_queue_draw (self->canvas);
}


static void
update_zoom (DiaShell *self, double factor)
{
  char buf[32];

  self->zoom = CLAMP (self->zoom * factor, 0.1, 20.0);
  g_snprintf (buf, sizeof (buf), "%.0f%%", self->zoom * 100.0);
  gtk_label_set_text (GTK_LABEL (self->zoom_label), buf);
  gtk_widget_queue_draw (self->canvas);
}

/* --- GActions (the "dia" action group, installed on the window content) -- */

static void
action_zoom_in (GSimpleAction *a, GVariant *p, gpointer data)
{
  update_zoom (data, 1.5);
}

static void
action_zoom_out (GSimpleAction *a, GVariant *p, gpointer data)
{
  update_zoom (data, 1.0 / 1.5);
}

static void
action_zoom_reset (GSimpleAction *a, GVariant *p, gpointer data)
{
  DiaShell *self = data;
  self->zoom = 1.0;
  update_zoom (self, 1.0);
}

static void
dia_shell_set_new_diagram (DiaShell *self)
{
  g_clear_object (&self->diagram);
  self->diagram = g_object_new (DIA_TYPE_DIAGRAM_DATA, NULL);
}

static void
action_undo (GSimpleAction *a, GVariant *p, gpointer data)
{
  DiaShell *self = data;

  if (self->undo_pos > 0) {
    self->undo_pos--;
    op_revert (self, g_ptr_array_index (self->undo, self->undo_pos));
    update_undo_actions (self);
    gtk_widget_queue_draw (self->canvas);
    gtk_label_set_text (GTK_LABEL (self->status_msg), _("Undo"));
  }
}

static void
action_redo (GSimpleAction *a, GVariant *p, gpointer data)
{
  DiaShell *self = data;

  if (self->undo_pos < (int) self->undo->len) {
    op_apply (self, g_ptr_array_index (self->undo, self->undo_pos));
    self->undo_pos++;
    update_undo_actions (self);
    gtk_widget_queue_draw (self->canvas);
    gtk_label_set_text (GTK_LABEL (self->status_msg), _("Redo"));
  }
}

static void
action_new (GSimpleAction *a, GVariant *p, gpointer data)
{
  DiaShell *self = data;

  dia_shell_set_new_diagram (self);
  g_ptr_array_set_size (self->undo, 0);   /* clear history */
  self->undo_pos = 0;
  update_undo_actions (self);
  self->selected = NULL;
  gtk_widget_queue_draw (self->canvas);
  gtk_label_set_text (GTK_LABEL (self->status_msg),
                      _("New diagram — 0 object(s)"));
}


/* Write the diagram to a real .dia file: a <dia:diagram> with a <dia:layer>
 * whose <dia:object>s are serialized by each object's own save vfunc (the same
 * primitives app/load_save.c uses), then written via dia-io (handles encoding
 * and optional gzip). Produces files loadable by upstream Dia. */
static gboolean
diagram_to_file (DiaShell *self, GFile *file)
{
  DiaContext *ctx = dia_context_new ("Save Diagram");
  xmlDocPtr doc = xmlNewDoc ((const xmlChar *) "1.0");
  xmlNodePtr root, layer_node;
  xmlNsPtr ns;
  DiaLayer *layer = dia_diagram_data_get_active_layer (self->diagram);
  char *path = g_file_get_path (file);
  int n = layer ? dia_layer_object_count (layer) : 0;
  gboolean ok;

  doc->encoding = xmlStrdup ((const xmlChar *) "UTF-8");
  root = xmlNewDocNode (doc, NULL, (const xmlChar *) "diagram", NULL);
  xmlDocSetRootElement (doc, root);
  ns = xmlNewNs (root, (const xmlChar *) DIA_XML_NAME_SPACE_BASE,
                 (const xmlChar *) "dia");
  xmlSetNs (root, ns);

  layer_node = xmlNewChild (root, ns, (const xmlChar *) "layer", NULL);
  xmlSetProp (layer_node, (const xmlChar *) "name", (const xmlChar *) "Background");
  xmlSetProp (layer_node, (const xmlChar *) "visible", (const xmlChar *) "true");

  for (int i = 0; i < n; i++) {
    DiaObject *obj = dia_layer_object_get_nth (layer, i);
    xmlNodePtr obj_node = xmlNewChild (layer_node, ns,
                                       (const xmlChar *) "object", NULL);
    char buf[16];

    xmlSetProp (obj_node, (const xmlChar *) "type",
                (const xmlChar *) obj->type->name);
    g_snprintf (buf, sizeof (buf), "%d", obj->type->version);
    xmlSetProp (obj_node, (const xmlChar *) "version", (const xmlChar *) buf);
    g_snprintf (buf, sizeof (buf), "O%d", i);
    xmlSetProp (obj_node, (const xmlChar *) "id", (const xmlChar *) buf);

    obj->type->ops->save (obj, obj_node, ctx);
  }

  ok = dia_io_save_document (path, doc, FALSE, ctx);

  xmlFreeDoc (doc);
  dia_context_release (ctx);
  g_free (path);
  return ok;
}


static void
diagram_from_file (DiaShell *self, GFile *file)
{
  DiaContext *ctx = dia_context_new ("Load Diagram");
  char *path = g_file_get_path (file);
  xmlDocPtr doc = dia_io_load_document (path, ctx, NULL);

  if (doc) {
    xmlNodePtr root = xmlDocGetRootElement (doc);
    DiaLayer *layer;

    dia_shell_set_new_diagram (self);
    layer = dia_diagram_data_get_active_layer (self->diagram);

    for (xmlNodePtr ln = root ? root->children : NULL; ln; ln = ln->next) {
      if (xmlStrcmp (ln->name, (const xmlChar *) "layer") != 0) {
        continue;
      }
      for (xmlNodePtr on = ln->children; on; on = on->next) {
        char *type_name, *version_str;
        DiaObjectType *type;
        DiaObject *obj;

        if (xmlStrcmp (on->name, (const xmlChar *) "object") != 0) {
          continue;
        }
        type_name = (char *) xmlGetProp (on, (const xmlChar *) "type");
        version_str = (char *) xmlGetProp (on, (const xmlChar *) "version");
        type = type_name ? object_get_type (type_name) : NULL;

        if (type && type->ops->load) {
          obj = type->ops->load (on, version_str ? atoi (version_str) : 0, ctx);
          if (obj) {
            dia_layer_add_object (layer, obj);
          }
        }
        if (type_name) xmlFree (type_name);
        if (version_str) xmlFree (version_str);
      }
    }
    xmlFreeDoc (doc);
    gtk_widget_queue_draw (self->canvas);
  }

  dia_context_release (ctx);
  g_free (path);
}


/* UI-test hook (DIA_UITEST): verify real .dia I/O without the file chooser —
 * save the diagram, clear it, reload it, and report whether the object count
 * survived the round-trip. */
static void
on_uitest_roundtrip (GtkButton *button, DiaShell *self)
{
  GFile *file = g_file_new_for_path ("/tmp/dia-uitest-roundtrip.dia");
  guint before, after;
  char buf[96];

  if (diagram_object_count (self) == 0) {
    diagram_create_object (self, "Standard - Box", (Point) { 5, 5 });
  }
  before = diagram_object_count (self);

  diagram_to_file (self, file);
  dia_shell_set_new_diagram (self);
  diagram_from_file (self, file);
  after = diagram_object_count (self);

  if (before == after && after > 0) {
    g_snprintf (buf, sizeof (buf), _("round-trip OK (%u -> %u objects)"),
                before, after);
  } else {
    g_snprintf (buf, sizeof (buf), _("round-trip FAIL (%u -> %u objects)"),
                before, after);
  }
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
  gtk_widget_queue_draw (self->canvas);
  g_object_unref (file);
}


/* UI-test hook (DIA_UITEST): select the first object and move it, verifying the
 * Modify tool's select + move logic without synthesized pointer input. */
static void
on_uitest_select_move (GtkButton *button, DiaShell *self)
{
  DiaLayer *layer = dia_diagram_data_get_active_layer (self->diagram);
  DiaObject *obj;
  Point before, to;
  DiaObjectChange *change;
  char buf[96];

  if (!layer || dia_layer_object_count (layer) == 0) {
    diagram_create_object (self, "Standard - Box", (Point) { 5, 5 });
    layer = dia_diagram_data_get_active_layer (self->diagram);
  }

  obj = dia_layer_object_get_nth (layer, 0);
  before = obj->position;
  to.x = before.x + 3.0;
  to.y = before.y + 2.0;

  data_remove_all_selected (self->diagram);
  data_select (self->diagram, obj);
  self->selected = obj;
  change = dia_object_move (obj, &to);
  g_clear_pointer (&change, dia_object_change_unref);

  if (obj->position.x != before.x || obj->position.y != before.y) {
    g_snprintf (buf, sizeof (buf), _("select+move OK (now %.1f, %.1f)"),
                obj->position.x, obj->position.y);
  } else {
    g_snprintf (buf, sizeof (buf), _("select+move FAIL"));
  }
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
  gtk_widget_queue_draw (self->canvas);
}


/* UI-test hook (DIA_UITEST): create -> undo -> redo, verifying the object count
 * goes N -> N+1 -> N -> N+1. */
static void
on_uitest_undo_redo (GtkButton *button, DiaShell *self)
{
  guint c0, c1, c2, c3;
  DiaObject *obj;
  char buf[96];

  c0 = diagram_object_count (self);
  obj = diagram_create_object (self, "Standard - Box", (Point) { 6, 6 });
  if (obj) {
    push_op (self, OP_CREATE, obj, (Point) { 6, 6 }, (Point) { 6, 6 });
  }
  c1 = diagram_object_count (self);

  if (self->undo_pos > 0) {
    self->undo_pos--;
    op_revert (self, g_ptr_array_index (self->undo, self->undo_pos));
    update_undo_actions (self);
  }
  c2 = diagram_object_count (self);

  if (self->undo_pos < (int) self->undo->len) {
    op_apply (self, g_ptr_array_index (self->undo, self->undo_pos));
    self->undo_pos++;
    update_undo_actions (self);
  }
  c3 = diagram_object_count (self);

  if (c1 == c0 + 1 && c2 == c0 && c3 == c1) {
    g_snprintf (buf, sizeof (buf), _("undo/redo OK (%u/%u/%u/%u)"), c0, c1, c2, c3);
  } else {
    g_snprintf (buf, sizeof (buf), _("undo/redo FAIL (%u/%u/%u/%u)"), c0, c1, c2, c3);
  }
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
  gtk_widget_queue_draw (self->canvas);
}


/* UI-test hook (DIA_UITEST): create one object from each extra set (flowchart,
 * network, ER) and verify every type resolves and lands on the canvas. Proves
 * the extra object sets are registered and instantiable. */
static void
on_uitest_extra_objects (GtkButton *button, DiaShell *self)
{
  static const char *types[] = {
    "Flowchart - Box", "Flowchart - Diamond",
    "Network - Bus", "Network - Base Station",
    "ER - Entity", "ER - Relationship",
  };
  guint c0 = diagram_object_count (self);
  guint made = 0;
  char buf[128];

  for (gsize i = 0; i < G_N_ELEMENTS (types); i++) {
    Point p = { 3.0 + i * 2.0, 3.0 };
    DiaObject *obj = diagram_create_object (self, types[i], p);
    if (obj) {
      push_op (self, OP_CREATE, obj, p, p);
      made++;
    }
  }

  if (made == G_N_ELEMENTS (types)
      && diagram_object_count (self) == c0 + made) {
    g_snprintf (buf, sizeof (buf), _("extra-objects OK (%u types)"), made);
  } else {
    g_snprintf (buf, sizeof (buf), _("extra-objects FAIL (%u/%u)"),
                made, (guint) G_N_ELEMENTS (types));
  }
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
  gtk_widget_queue_draw (self->canvas);
}


static void
save_done (GObject *source, GAsyncResult *res, gpointer data)
{
  DiaShell *self = data;
  GFile *file = gtk_file_dialog_save_finish (GTK_FILE_DIALOG (source), res, NULL);

  if (file) {
    gboolean ok = diagram_to_file (self, file);
    gtk_label_set_text (GTK_LABEL (self->status_msg),
                        ok ? _("Saved diagram") : _("Save failed"));
    g_object_unref (file);
  }
}


/* Restrict the file dialog to *.dia (plus an all-files entry). */
static void
set_dia_filter (GtkFileDialog *dialog)
{
  GListStore *filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  GtkFileFilter *dia = gtk_file_filter_new ();
  GtkFileFilter *all = gtk_file_filter_new ();

  gtk_file_filter_set_name (dia, _("Dia diagrams (*.dia)"));
  gtk_file_filter_add_pattern (dia, "*.dia");
  gtk_file_filter_set_name (all, _("All files"));
  gtk_file_filter_add_pattern (all, "*");

  g_list_store_append (filters, dia);
  g_list_store_append (filters, all);
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));

  g_object_unref (dia);
  g_object_unref (all);
  g_object_unref (filters);
}

static void
action_save (GSimpleAction *a, GVariant *p, gpointer data)
{
  DiaShell *self = data;
  GtkFileDialog *dialog = gtk_file_dialog_new ();
  GtkRoot *root = gtk_widget_get_root (self->canvas);

  gtk_file_dialog_set_title (dialog, _("Save Diagram"));
  gtk_file_dialog_set_modal (dialog, FALSE);   /* avoid stuck modal grabs */
  set_dia_filter (dialog);
  gtk_file_dialog_save (dialog, GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL,
                        NULL, save_done, self);
  g_object_unref (dialog);
}


static void
open_done (GObject *source, GAsyncResult *res, gpointer data)
{
  DiaShell *self = data;
  GFile *file = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (source), res, NULL);

  if (file) {
    char buf[64];

    diagram_from_file (self, file);
    g_snprintf (buf, sizeof (buf), _("Opened diagram — %u object(s)"),
                diagram_object_count (self));
    gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
    g_object_unref (file);
  }
}

static void
action_open (GSimpleAction *a, GVariant *p, gpointer data)
{
  DiaShell *self = data;
  GtkFileDialog *dialog = gtk_file_dialog_new ();
  GtkRoot *root = gtk_widget_get_root (self->canvas);

  gtk_file_dialog_set_title (dialog, _("Open Diagram"));
  gtk_file_dialog_set_modal (dialog, FALSE);   /* avoid stuck modal grabs */
  set_dia_filter (dialog);
  gtk_file_dialog_open (dialog, GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL,
                        NULL, open_done, self);
  g_object_unref (dialog);
}


static const GActionEntry dia_actions[] = {
  { "new",        action_new,        NULL, NULL, NULL },
  { "open",       action_open,       NULL, NULL, NULL },
  { "save",       action_save,       NULL, NULL, NULL },
  { "undo",       action_undo,       NULL, NULL, NULL },
  { "redo",       action_redo,       NULL, NULL, NULL },
  { "zoom-in",    action_zoom_in,    NULL, NULL, NULL },
  { "zoom-out",   action_zoom_out,   NULL, NULL, NULL },
  { "zoom-reset", action_zoom_reset, NULL, NULL, NULL },
};

static void
dia_shell_install_actions (DiaShell *self, GtkWidget *view)
{
  GSimpleActionGroup *group = g_simple_action_group_new ();

  g_action_map_add_action_entries (G_ACTION_MAP (group), dia_actions,
                                   G_N_ELEMENTS (dia_actions), self);
  gtk_widget_insert_action_group (view, "dia", G_ACTION_GROUP (group));
  /* keep a borrowed ref so undo/redo can be enabled/disabled */
  self->actions = G_ACTION_GROUP (group);
  g_object_unref (group);
  update_undo_actions (self);   /* both start disabled */
}


static void
on_colour_chosen (GObject *source, GAsyncResult *result, gpointer user_data)
{
  DiaShell *self = user_data;
  GdkRGBA *rgba;

  rgba = gtk_color_dialog_choose_rgba_finish (GTK_COLOR_DIALOG (source),
                                              result, NULL);
  if (rgba) {
    self->fg = *rgba;
    gdk_rgba_free (rgba);
    gtk_widget_queue_draw (self->colour_area);
  }
}


static void
on_colour_clicked (GtkButton *button, DiaShell *self)
{
  GtkColorDialog *dialog = gtk_color_dialog_new ();
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (button));

  gtk_color_dialog_set_title (dialog, _("Foreground Colour"));
  gtk_color_dialog_choose_rgba (dialog,
                                GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL,
                                &self->fg,
                                NULL,
                                on_colour_chosen,
                                self);
  g_object_unref (dialog);
}


/* --- builders ------------------------------------------------------------ */

static GtkWidget *
build_primary_menu_button (void)
{
  GtkWidget *button = gtk_menu_button_new ();
  GMenu *menu = g_menu_new ();
  GMenu *file = g_menu_new ();
  GMenu *view = g_menu_new ();
  GMenu *app = g_menu_new ();

  g_menu_append (file, _("_New"), "dia.new");
  g_menu_append (file, _("_Open…"), "dia.open");
  g_menu_append (file, _("_Save…"), "dia.save");
  g_menu_append_section (menu, NULL, G_MENU_MODEL (file));

  g_menu_append (view, _("Zoom _In"), "dia.zoom-in");
  g_menu_append (view, _("Zoom _Out"), "dia.zoom-out");
  g_menu_append (view, _("_Reset Zoom"), "dia.zoom-reset");
  g_menu_append_section (menu, NULL, G_MENU_MODEL (view));

  g_menu_append (app, _("_About Dia"), "app.about");
  g_menu_append (app, _("_Quit"), "app.quit");
  g_menu_append_section (menu, NULL, G_MENU_MODEL (app));

  gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (button), "open-menu-symbolic");
  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (button), G_MENU_MODEL (menu));

  g_object_unref (menu);
  g_object_unref (file);
  g_object_unref (view);
  g_object_unref (app);

  return button;
}


static GtkWidget *
build_action_toolbar (DiaShell *self)
{
  GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  /* GTK4: buttons drive GActions by name (the "dia" group, installed on the
   * window content) rather than direct callbacks. */
  struct { const char *icon; const char *tip; const char *action; } items[] = {
    { "document-new-symbolic",   N_("New diagram"), "dia.new" },
    { "document-open-symbolic",  N_("Open"),        "dia.open" },
    { "document-save-symbolic",  N_("Save"),        "dia.save" },
    { NULL, NULL, NULL },
    { "edit-undo-symbolic",      N_("Undo"),        "dia.undo" },
    { "edit-redo-symbolic",      N_("Redo"),        "dia.redo" },
    { NULL, NULL, NULL },
    { "zoom-in-symbolic",        N_("Zoom in"),     "dia.zoom-in" },
    { "zoom-out-symbolic",       N_("Zoom out"),    "dia.zoom-out" },
    { "zoom-original-symbolic",  N_("Zoom 1:1"),    "dia.zoom-reset" },
  };

  gtk_widget_add_css_class (bar, "toolbar");
  gtk_widget_set_margin_start (bar, 3);
  gtk_widget_set_margin_end (bar, 3);

  for (gsize i = 0; i < G_N_ELEMENTS (items); i++) {
    GtkWidget *b;
    if (items[i].icon == NULL) {
      gtk_box_append (GTK_BOX (bar),
                      gtk_separator_new (GTK_ORIENTATION_VERTICAL));
      continue;
    }
    b = gtk_button_new_from_icon_name (items[i].icon);
    gtk_button_set_has_frame (GTK_BUTTON (b), FALSE);
    gtk_widget_set_tooltip_text (b, gettext (items[i].tip));
    set_a11y_label (b, gettext (items[i].tip));
    if (items[i].action)
      gtk_actionable_set_action_name (GTK_ACTIONABLE (b), items[i].action);
    else
      gtk_widget_set_sensitive (b, FALSE);   /* Undo/Redo: not wired yet */
    gtk_box_append (GTK_BOX (bar), b);
  }

  /* UI-test-only trigger (see on_uitest_apply_tool). Absent in normal use.
   * The label IS the AT-SPI name the test searches for. */
  if (g_getenv ("DIA_UITEST")) {
    GtkWidget *t = gtk_button_new_with_label ("uitest-apply-tool");
    GtkWidget *r = gtk_button_new_with_label ("uitest-roundtrip");
    GtkWidget *m = gtk_button_new_with_label ("uitest-select-move");
    GtkWidget *u = gtk_button_new_with_label ("uitest-undo-redo");
    GtkWidget *x = gtk_button_new_with_label ("uitest-extra-objects");
    gtk_button_set_has_frame (GTK_BUTTON (t), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (r), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (m), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (u), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (x), FALSE);
    g_signal_connect (t, "clicked", G_CALLBACK (on_uitest_apply_tool), self);
    g_signal_connect (r, "clicked", G_CALLBACK (on_uitest_roundtrip), self);
    g_signal_connect (m, "clicked", G_CALLBACK (on_uitest_select_move), self);
    g_signal_connect (u, "clicked", G_CALLBACK (on_uitest_undo_redo), self);
    g_signal_connect (x, "clicked", G_CALLBACK (on_uitest_extra_objects), self);
    gtk_box_append (GTK_BOX (bar), t);
    gtk_box_append (GTK_BOX (bar), r);
    gtk_box_append (GTK_BOX (bar), m);
    gtk_box_append (GTK_BOX (bar), u);
    gtk_box_append (GTK_BOX (bar), x);
  }

  return bar;
}


static GtkWidget *
build_toolbox (DiaShell *self)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  GtkWidget *grid = gtk_grid_new ();
  GtkWidget *colour;
  GtkWidget *colour_btn;
  GtkToggleButton *first = NULL;

  gtk_widget_set_margin_start (box, 4);
  gtk_widget_set_margin_end (box, 4);
  gtk_widget_set_margin_top (box, 4);
  gtk_widget_set_size_request (box, 140, -1);

  gtk_grid_set_row_homogeneous (GTK_GRID (grid), TRUE);
  gtk_grid_set_column_homogeneous (GTK_GRID (grid), TRUE);

  for (gsize i = 0; i < G_N_ELEMENTS (tool_entries); i++) {
    GtkWidget *btn = gtk_toggle_button_new ();
    GtkWidget *img = gtk_image_new_from_resource (tool_entries[i].icon);
    const char *name = gettext (tool_entries[i].name);

    gtk_image_set_pixel_size (GTK_IMAGE (img), 22);
    gtk_button_set_child (GTK_BUTTON (btn), img);
    gtk_widget_set_tooltip_text (btn, gettext (tool_entries[i].tooltip));
    /* No text label now, so set the accessible name (for the tests/readers)
     * and stash the tool name for the toggle handler. */
    set_a11y_label (btn, name);
    g_object_set_data (G_OBJECT (btn), "tool-name", (gpointer) name);
    if (first == NULL) {
      first = GTK_TOGGLE_BUTTON (btn);
      gtk_toggle_button_set_active (first, TRUE);
      g_strlcpy (self->tool, gettext (tool_entries[i].name), sizeof (self->tool));
    } else {
      gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (btn), first);
    }
    g_signal_connect (btn, "toggled", G_CALLBACK (on_tool_toggled), self);
    gtk_grid_attach (GTK_GRID (grid), btn, i % 2, i / 2, 1, 1);
  }
  gtk_box_append (GTK_BOX (box), grid);

  gtk_box_append (GTK_BOX (box),
                  gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));

  /* Colour area: a flat button (so AT-SPI exposes a click action and it is
   * keyboard-operable) wrapping a drawing area that paints the fg/bg swatches. */
  colour = gtk_drawing_area_new ();
  self->colour_area = colour;
  gtk_widget_set_size_request (colour, 56, 56);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (colour),
                                  draw_color_area, self, NULL);

  colour_btn = gtk_button_new ();
  gtk_button_set_child (GTK_BUTTON (colour_btn), colour);
  gtk_button_set_has_frame (GTK_BUTTON (colour_btn), FALSE);
  gtk_widget_set_halign (colour_btn, GTK_ALIGN_CENTER);
  set_a11y_label (colour_btn, "colour-area");
  gtk_widget_set_tooltip_text (colour_btn, _("Click to pick the foreground colour"));
  g_signal_connect (colour_btn, "clicked", G_CALLBACK (on_colour_clicked), self);
  gtk_box_append (GTK_BOX (box), colour_btn);

  return box;
}


static GtkWidget *
build_canvas_area (DiaShell *self)
{
  GtkWidget *notebook = gtk_notebook_new ();
  GtkWidget *grid = gtk_grid_new ();
  GtkWidget *origin = gtk_button_new ();
  GtkWidget *hruler = gtk_drawing_area_new ();
  GtkWidget *vruler = gtk_drawing_area_new ();
  /* A bare GtkDrawingArea is invisible to AT-SPI; give it an accessible role
   * so the UI tests (and screen readers) can find the canvas. */
  GtkWidget *canvas = g_object_new (GTK_TYPE_DRAWING_AREA,
                                    "accessible-role", GTK_ACCESSIBLE_ROLE_IMG,
                                    NULL);
  GtkWidget *vscroll = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, NULL);
  GtkWidget *hscroll = gtk_scrollbar_new (GTK_ORIENTATION_HORIZONTAL, NULL);
  GtkEventController *motion;
  GtkGesture *click;
  GtkGesture *drag;

  self->canvas = canvas;

  gtk_button_set_has_frame (GTK_BUTTON (origin), FALSE);

  self->hruler = hruler;
  self->vruler = vruler;

  gtk_widget_set_size_request (hruler, -1, 20);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (hruler),
                                  draw_ruler, self, NULL);

  gtk_widget_set_size_request (vruler, 20, -1);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (vruler),
                                  draw_ruler, self, NULL);

  gtk_widget_set_hexpand (canvas, TRUE);
  gtk_widget_set_vexpand (canvas, TRUE);
  gtk_widget_set_focusable (canvas, TRUE);   /* keyboard input + a11y exposure */
  set_a11y_label (canvas, "diagram-canvas");
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (canvas),
                                  draw_canvas, self, NULL);

  /* GTK4 input model: event controllers instead of button/motion signals. */
  motion = gtk_event_controller_motion_new ();
  g_signal_connect (motion, "motion", G_CALLBACK (on_canvas_motion), self);
  gtk_widget_add_controller (canvas, motion);

  click = gtk_gesture_click_new ();
  g_signal_connect (click, "pressed", G_CALLBACK (on_canvas_pressed), self);
  gtk_widget_add_controller (canvas, GTK_EVENT_CONTROLLER (click));

  drag = gtk_gesture_drag_new ();
  g_signal_connect (drag, "drag-begin", G_CALLBACK (on_canvas_drag_begin), self);
  g_signal_connect (drag, "drag-update", G_CALLBACK (on_canvas_drag_update), self);
  g_signal_connect (drag, "drag-end", G_CALLBACK (on_canvas_drag_end), self);
  gtk_widget_add_controller (canvas, GTK_EVENT_CONTROLLER (drag));

  gtk_grid_attach (GTK_GRID (grid), origin, 0, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), hruler, 1, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), vruler, 0, 1, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), canvas, 1, 1, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), vscroll, 2, 1, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), hscroll, 1, 2, 1, 1);

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), grid,
                            gtk_label_new (_("Diagram1")));
  gtk_notebook_set_scrollable (GTK_NOTEBOOK (notebook), TRUE);

  return notebook;
}


static GtkWidget *
build_layers (void)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  GtkWidget *label = gtk_label_new (_("Layers"));
  GtkWidget *list = gtk_list_box_new ();
  GtkWidget *row = gtk_label_new (_("Background"));
  GtkWidget *controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  const char *btns[] = { "list-add-symbolic", "list-remove-symbolic",
                         "go-up-symbolic", "go-down-symbolic" };

  gtk_widget_set_size_request (box, 150, -1);
  gtk_widget_set_margin_start (box, 4);
  gtk_widget_set_margin_end (box, 4);
  gtk_widget_set_margin_top (box, 4);

  gtk_widget_add_css_class (label, "heading");
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (box), label);

  gtk_widget_set_vexpand (list, TRUE);
  gtk_list_box_insert (GTK_LIST_BOX (list), row, -1);
  gtk_box_append (GTK_BOX (box), list);

  gtk_widget_add_css_class (controls, "toolbar");
  for (gsize i = 0; i < G_N_ELEMENTS (btns); i++) {
    GtkWidget *b = gtk_button_new_from_icon_name (btns[i]);
    gtk_button_set_has_frame (GTK_BUTTON (b), FALSE);
    gtk_box_append (GTK_BOX (controls), b);
  }
  gtk_box_append (GTK_BOX (box), controls);

  return box;
}


static GtkWidget *
build_statusbar (DiaShell *self)
{
  GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);

  self->status_msg = gtk_label_new ("");
  self->pos_label = gtk_label_new ("0, 0");
  self->zoom_label = gtk_label_new (_("100%"));

  gtk_widget_add_css_class (bar, "toolbar");
  gtk_widget_set_margin_start (bar, 6);
  gtk_widget_set_margin_end (bar, 6);

  gtk_widget_set_hexpand (self->status_msg, TRUE);
  gtk_widget_set_halign (self->status_msg, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (bar), self->status_msg);
  gtk_box_append (GTK_BOX (bar), self->pos_label);
  gtk_box_append (GTK_BOX (bar), gtk_separator_new (GTK_ORIENTATION_VERTICAL));
  gtk_box_append (GTK_BOX (bar), self->zoom_label);

  return bar;
}


static void
dia_shell_free (gpointer data)
{
  DiaShell *self = data;
  g_clear_pointer (&self->undo, g_ptr_array_unref);
  g_clear_object (&self->diagram);
  g_free (self);
}


/* Seed the diagram with a few real objects so it isn't blank on open and the
 * render pipeline is exercised before the user creates anything. */
static void
dia_shell_seed_sample (DiaShell *self)
{
  diagram_create_object (self, "Standard - Box",     (Point) { 2, 2 });
  diagram_create_object (self, "Standard - Ellipse", (Point) { 12, 8 });
  diagram_create_object (self, "Standard - Line",    (Point) { 7, 4 });
  diagram_create_object (self, "Standard - Text",    (Point) { 2, 14 });
}


GtkWidget *
dia_shell_new (void)
{
  DiaShell *self = g_new0 (DiaShell, 1);
  GtkWidget *view = adw_toolbar_view_new ();
  GtkWidget *header = adw_header_bar_new ();
  GtkWidget *main_area = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *title = adw_window_title_new ("Dia", _("Diagram1"));
  GtkWidget *statusbar;

  self->zoom = 1.0;
  self->fg = (GdkRGBA) { 0, 0, 0, 1 };
  self->bg = (GdkRGBA) { 1, 1, 1, 1 };

  /* Initialise libdia (property system, object registry, fonts) before any
   * object types are registered or objects created. */
  libdia_init (DIA_INTERACTIVE);
  register_standard_object_types ();
  self->diagram = g_object_new (DIA_TYPE_DIAGRAM_DATA, NULL);
  self->undo = g_ptr_array_new_with_free_func (g_free);
  dia_shell_seed_sample (self);

  /* Install the "dia" action group on the content so the toolbar buttons and
   * menu items resolve dia.new / dia.open / dia.save / dia.zoom-*. */
  dia_shell_install_actions (self, view);

  /* Build the statusbar first: its labels are updated by handlers that can
   * fire during construction (e.g. activating the first tool button). */
  statusbar = build_statusbar (self);

  adw_header_bar_set_title_widget (ADW_HEADER_BAR (header), title);
  adw_header_bar_pack_end (ADW_HEADER_BAR (header), build_primary_menu_button ());

  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (view), header);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (view), build_action_toolbar (self));

  gtk_box_append (GTK_BOX (main_area), build_toolbox (self));
  gtk_box_append (GTK_BOX (main_area),
                  gtk_separator_new (GTK_ORIENTATION_VERTICAL));
  gtk_box_append (GTK_BOX (main_area), build_canvas_area (self));
  gtk_box_append (GTK_BOX (main_area),
                  gtk_separator_new (GTK_ORIENTATION_VERTICAL));
  gtk_box_append (GTK_BOX (main_area), build_layers ());
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (view), main_area);

  adw_toolbar_view_add_bottom_bar (ADW_TOOLBAR_VIEW (view), statusbar);

  /* tie the shared state lifetime to the view */
  g_object_set_data_full (G_OBJECT (view), "dia-shell", self, dia_shell_free);

  return view;
}
