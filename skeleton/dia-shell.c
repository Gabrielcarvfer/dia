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
#include <glib/gstdio.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>

#include "dia-shell.h"

/* Ported core library: draw on the canvas with the real Dia renderer. */
#include "diarenderer.h"
#include "renderer/diacairo.h"
#include <cairo-pdf.h>
#include <cairo-svg.h>
#include "dia-colour.h"
#include "geometry.h"
#include "font.h"
#include "dia-enums.h"
#include "object.h"
#include "connectionpoint.h"
#include "diagramdata.h"
#include "dia-layer.h"
#include "dia-object-change.h"
#include "dialib.h"
#include "dia-io.h"
#include "dia_xml.h"
#include "diacontext.h"
#include "message.h"
#include "register-objects.h"
#include "properties.h"
#include "prop_geomtypes.h"
#include "prop_attr.h"
#include "prop_text.h"
#include "arrows.h"
#include "attributes.h"
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
  DiaObject  *clipboard;  /* a cloned object held for paste (cut/copy) */
  Point       drag_origin; /* selected object's position at drag start */
  /* Handle drag (resize/stretch/bend): when the press lands on a handle of the
   * selection, the drag moves that handle instead of the whole object. */
  Handle     *drag_handle;       /* handle being dragged, or NULL for a move */
  int         drag_handle_idx;   /* its index in selected->handles (for undo) */
  Point       drag_handle_start; /* the handle's position at drag start */
  /* Multi-select: a rubber-band on empty canvas, and a snapshot of the moving
   * objects' start positions so a drag moves the whole selection together. */
  gboolean    rubber_band;
  Point       rubber_start, rubber_cur;
  GArray     *drag_moves;        /* of MoveItem {DiaObject*, Point start} */

  GPtrArray  *undo;       /* UndoOp* — the undo/redo history */
  int         undo_pos;   /* number of ops currently applied (undo[0..pos-1]) */
  GActionGroup *actions;  /* the "dia" group, to toggle undo/redo enabled */
  /* Viewport over an (optionally infinite) cm workspace. origin_{x,y} is the cm
   * coordinate at the canvas top-left; zoom gives px-per-cm = PX_PER_CM * zoom.
   * page_{x,y,scale} is the derived device transform (px of cm 0, and px/cm) so
   * the click/motion handlers and rulers can map between px and cm. */
  double     origin_x, origin_y;    /* cm at the viewport's top-left pixel */
  double     page_x, page_y, page_scale;
  /* The drawing page, in cm. page_infinite => unbounded scrollable workspace. */
  double     page_w_cm, page_h_cm;
  gboolean   page_infinite;
  GtkWidget *hscroll, *vscroll;     /* scrollbars over the workspace */
  guint      scroll_guard;          /* >0 while we update adjustments ourselves */
  /* Snapping toggles (toolbar). snap_grid is applied to create/move/handle. */
  gboolean   snap_grid, snap_object, snap_guide;
  GtkWidget *layers_panel;          /* right sidebar, hideable to free space */
  GtkWidget *layers_list;           /* GtkListBox of the diagram's layers */
  GtkWidget *sheet_box;             /* FlowBox of the current sheet's shapes */
  int        sheet_index;           /* selected sheet (see sheets[]) */
  /* Line attributes applied to new objects (and the current selection). */
  double       line_width;          /* cm */
  DiaLineStyle line_style;
  ArrowType    start_arrow, end_arrow;
} DiaShell;

/* Pixels per cm at 100% zoom. Chosen so a typical canvas shows ~80 cm across,
 * matching upstream Dia's zoomed-out default (the page is small within it). */
#define PX_PER_CM 10.0

/* A moving object + its position at drag start (for multi-object move). */
typedef struct { DiaObject *obj; Point start; } MoveItem;


/* Object types are registered from objects-port (register-objects.c) so the
 * skeleton and the object tests use the identical set. */
static void
register_standard_object_types (void)
{
  dia_port_register_objects ();
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
typedef enum { OP_CREATE, OP_DELETE, OP_MOVE, OP_HANDLE } OpKind;

typedef struct {
  OpKind     kind;
  DiaObject *obj;
  Point      from;   /* OP_MOVE/OP_HANDLE: handle/object position before */
  Point      to;     /* OP_MOVE/OP_HANDLE: handle/object position after */
  int        handle; /* OP_HANDLE: index of the moved handle in obj->handles */
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

/* Record a handle move (resize/stretch/bend) as an undoable op. Reuses push_op
 * for the history bookkeeping, then tags the entry as OP_HANDLE. */
static void
push_op_handle (DiaShell *self, DiaObject *obj, int handle_idx,
                Point from, Point to)
{
  UndoOp *op;

  push_op (self, OP_MOVE, obj, from, to);
  op = g_ptr_array_index (self->undo, self->undo_pos - 1);
  op->kind = OP_HANDLE;
  op->handle = handle_idx;
}

/* Move op->obj's recorded handle to @p (used by both apply and revert). */
static void
op_move_handle (UndoOp *op, Point *p)
{
  DiaObjectChange *change;

  if (op->handle < 0 || op->handle >= op->obj->num_handles) {
    return;
  }
  change = dia_object_move_handle (op->obj, op->obj->handles[op->handle], p,
                                   NULL, HANDLE_MOVE_USER_FINAL, 0);
  g_clear_pointer (&change, dia_object_change_unref);
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
    case OP_DELETE:
      if (self->selected == op->obj) {
        self->selected = NULL;
      }
      data_unselect (self->diagram, op->obj);
      dia_layer_remove_object (layer, op->obj);
      break;
    case OP_MOVE:
      change = dia_object_move (op->obj, &op->to);
      g_clear_pointer (&change, dia_object_change_unref);
      break;
    case OP_HANDLE:
      op_move_handle (op, &op->to);
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
    case OP_DELETE:
      dia_layer_add_object (layer, op->obj);
      break;
    case OP_MOVE:
      change = dia_object_move (op->obj, &op->from);
      g_clear_pointer (&change, dia_object_change_unref);
      break;
    case OP_HANDLE:
      op_move_handle (op, &op->from);
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


/* Draw small squares at each selected object's handles, at a fixed pixel size
 * in DEVICE space: (ox,oy) is the device px of cm 0, scale is px/cm. */
static void
draw_selection_handles (cairo_t *cr, DiagramData *diagram,
                        double ox, double oy, double scale)
{
  const double s = 3.5;   /* half handle size, px */

  for (GList *l = diagram->selected; l; l = l->next) {
    DiaObject *obj = l->data;

    for (int i = 0; i < obj->num_handles; i++) {
      Handle *h = obj->handles[i];
      double dx = ox + h->pos.x * scale;
      double dy = oy + h->pos.y * scale;
      cairo_rectangle (cr, dx - s, dy - s, 2 * s, 2 * s);
    }
    cairo_set_source_rgb (cr, 0.10, 0.45, 0.90);
    cairo_fill_preserve (cr);
    cairo_set_source_rgb (cr, 1, 1, 1);
    cairo_set_line_width (cr, 1.0);
    cairo_stroke (cr);
  }
}


/* Draw every object's connection points as small crosses (fixed pixel size,
 * device space) so the user can see where a line endpoint will connect. */
static void
draw_connection_points (cairo_t *cr, DiagramData *diagram,
                        double ox, double oy, double scale)
{
  DiaLayer *layer = dia_diagram_data_get_active_layer (diagram);
  int n = layer ? dia_layer_object_count (layer) : 0;
  const double s = 4.0;   /* half cross size, px */

  cairo_set_source_rgba (cr, 0.0, 0.45, 0.30, 0.6);
  cairo_set_line_width (cr, 1.0);
  for (int i = 0; i < n; i++) {
    DiaObject *obj = dia_layer_object_get_nth (layer, i);

    for (int c = 0; c < dia_object_get_num_connections (obj); c++) {
      Point p = obj->connections[c]->pos;
      double dx = ox + p.x * scale;
      double dy = oy + p.y * scale;
      cairo_move_to (cr, dx - s, dy);
      cairo_line_to (cr, dx + s, dy);
      cairo_move_to (cr, dx, dy - s);
      cairo_line_to (cr, dx, dy + s);
    }
  }
  cairo_stroke (cr);
}


/* After @obj moves/resizes, make any line endpoints connected to its connection
 * points follow (move_handle with HANDLE_MOVE_CONNECTED). */
static void
update_connections_for (DiaObject *obj)
{
  for (int i = 0; i < dia_object_get_num_connections (obj); i++) {
    ConnectionPoint *cp = obj->connections[i];

    for (GList *l = cp->connected; l; l = l->next) {
      DiaObject *conn = l->data;

      for (int j = 0; j < conn->num_handles; j++) {
        if (conn->handles[j]->connected_to == cp) {
          DiaObjectChange *ch = conn->ops->move_handle (conn, conn->handles[j],
                                                        &cp->pos, cp,
                                                        HANDLE_MOVE_CONNECTED, 0);
          g_clear_pointer (&ch, dia_object_change_unref);
        }
      }
    }
  }
}


/* Pixels-per-cm at the current zoom. */
static double
shell_pxcm (DiaShell *self)
{
  return PX_PER_CM * (self ? self->zoom : 1.0);
}

/* Recompute the device transform (px of cm 0, and px/cm) from origin + zoom.
 * Cheap and widget-size-independent, so callers update it whenever the origin
 * or zoom changes and every consumer (canvas, rulers, click/motion) reads it. */
static void
update_transform (DiaShell *self)
{
  double pxcm = shell_pxcm (self);

  self->page_scale = pxcm;
  self->page_x = -self->origin_x * pxcm;
  self->page_y = -self->origin_y * pxcm;
}

static void
redraw_canvas_and_rulers (DiaShell *self)
{
  if (self->canvas) gtk_widget_queue_draw (self->canvas);
  if (self->hruler) gtk_widget_queue_draw (self->hruler);
  if (self->vruler) gtk_widget_queue_draw (self->vruler);
}

/* Reconfigure the scrollbar adjustments for the current viewport: value =
 * origin (cm), page_size = visible cm, range = page bounds + margin (or a large
 * span when the workspace is infinite), always covering the current view. */
static void
update_scrollbars (DiaShell *self)
{
  GtkAdjustment *ha, *va;
  double pxcm = shell_pxcm (self);
  double vis_w, vis_h, lo_x, hi_x, lo_y, hi_y;
  int cw, ch;

  if (!self->canvas || !self->hscroll || !self->vscroll) {
    return;
  }
  cw = gtk_widget_get_width (self->canvas);
  ch = gtk_widget_get_height (self->canvas);
  if (cw <= 1) cw = 600;
  if (ch <= 1) ch = 400;
  vis_w = cw / pxcm;
  vis_h = ch / pxcm;

  if (self->page_infinite) {
    lo_x = -200.0; hi_x = 200.0;
    lo_y = -200.0; hi_y = 200.0;
  } else {
    lo_x = -5.0; hi_x = self->page_w_cm + 5.0;
    lo_y = -5.0; hi_y = self->page_h_cm + 5.0;
  }
  /* Never crop the current view. */
  lo_x = MIN (lo_x, self->origin_x);
  hi_x = MAX (hi_x, self->origin_x + vis_w);
  lo_y = MIN (lo_y, self->origin_y);
  hi_y = MAX (hi_y, self->origin_y + vis_h);

  ha = gtk_scrollbar_get_adjustment (GTK_SCROLLBAR (self->hscroll));
  va = gtk_scrollbar_get_adjustment (GTK_SCROLLBAR (self->vscroll));

  self->scroll_guard++;
  gtk_adjustment_configure (ha, self->origin_x, lo_x, hi_x,
                            vis_w / 10.0, vis_w * 0.9, vis_w);
  gtk_adjustment_configure (va, self->origin_y, lo_y, hi_y,
                            vis_h / 10.0, vis_h * 0.9, vis_h);
  self->scroll_guard--;
}

static void
on_hadj_changed (GtkAdjustment *adj, DiaShell *self)
{
  if (self->scroll_guard) return;
  self->origin_x = gtk_adjustment_get_value (adj);
  update_transform (self);
  redraw_canvas_and_rulers (self);
}

static void
on_vadj_changed (GtkAdjustment *adj, DiaShell *self)
{
  if (self->scroll_guard) return;
  self->origin_y = gtk_adjustment_get_value (adj);
  update_transform (self);
  redraw_canvas_and_rulers (self);
}

/* Keep the page in view: after a zoom/scroll, never let the white sheet leave
 * the viewport entirely. At least ~1 cm of the page stays visible (so an edge
 * shows), unless the viewport is wholly inside the page — then it's all white
 * with no edge to show, which is fine. No-op for the infinite workspace. */
static void
clamp_origin_to_page (DiaShell *self)
{
  double pxcm = shell_pxcm (self);
  double vis_w, vis_h, m = 1.0;
  int cw, ch;

  if (self->page_infinite || !self->canvas) {
    return;
  }
  cw = gtk_widget_get_width (self->canvas);
  ch = gtk_widget_get_height (self->canvas);
  if (cw <= 1) cw = 600;
  if (ch <= 1) ch = 400;
  vis_w = cw / pxcm;
  vis_h = ch / pxcm;

  if (vis_w >= self->page_w_cm) {
    /* Page narrower than the view: keep the whole page on screen. */
    self->origin_x = CLAMP (self->origin_x, self->page_w_cm - vis_w, 0.0);
  } else {
    /* Page wider: allow the view inside the page, but keep >= m cm overlapping. */
    self->origin_x = CLAMP (self->origin_x, m - vis_w, self->page_w_cm - m);
  }
  if (vis_h >= self->page_h_cm) {
    self->origin_y = CLAMP (self->origin_y, self->page_h_cm - vis_h, 0.0);
  } else {
    self->origin_y = CLAMP (self->origin_y, m - vis_h, self->page_h_cm - m);
  }
}

static void update_zoom (DiaShell *self, double factor);

/* Mouse wheel: Ctrl = zoom, Shift = horizontal scroll, otherwise vertical. */
static gboolean
on_canvas_scroll (GtkEventControllerScroll *ctrl,
                  double dx, double dy, DiaShell *self)
{
  GdkModifierType mods = gtk_event_controller_get_current_event_state (
                           GTK_EVENT_CONTROLLER (ctrl));
  double step = 2.0;   /* cm per wheel notch */

  if (mods & GDK_CONTROL_MASK) {
    /* Zoom: wheel up (dy<0) zooms in, wheel down zooms out. */
    if (dy != 0.0) {
      update_zoom (self, dy < 0 ? 1.1 : 1.0 / 1.1);
    }
    return TRUE;
  }
  if (mods & GDK_SHIFT_MASK) {
    self->origin_x += (dy + dx) * step;
  } else {
    self->origin_y += dy * step;
    self->origin_x += dx * step;
  }
  clamp_origin_to_page (self);
  update_transform (self);
  update_scrollbars (self);
  redraw_canvas_and_rulers (self);
  return TRUE;
}

/* Canvas resized: the visible cm span changed, so refresh the scrollbars. */
static void
on_canvas_resize (GtkDrawingArea *area, int width, int height, DiaShell *self)
{
  update_scrollbars (self);
  if (self->hruler) gtk_widget_queue_draw (self->hruler);
  if (self->vruler) gtk_widget_queue_draw (self->vruler);
}


static void
draw_canvas (GtkDrawingArea *area,
             cairo_t        *cr,
             int             width,
             int             height,
             gpointer        user_data)
{
  DiaShell *self = user_data;
  double pxcm, ox, oy;
  double cm_x0, cm_y0, cm_x1, cm_y1;

  if (!self) {
    cairo_set_source_rgb (cr, 0.6, 0.6, 0.62);
    cairo_paint (cr);
    return;
  }

  /* Infinite workspace is all white (one boundless sheet); a fixed page sits in
   * a grey workspace so its edges are visible. */
  if (self->page_infinite) {
    cairo_set_source_rgb (cr, 1, 1, 1);
  } else {
    cairo_set_source_rgb (cr, 0.6, 0.6, 0.62);
  }
  cairo_paint (cr);

  update_transform (self);
  pxcm = self->page_scale;
  ox = self->page_x;   /* device x of cm 0 */
  oy = self->page_y;   /* device y of cm 0 */

  /* The page sheet (unless the workspace is infinite): drop shadow + white. */
  if (!self->page_infinite) {
    double pw = self->page_w_cm * pxcm;
    double ph = self->page_h_cm * pxcm;

    cairo_rectangle (cr, ox + 3, oy + 3, pw, ph);
    cairo_set_source_rgba (cr, 0, 0, 0, 0.25);
    cairo_fill (cr);

    cairo_rectangle (cr, ox, oy, pw, ph);
    cairo_set_source_rgb (cr, 1, 1, 1);
    cairo_fill_preserve (cr);
    cairo_set_source_rgb (cr, 0.4, 0.4, 0.4);
    cairo_set_line_width (cr, 1.0);
    cairo_stroke (cr);
  }

  /* Engineering-paper grid across the whole visible viewport, in cm. */
  cm_x0 = self->origin_x;
  cm_y0 = self->origin_y;
  cm_x1 = self->origin_x + width / pxcm;
  cm_y1 = self->origin_y + height / pxcm;
  {
    int gx0 = (int) floor (cm_x0), gx1 = (int) ceil (cm_x1);
    int gy0 = (int) floor (cm_y0), gy1 = (int) ceil (cm_y1);

    for (int gx = gx0; gx <= gx1; gx++) {
      gboolean major = (gx % 5 == 0);
      double dx = ox + gx * pxcm;
      cairo_set_line_width (cr, major ? 1.0 : 0.5);
      cairo_set_source_rgb (cr, major ? 0.60 : 0.82,
                                major ? 0.70 : 0.88,
                                major ? 0.85 : 0.94);
      cairo_move_to (cr, dx, 0);
      cairo_line_to (cr, dx, height);
      cairo_stroke (cr);
    }
    for (int gy = gy0; gy <= gy1; gy++) {
      gboolean major = (gy % 5 == 0);
      double dy = oy + gy * pxcm;
      cairo_set_line_width (cr, major ? 1.0 : 0.5);
      cairo_set_source_rgb (cr, major ? 0.60 : 0.82,
                                major ? 0.70 : 0.88,
                                major ? 0.85 : 0.94);
      cairo_move_to (cr, 0, dy);
      cairo_line_to (cr, width, dy);
      cairo_stroke (cr);
    }
  }

  /* Diagram shapes in cm: scale to px/cm, then shift the origin into place. */
  cairo_save (cr);
  cairo_scale (cr, pxcm, pxcm);
  cairo_translate (cr, -self->origin_x, -self->origin_y);
  if (self->diagram) {
    draw_diagram (cr, self->diagram);
  }
  cairo_restore (cr);

  /* Connection points and selection handles are drawn in DEVICE space at a
   * fixed pixel size (ox/oy = device px of cm 0), so they stay grabbable
   * whatever the zoom — drawing them in cm space made them ~1 px when zoomed
   * out. */
  if (self->diagram) {
    draw_connection_points (cr, self->diagram, ox, oy, pxcm);
    draw_selection_handles (cr, self->diagram, ox, oy, pxcm);
  }

  /* The rubber-band selection rectangle (device space, dashed). */
  if (self->rubber_band) {
    double rx0 = ox + MIN (self->rubber_start.x, self->rubber_cur.x) * pxcm;
    double ry0 = oy + MIN (self->rubber_start.y, self->rubber_cur.y) * pxcm;
    double rx1 = ox + MAX (self->rubber_start.x, self->rubber_cur.x) * pxcm;
    double ry1 = oy + MAX (self->rubber_start.y, self->rubber_cur.y) * pxcm;
    double dashes[] = { 4.0, 3.0 };

    cairo_rectangle (cr, rx0, ry0, rx1 - rx0, ry1 - ry0);
    cairo_set_source_rgba (cr, 0.10, 0.45, 0.90, 0.12);
    cairo_fill_preserve (cr);
    cairo_set_source_rgb (cr, 0.10, 0.45, 0.90);
    cairo_set_line_width (cr, 1.0);
    cairo_set_dash (cr, dashes, 2, 0);
    cairo_stroke (cr);
    cairo_set_dash (cr, NULL, 0, 0);
  }
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
  int thick = horizontal ? height : width;
  /* The canvas and rulers share a grid row/column (same px axis), so the
   * derived transform (kept up to date by update_transform) maps px<->cm. */
  double scale = self ? self->page_scale : 0.0;
  double origin = horizontal ? (self ? self->page_x : 0.0)
                             : (self ? self->page_y : 0.0);

  cairo_set_source_rgb (cr, 0.93, 0.93, 0.93);
  cairo_paint (cr);
  cairo_set_font_size (cr, 11.0);
  cairo_set_line_width (cr, 1.0);

  if (scale > 0.0) {
    /* One tick per cm; a longer tick + a numeric label every 5 cm. */
    int cm0 = (int) floor ((0 - origin) / scale);
    int cm1 = (int) ceil ((extent - origin) / scale);

    for (int cm = cm0; cm <= cm1; cm++) {
      double pos = origin + cm * scale;
      gboolean major = (cm % 5 == 0);
      double len = thick * (major ? 0.6 : 0.3);

      cairo_set_source_rgb (cr, 0.45, 0.45, 0.45);
      if (horizontal) {
        cairo_move_to (cr, pos + 0.5, thick - len);
        cairo_line_to (cr, pos + 0.5, thick);
      } else {
        cairo_move_to (cr, thick - len, pos + 0.5);
        cairo_line_to (cr, thick, pos + 0.5);
      }
      cairo_stroke (cr);

      if (major) {
        char lbl[16];
        g_snprintf (lbl, sizeof (lbl), "%d", cm);
        cairo_set_source_rgb (cr, 0.2, 0.2, 0.2);
        if (horizontal) {
          cairo_move_to (cr, pos + 2.0, thick - len - 1.0);
        } else {
          cairo_move_to (cr, 1.0, pos - 1.0);
        }
        cairo_show_text (cr, lbl);
      }
    }
  } else {
    /* Page transform not known yet (first frame): plain pixel ticks. */
    cairo_set_source_rgb (cr, 0.5, 0.5, 0.5);
    for (int i = 0; i < extent; i += 10) {
      double t = (i % 50 == 0) ? 0.0 : (thick / 2.0);
      if (horizontal) {
        cairo_move_to (cr, i + 0.5, t);
        cairo_line_to (cr, i + 0.5, thick);
      } else {
        cairo_move_to (cr, t, i + 0.5);
        cairo_line_to (cr, thick, i + 0.5);
      }
    }
    cairo_stroke (cr);
  }

  /* Cursor-tracking marker: a black triangle at the pointer's position along
   * the ruler (the canvas and rulers share a grid column/row, so the
   * canvas-relative pointer coordinate maps straight onto the ruler). Sized to
   * most of the ruler thickness so it is easy to see. */
  if (self->cursor_valid) {
    double hw = 10.0;           /* half-width of the triangle base */
    double depth = thick - 2.0; /* how far it reaches across the ruler */

    cairo_set_source_rgb (cr, 0, 0, 0);
    if (horizontal) {
      double x = self->cursor_x;
      cairo_move_to (cr, x, thick);                 /* apex at inner edge */
      cairo_line_to (cr, x - hw, thick - depth);
      cairo_line_to (cr, x + hw, thick - depth);
    } else {
      double y = self->cursor_y;
      cairo_move_to (cr, thick, y);
      cairo_line_to (cr, thick - depth, y - hw);
      cairo_line_to (cr, thick - depth, y + hw);
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


/* Dropdown index -> ArrowType for the start/end arrow choosers. */
static const ArrowType arrow_choices[] = {
  ARROW_NONE, ARROW_LINES, ARROW_FILLED_TRIANGLE
};

static Arrow
shell_arrow (DiaShell *self, gboolean start)
{
  Arrow a;

  a.type = start ? self->start_arrow : self->end_arrow;
  a.length = 0.5;
  a.width = 0.5;
  return a;
}

/* Apply the shell's current line attributes (width / style / arrows) to @obj
 * via the StdProp system. Objects lacking a given property ignore it. */
static void
apply_line_props (DiaShell *self, DiaObject *obj)
{
  static PropDescription descs[] = {
    PROP_STD_LINE_WIDTH,
    PROP_STD_LINE_STYLE,
    PROP_STD_START_ARROW,
    PROP_STD_END_ARROW,
    PROP_DESC_END
  };
  GPtrArray *props;

  if (!obj) {
    return;
  }
  props = prop_list_from_descs (descs, pdtpp_true);
  ((RealProperty *) g_ptr_array_index (props, 0))->real_data = self->line_width;
  ((LinestyleProperty *) g_ptr_array_index (props, 1))->style = self->line_style;
  ((LinestyleProperty *) g_ptr_array_index (props, 1))->dash = 0.5;
  ((ArrowProperty *) g_ptr_array_index (props, 2))->arrow_data = shell_arrow (self, TRUE);
  ((ArrowProperty *) g_ptr_array_index (props, 3))->arrow_data = shell_arrow (self, FALSE);
  dia_object_set_properties (obj, props);
  prop_list_free (props);
}

/* Push the current attributes as the libdia defaults (so freshly created
 * objects inherit them) and, if something is selected, restyle it now. */
static void
on_line_attrs_changed (DiaShell *self)
{
  attributes_set_default_linewidth (self->line_width);
  attributes_set_default_line_style (self->line_style, 0.5);
  attributes_set_default_start_arrow (shell_arrow (self, TRUE));
  attributes_set_default_end_arrow (shell_arrow (self, FALSE));
  if (self->selected) {
    apply_line_props (self, self->selected);
    gtk_widget_queue_draw (self->canvas);
  }
}


/* Return a copy of @obj's text content, or NULL if it has no text property
 * (object types without text leave the property unset on get). */
static char *
get_object_text (DiaObject *obj)
{
  static PropDescription descs[] = { PROP_STD_TEXT, PROP_DESC_END };
  GPtrArray *props = prop_list_from_descs (descs, pdtpp_true);
  TextProperty *tp = g_ptr_array_index (props, 0);
  char *result = NULL;

  dia_object_get_properties (obj, props);
  if (tp->text_data) {
    result = g_strdup (tp->text_data);
  }
  prop_list_free (props);
  return result;
}

/* Replace @obj's text content, keeping its font/size/colour/alignment. */
static void
set_object_text (DiaObject *obj, const char *str)
{
  static PropDescription descs[] = { PROP_STD_TEXT, PROP_DESC_END };
  GPtrArray *props = prop_list_from_descs (descs, pdtpp_true);
  TextProperty *tp = g_ptr_array_index (props, 0);

  dia_object_get_properties (obj, props);     /* keep the existing attributes */
  g_free (tp->text_data);
  tp->text_data = g_strdup (str);
  dia_object_set_properties (obj, props);
  prop_list_free (props);
}


/* If snap-to-grid is on, round @p to the nearest half-centimetre. */
static void
snap_to_grid (DiaShell *self, Point *p)
{
  const double step = 0.5;

  if (!self->snap_grid) {
    return;
  }
  p->x = round (p->x / step) * step;
  p->y = round (p->y / step) * step;
}


/* Apply the current tool at diagram point @p: create a real DiaObject (create
 * tools) or just report the position. Shared by the canvas gesture and the
 * UI-test trigger so both exercise the same code path. */
static void
apply_tool_at (DiaShell *self, Point p)
{
  char buf[128];
  const char *type_name;
  DiaObject *obj;

  snap_to_grid (self, &p);
  type_name = tool_to_type_name (self->tool);
  /* Sheet shapes set self->tool directly to a registered type name. */
  if (!type_name && object_get_type (self->tool) != NULL) {
    type_name = self->tool;
  }
  obj = type_name ? diagram_create_object (self, type_name, p) : NULL;

  if (obj) {
    apply_line_props (self, obj);   /* honour the toolbox line attributes */
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


/* Modify tool: hit-test the object under @p and make it the selection. With
 * @add (Shift) the object is added to the current selection instead of
 * replacing it. self->selected tracks the "primary" (last-clicked) object. */
static void
select_at (DiaShell *self, Point p, gboolean add)
{
  DiaLayer *layer = dia_diagram_data_get_active_layer (self->diagram);
  DiaObject *obj = layer ? dia_layer_find_closest_object (layer, &p, 0.5) : NULL;
  char buf[96];

  if (!add) {
    data_remove_all_selected (self->diagram);
    self->selected = NULL;
  }
  if (obj) {
    data_select (self->diagram, obj);
    self->selected = obj;
  }

  if (self->selected) {
    g_snprintf (buf, sizeof (buf), _("Selected %s (%u)"),
                self->selected->type->name,
                g_list_length (self->diagram->selected));
  } else {
    g_snprintf (buf, sizeof (buf), _("Nothing selected"));
  }
  gtk_widget_queue_draw (self->canvas);
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
}


/* Remove the current selection from its layer (undoable). The object is only
 * unlinked, not destroyed, so undo can put it back. */
static void
delete_selected (DiaShell *self)
{
  DiaLayer *layer = dia_diagram_data_get_active_layer (self->diagram);
  GList *sel = g_list_copy (self->diagram->selected);   /* snapshot: we mutate it */
  guint count = 0;
  char buf[64];

  if (!sel) {
    gtk_label_set_text (GTK_LABEL (self->status_msg), _("Nothing to delete"));
    return;
  }
  for (GList *l = sel; l; l = l->next) {
    DiaObject *obj = l->data;

    /* Record before unlinking (the op is considered already applied). */
    push_op (self, OP_DELETE, obj, obj->position, obj->position);
    data_unselect (self->diagram, obj);
    dia_layer_remove_object (layer, obj);
    count++;
  }
  g_list_free (sel);
  self->selected = NULL;

  g_snprintf (buf, sizeof (buf), _("Deleted %u — %u object(s) left"),
              count, diagram_object_count (self));
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
  gtk_widget_queue_draw (self->canvas);
}


/* Return the index of @obj's handle within @tol cm of @p, or -1 if none. The
 * handles are the small squares draw_selection_handles() paints; grabbing one
 * lets the user resize/stretch/bend the object. */
static int
handle_at (DiaObject *obj, Point p, double tol)
{
  int best = -1;
  double best2 = tol * tol;

  if (obj == NULL) {
    return -1;
  }
  for (int i = 0; i < obj->num_handles; i++) {
    Handle *h = obj->handles[i];
    double dx = h->pos.x - p.x;
    double dy = h->pos.y - p.y;
    double d2 = dx * dx + dy * dy;

    if (d2 <= best2) {
      best2 = d2;
      best = i;
    }
  }
  return best;
}


static void
on_text_dialog_response (AdwAlertDialog *dlg, const char *response, DiaShell *self)
{
  GtkWidget *entry = g_object_get_data (G_OBJECT (dlg), "dia-entry");
  DiaObject *obj = g_object_get_data (G_OBJECT (dlg), "dia-object");

  if (g_strcmp0 (response, "ok") == 0 && entry && obj) {
    set_object_text (obj, gtk_editable_get_text (GTK_EDITABLE (entry)));
    gtk_widget_queue_draw (self->canvas);
    gtk_label_set_text (GTK_LABEL (self->status_msg), _("Text updated"));
  }
}

/* Open an inline text editor for @obj (a dialog with its current text), if the
 * object has a text property. */
static void
open_text_editor (DiaShell *self, DiaObject *obj)
{
  char *cur = get_object_text (obj);
  GtkWidget *entry;
  AdwDialog *dlg;
  GtkRoot *root;

  if (!cur) {
    return;   /* object has no editable text */
  }
  entry = gtk_entry_new ();
  gtk_editable_set_text (GTK_EDITABLE (entry), cur);
  g_free (cur);

  dlg = adw_alert_dialog_new (_("Edit Text"), NULL);
  adw_alert_dialog_set_extra_child (ADW_ALERT_DIALOG (dlg), entry);
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dlg), "cancel", _("Cancel"));
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dlg), "ok", _("OK"));
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dlg), "ok");
  g_object_set_data (G_OBJECT (dlg), "dia-entry", entry);
  g_object_set_data (G_OBJECT (dlg), "dia-object", obj);
  g_signal_connect (dlg, "response", G_CALLBACK (on_text_dialog_response), self);

  root = gtk_widget_get_root (self->canvas);
  adw_dialog_present (dlg, GTK_WIDGET (root));
}

static void
on_canvas_pressed (GtkGestureClick *gesture,
                   int              n_press,
                   double           x,
                   double           y,
                   DiaShell        *self)
{
  Point p;

  /* Focus the canvas so it receives key events (Delete, Escape, …). */
  gtk_widget_grab_focus (self->canvas);

  /* widget pixels -> diagram cm (inverse of the page transform) */
  if (self->page_scale <= 0.0) {
    return;
  }
  p.x = (x - self->page_x) / self->page_scale;
  p.y = (y - self->page_y) / self->page_scale;

  if (g_strcmp0 (self->tool, "Modify") == 0) {
    if (n_press >= 2) {
      /* Double-click edits the selected object's text (the first click of the
       * sequence already selected it). */
      if (self->selected) {
        open_text_editor (self, self->selected);
      }
    } else {
      GdkModifierType m = gtk_event_controller_get_current_event_state (
                            GTK_EVENT_CONTROLLER (gesture));
      select_at (self, p, (m & GDK_SHIFT_MASK) != 0);
    }
  } else {
    apply_tool_at (self, p);
  }
}


/* Drag with the Modify tool: move the grabbed handle (resize/stretch/bend) if
 * the press landed on a selection handle, otherwise move the whole object.
 *
 * The handle hit-test is done HERE, not in the click 'pressed' handler: the
 * click and drag gestures both fire on press, and their relative order is not
 * guaranteed (the drag controller, added last, can fire first). The selection
 * already persists from the earlier click that revealed the handles, so testing
 * against self->selected here is reliable regardless of gesture order. */
static void
on_canvas_drag_begin (GtkGestureDrag *gesture,
                      double          start_x,
                      double          start_y,
                      DiaShell       *self)
{
  Point p;
  int hi;
  DiaLayer *layer;
  DiaObject *obj_under;

  self->drag_handle = NULL;
  self->drag_handle_idx = -1;
  self->rubber_band = FALSE;
  if (self->drag_moves) {
    g_array_set_size (self->drag_moves, 0);
  } else {
    self->drag_moves = g_array_new (FALSE, FALSE, sizeof (MoveItem));
  }

  if (g_strcmp0 (self->tool, "Modify") != 0 || self->page_scale <= 0.0) {
    return;
  }

  p.x = (start_x - self->page_x) / self->page_scale;
  p.y = (start_y - self->page_y) / self->page_scale;

  /* 1. A handle of the primary selection -> resize/reshape that object. */
  hi = self->selected ? handle_at (self->selected, p, 0.4) : -1;
  if (hi >= 0) {
    self->drag_handle = self->selected->handles[hi];
    self->drag_handle_idx = hi;
    self->drag_handle_start = self->drag_handle->pos;
    if (self->drag_handle->connected_to != NULL) {
      object_unconnect (self->selected, self->drag_handle);
    }
    return;
  }

  /* 2. On an object -> move the whole selection. On empty -> rubber-band. */
  layer = dia_diagram_data_get_active_layer (self->diagram);
  obj_under = layer ? dia_layer_find_closest_object (layer, &p, 0.5) : NULL;
  if (obj_under) {
    if (self->diagram->selected
        && g_list_find (self->diagram->selected, obj_under)) {
      for (GList *l = self->diagram->selected; l; l = l->next) {
        MoveItem it = { l->data, ((DiaObject *) l->data)->position };
        g_array_append_val (self->drag_moves, it);
      }
    } else {
      MoveItem it = { obj_under, obj_under->position };
      g_array_append_val (self->drag_moves, it);
    }
  } else {
    self->rubber_band = TRUE;
    self->rubber_start = p;
    self->rubber_cur = p;
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

  if (g_strcmp0 (self->tool, "Modify") != 0 || self->page_scale <= 0.0) {
    return;
  }

  if (self->drag_handle && self->selected) {
    /* Absolute target relative to the handle's start, so repeated updates are
     * not cumulative. */
    to.x = self->drag_handle_start.x + offset_x / self->page_scale;
    to.y = self->drag_handle_start.y + offset_y / self->page_scale;
    snap_to_grid (self, &to);
    change = dia_object_move_handle (self->selected, self->drag_handle, &to,
                                     NULL, HANDLE_MOVE_USER, 0);
    g_clear_pointer (&change, dia_object_change_unref);
    update_connections_for (self->selected);
  } else if (self->rubber_band) {
    self->rubber_cur.x = self->rubber_start.x + offset_x / self->page_scale;
    self->rubber_cur.y = self->rubber_start.y + offset_y / self->page_scale;
  } else {
    for (guint i = 0; i < self->drag_moves->len; i++) {
      MoveItem *it = &g_array_index (self->drag_moves, MoveItem, i);

      to.x = it->start.x + offset_x / self->page_scale;
      to.y = it->start.y + offset_y / self->page_scale;
      snap_to_grid (self, &to);
      change = dia_object_move (it->obj, &to);
      g_clear_pointer (&change, dia_object_change_unref);
      update_connections_for (it->obj);
    }
  }
  gtk_widget_queue_draw (self->canvas);
}

/* Select every object whose bounding box intersects the rubber-band rect. */
static void
rubber_band_select (DiaShell *self)
{
  DiaLayer *layer = dia_diagram_data_get_active_layer (self->diagram);
  int n = layer ? dia_layer_object_count (layer) : 0;
  double x0 = MIN (self->rubber_start.x, self->rubber_cur.x);
  double y0 = MIN (self->rubber_start.y, self->rubber_cur.y);
  double x1 = MAX (self->rubber_start.x, self->rubber_cur.x);
  double y1 = MAX (self->rubber_start.y, self->rubber_cur.y);
  char buf[64];

  data_remove_all_selected (self->diagram);
  self->selected = NULL;
  for (int i = 0; i < n; i++) {
    DiaObject *o = dia_layer_object_get_nth (layer, i);
    DiaRectangle *bb = &o->bounding_box;

    if (!(bb->right < x0 || bb->left > x1 || bb->bottom < y0 || bb->top > y1)) {
      data_select (self->diagram, o);
      self->selected = o;
    }
  }
  g_snprintf (buf, sizeof (buf), _("Selected %u object(s)"),
              g_list_length (self->diagram->selected));
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
}

static void
on_canvas_drag_end (GtkGestureDrag *gesture,
                    double          offset_x,
                    double          offset_y,
                    DiaShell       *self)
{
  DiaObjectChange *change;
  Point to;

  if (g_strcmp0 (self->tool, "Modify") != 0 || self->page_scale <= 0.0) {
    return;
  }

  if (self->rubber_band) {
    self->rubber_band = FALSE;
    rubber_band_select (self);
    gtk_widget_queue_draw (self->canvas);
    return;
  }

  if (!self->drag_handle && self->drag_moves->len > 0) {
    for (guint i = 0; i < self->drag_moves->len; i++) {
      MoveItem *it = &g_array_index (self->drag_moves, MoveItem, i);

      to.x = it->start.x + offset_x / self->page_scale;
      to.y = it->start.y + offset_y / self->page_scale;
      snap_to_grid (self, &to);
      update_connections_for (it->obj);
      if (to.x != it->start.x || to.y != it->start.y) {
        push_op (self, OP_MOVE, it->obj, it->start, to);
      }
    }
    g_array_set_size (self->drag_moves, 0);
    gtk_widget_queue_draw (self->canvas);
    return;
  }

  if (self->drag_handle && self->selected) {
    to.x = self->drag_handle_start.x + offset_x / self->page_scale;
    to.y = self->drag_handle_start.y + offset_y / self->page_scale;
    snap_to_grid (self, &to);
    change = dia_object_move_handle (self->selected, self->drag_handle, &to,
                                     NULL, HANDLE_MOVE_USER_FINAL, 0);
    g_clear_pointer (&change, dia_object_change_unref);
    if (to.x != self->drag_handle_start.x ||
        to.y != self->drag_handle_start.y) {
      push_op_handle (self, self->selected, self->drag_handle_idx,
                      self->drag_handle_start, to);
    }
    /* If a connectable endpoint was dropped near another object's connection
     * point, connect it and snap onto the point. */
    if (self->drag_handle->connect_type == HANDLE_CONNECTABLE) {
      DiaLayer *layer = dia_diagram_data_get_active_layer (self->diagram);
      ConnectionPoint *cp = NULL;
      real dist = layer ? dia_layer_find_closest_connectionpoint (
                            layer, &cp, &to, self->selected) : 1e9;

      if (cp && dist < 0.5) {
        object_connect (self->selected, self->drag_handle, cp);
        change = dia_object_move_handle (self->selected, self->drag_handle,
                                         &cp->pos, cp, HANDLE_MOVE_CONNECTED, 0);
        g_clear_pointer (&change, dia_object_change_unref);
        gtk_label_set_text (GTK_LABEL (self->status_msg), _("Connected"));
      }
    }
    self->drag_handle = NULL;
    self->drag_handle_idx = -1;
    gtk_widget_queue_draw (self->canvas);
    return;
  }
}


/* Canvas key bindings: Delete/BackSpace remove the selection, Escape clears it. */
static gboolean
on_canvas_key (GtkEventControllerKey *controller,
               guint                  keyval,
               guint                  keycode,
               GdkModifierType        state,
               DiaShell              *self)
{
  /* Ctrl+X/C/V/A dispatch through the "dia" action group (cut/copy/paste/
   * select-all), so the shortcut and the menu item share one implementation. */
  if (state & GDK_CONTROL_MASK) {
    const char *act = NULL;

    switch (keyval) {
      case GDK_KEY_x: case GDK_KEY_X: act = "cut";        break;
      case GDK_KEY_c: case GDK_KEY_C: act = "copy";       break;
      case GDK_KEY_v: case GDK_KEY_V: act = "paste";      break;
      case GDK_KEY_a: case GDK_KEY_A: act = "select-all"; break;
      case GDK_KEY_z: case GDK_KEY_Z: act = "undo";       break;
      case GDK_KEY_y: case GDK_KEY_Y: act = "redo";       break;
      default: break;
    }
    if (act) {
      g_action_group_activate_action (self->actions, act, NULL);
      return TRUE;
    }
  }

  switch (keyval) {
    case GDK_KEY_Delete:
    case GDK_KEY_BackSpace:
      delete_selected (self);
      return TRUE;
    case GDK_KEY_Escape:
      data_remove_all_selected (self->diagram);
      self->selected = NULL;
      gtk_widget_queue_draw (self->canvas);
      gtk_label_set_text (GTK_LABEL (self->status_msg), _("Selection cleared"));
      return TRUE;
    default:
      return FALSE;
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
  int cw = gtk_widget_get_width (self->canvas);
  int ch = gtk_widget_get_height (self->canvas);
  double old_pxcm = shell_pxcm (self);
  double cx, cy, new_pxcm;

  if (cw <= 1) cw = 600;
  if (ch <= 1) ch = 400;

  /* Zoom about the centre of the viewport: keep that cm point fixed. */
  cx = self->origin_x + (cw / old_pxcm) / 2.0;
  cy = self->origin_y + (ch / old_pxcm) / 2.0;

  self->zoom = CLAMP (self->zoom * factor, 0.1, 20.0);
  new_pxcm = shell_pxcm (self);
  self->origin_x = cx - (cw / new_pxcm) / 2.0;
  self->origin_y = cy - (ch / new_pxcm) / 2.0;

  clamp_origin_to_page (self);   /* keep the page edge in view */
  update_transform (self);
  update_scrollbars (self);

  g_snprintf (buf, sizeof (buf), "%.0f%%", self->zoom * 100.0);
  gtk_editable_set_text (GTK_EDITABLE (self->zoom_label), buf);
  redraw_canvas_and_rulers (self);
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
  update_zoom (self, 1.0 / self->zoom);   /* back to 100%, centred */
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
action_delete (GSimpleAction *a, GVariant *p, gpointer data)
{
  delete_selected (data);
}

static void
action_copy (GSimpleAction *a, GVariant *p, gpointer data)
{
  DiaShell *self = data;

  if (!self->selected) {
    return;
  }
  dia_clear_object (&self->clipboard);
  self->clipboard = dia_object_clone (self->selected);
  gtk_label_set_text (GTK_LABEL (self->status_msg), _("Copied"));
}

static void
action_cut (GSimpleAction *a, GVariant *p, gpointer data)
{
  DiaShell *self = data;

  if (!self->selected) {
    return;
  }
  dia_clear_object (&self->clipboard);
  self->clipboard = dia_object_clone (self->selected);
  delete_selected (self);
  gtk_label_set_text (GTK_LABEL (self->status_msg), _("Cut"));
}

static void
action_paste (GSimpleAction *a, GVariant *p, gpointer data)
{
  DiaShell *self = data;
  DiaLayer *layer;
  DiaObject *obj;
  DiaObjectChange *change;
  Point to;

  if (!self->clipboard) {
    return;
  }
  layer = dia_diagram_data_get_active_layer (self->diagram);
  obj = dia_object_clone (self->clipboard);
  /* offset a little so the paste doesn't sit exactly on the original */
  to.x = obj->position.x + 1.0;
  to.y = obj->position.y + 1.0;
  change = dia_object_move (obj, &to);
  g_clear_pointer (&change, dia_object_change_unref);

  dia_layer_add_object (layer, obj);
  push_op (self, OP_CREATE, obj, obj->position, obj->position);
  data_remove_all_selected (self->diagram);
  data_select (self->diagram, obj);
  self->selected = obj;
  gtk_widget_queue_draw (self->canvas);
  gtk_label_set_text (GTK_LABEL (self->status_msg), _("Pasted"));
}

static void
action_select_all (GSimpleAction *a, GVariant *p, gpointer data)
{
  DiaShell *self = data;
  DiaLayer *layer = dia_diagram_data_get_active_layer (self->diagram);
  int n = layer ? dia_layer_object_count (layer) : 0;
  char buf[64];

  data_remove_all_selected (self->diagram);
  for (int i = 0; i < n; i++) {
    data_select (self->diagram, dia_layer_object_get_nth (layer, i));
  }
  /* operations act on a single primary selection; use the topmost object */
  self->selected = n > 0 ? dia_layer_object_get_nth (layer, n - 1) : NULL;
  gtk_widget_queue_draw (self->canvas);
  g_snprintf (buf, sizeof (buf), _("Selected all (%d)"), n);
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
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
/* Render the whole diagram to an image/vector file, sized to its extents (plus
 * a small margin). Format is chosen by extension: .pdf, .svg, else PNG. Uses
 * the same DiaCairoRenderer the canvas draws with. This is the export backend
 * shared by the GUI Export action and the --export CLI option. */
gboolean
diagram_export_file (DiagramData *data, const char *path)
{
  char *lower = g_ascii_strdown (path, -1);
  gboolean is_pdf = g_str_has_suffix (lower, ".pdf");
  gboolean is_svg = g_str_has_suffix (lower, ".svg");
  DiaRectangle *ext;
  double margin = 0.5;                          /* cm */
  double dpcm = (is_pdf || is_svg) ? (72.0 / 2.54) : 40.0;  /* pts/cm or px/cm */
  double w_cm, h_cm;
  cairo_surface_t *surface;
  cairo_t *cr;
  DiaRenderer *renderer;
  gboolean ok = TRUE;

  g_free (lower);
  data_update_extents (data);
  ext = &data->extents;
  w_cm = (ext->right - ext->left) + 2 * margin;
  h_cm = (ext->bottom - ext->top) + 2 * margin;
  if (w_cm < 1.0) w_cm = 1.0;
  if (h_cm < 1.0) h_cm = 1.0;

  if (is_pdf) {
    surface = cairo_pdf_surface_create (path, w_cm * dpcm, h_cm * dpcm);
  } else if (is_svg) {
    surface = cairo_svg_surface_create (path, w_cm * dpcm, h_cm * dpcm);
  } else {
    surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                          (int) ceil (w_cm * dpcm),
                                          (int) ceil (h_cm * dpcm));
  }
  if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy (surface);
    return FALSE;
  }

  cr = cairo_create (surface);
  cairo_set_source_rgb (cr, 1, 1, 1);           /* white page */
  cairo_paint (cr);
  cairo_scale (cr, dpcm, dpcm);
  cairo_translate (cr, margin - ext->left, margin - ext->top);

  renderer = g_object_new (DIA_CAIRO_TYPE_RENDERER, NULL);
  DIA_CAIRO_RENDERER (renderer)->cr = cairo_reference (cr);
  DIA_CAIRO_RENDERER (renderer)->with_alpha = FALSE;
  data_render (data, renderer, NULL, NULL, NULL);
  g_object_unref (renderer);

  cairo_show_page (cr);
  cairo_surface_flush (surface);
  if (!is_pdf && !is_svg) {
    ok = (cairo_surface_write_to_png (surface, path) == CAIRO_STATUS_SUCCESS);
  }
  cairo_destroy (cr);
  cairo_surface_destroy (surface);
  return ok;
}

/* Load a .dia file into a fresh DiagramData (no shell/GUI). Caller unrefs.
 * Assumes libdia_init + object-type registration already happened. */
static DiagramData *
diagram_load_standalone (const char *path)
{
  DiaContext *ctx = dia_context_new ("Load Diagram");
  xmlDocPtr doc;
  DiagramData *data = NULL;

  /* So objects can resolve relative paths (e.g. images) against the diagram's
   * directory rather than the current working directory (GNOME/dia#524). */
  dia_context_set_filename (ctx, path);
  doc = dia_io_load_document (path, ctx, NULL);

  if (doc) {
    xmlNodePtr root = xmlDocGetRootElement (doc);
    DiaLayer *layer;

    data = g_object_new (DIA_TYPE_DIAGRAM_DATA, NULL);
    layer = dia_diagram_data_get_active_layer (data);

    for (xmlNodePtr ln = root ? root->children : NULL; ln; ln = ln->next) {
      if (xmlStrcmp (ln->name, (const xmlChar *) "layer") != 0) {
        continue;
      }
      for (xmlNodePtr on = ln->children; on; on = on->next) {
        char *type_name, *version_str;
        DiaObjectType *type;

        if (xmlStrcmp (on->name, (const xmlChar *) "object") != 0) {
          continue;
        }
        type_name = (char *) xmlGetProp (on, (const xmlChar *) "type");
        version_str = (char *) xmlGetProp (on, (const xmlChar *) "version");
        type = type_name ? object_get_type (type_name) : NULL;
        if (type && type->ops->load) {
          DiaObject *obj = type->ops->load (on,
                                            version_str ? atoi (version_str) : 0,
                                            ctx);
          if (obj) {
            dia_layer_add_object (layer, obj);
          }
        }
        if (type_name) xmlFree (type_name);
        if (version_str) xmlFree (version_str);
      }
    }
    xmlFreeDoc (doc);
  }
  dia_context_release (ctx);
  return data;
}

/* Console message handler for the headless CLI: libdia normally pops a GTK
 * dialog (message.c), which crashes without a display, so route messages to
 * stderr instead. */
static void
cli_message (const char *title, enum ShowAgainStyle showAgain,
             const char *fmt, va_list args)
{
  char *body = g_strdup_vprintf (fmt, args);

  g_printerr ("dia: %s: %s\n", title ? title : "message", body);
  g_free (body);
}

/* Headless CLI: load @infile (.dia) and export it to @outfile, format chosen by
 * @outfile's extension. Returns a process exit code. Needs no display. */
int
dia_shell_export_cli (const char *infile, const char *outfile)
{
  DiagramData *data;
  gboolean ok;

  set_message_func (cli_message);   /* no GUI dialogs in headless mode */
  libdia_init (DIA_INTERACTIVE);
  register_standard_object_types ();

  data = diagram_load_standalone (infile);
  if (!data) {
    g_printerr ("dia: cannot load '%s'\n", infile);
    return 1;
  }
  ok = diagram_export_file (data, outfile);
  g_object_unref (data);

  if (!ok) {
    g_printerr ("dia: export to '%s' failed\n", outfile);
    return 1;
  }
  g_print ("Exported '%s' -> '%s'\n", infile, outfile);
  return 0;
}

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
  xmlDocPtr doc;

  /* Resolve relative paths (images) against the diagram's folder (#524). */
  dia_context_set_filename (ctx, path);
  doc = dia_io_load_document (path, ctx, NULL);

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


/* UI-test hook (DIA_UITEST): create a box, then drag one of its handles out
 * via dia_object_move_handle() — the SAME call the canvas handle-drag uses —
 * and verify the object actually grew. Proves resize/stretch works. */
static void
on_uitest_resize (GtkButton *button, DiaShell *self)
{
  DiaObject *obj;
  DiaObjectChange *change;
  Handle *h;
  Point to;
  double w0, h0, w1, h1;
  char buf[128];

  obj = diagram_create_object (self, "Standard - Box", (Point) { 5, 5 });
  if (!obj || obj->num_handles < 1) {
    gtk_label_set_text (GTK_LABEL (self->status_msg),
                        _("resize FAIL (no object)"));
    return;
  }
  push_op (self, OP_CREATE, obj, (Point) { 5, 5 }, (Point) { 5, 5 });
  data_remove_all_selected (self->diagram);
  data_select (self->diagram, obj);
  self->selected = obj;

  w0 = obj->bounding_box.right - obj->bounding_box.left;
  h0 = obj->bounding_box.bottom - obj->bounding_box.top;

  /* The last handle is a corner; drag it out 2 cm each way. */
  h = obj->handles[obj->num_handles - 1];
  to.x = h->pos.x + 2.0;
  to.y = h->pos.y + 2.0;
  change = dia_object_move_handle (obj, h, &to, NULL, HANDLE_MOVE_USER_FINAL, 0);
  g_clear_pointer (&change, dia_object_change_unref);

  w1 = obj->bounding_box.right - obj->bounding_box.left;
  h1 = obj->bounding_box.bottom - obj->bounding_box.top;

  if (w1 > w0 + 0.5 || h1 > h0 + 0.5) {
    g_snprintf (buf, sizeof (buf),
                _("resize OK (%.1fx%.1f -> %.1fx%.1f)"), w0, h0, w1, h1);
  } else {
    g_snprintf (buf, sizeof (buf),
                _("resize FAIL (%.1fx%.1f -> %.1fx%.1f)"), w0, h0, w1, h1);
  }
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
  gtk_widget_queue_draw (self->canvas);
}


/* UI-test hook (DIA_UITEST): create -> delete -> undo, verifying the object
 * count goes N -> N+1 -> N -> N+1 (delete removes, undo restores). */
static void
on_uitest_delete (GtkButton *button, DiaShell *self)
{
  guint c0, c1, c2, c3;
  DiaObject *obj;
  char buf[96];

  c0 = diagram_object_count (self);
  obj = diagram_create_object (self, "Standard - Box", (Point) { 7, 7 });
  if (obj) {
    push_op (self, OP_CREATE, obj, (Point) { 7, 7 }, (Point) { 7, 7 });
  }
  c1 = diagram_object_count (self);

  data_remove_all_selected (self->diagram);
  data_select (self->diagram, obj);
  self->selected = obj;
  delete_selected (self);
  c2 = diagram_object_count (self);

  if (self->undo_pos > 0) {
    self->undo_pos--;
    op_revert (self, g_ptr_array_index (self->undo, self->undo_pos));
    update_undo_actions (self);
  }
  c3 = diagram_object_count (self);

  if (c1 == c0 + 1 && c2 == c0 && c3 == c1) {
    g_snprintf (buf, sizeof (buf), _("delete OK (%u/%u/%u/%u)"), c0, c1, c2, c3);
  } else {
    g_snprintf (buf, sizeof (buf), _("delete FAIL (%u/%u/%u/%u)"), c0, c1, c2, c3);
  }
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
  gtk_widget_queue_draw (self->canvas);
}


/* UI-test hook (DIA_UITEST): zoom in then out and confirm the zoom level
 * tracked (the editable entry's text is not reliably exposed over AT-SPI, so
 * we assert on the model). */
static void
on_uitest_zoom (GtkButton *button, DiaShell *self)
{
  double z0 = self->zoom, z1, z2;
  char buf[96];

  update_zoom (self, 1.5);
  z1 = self->zoom;
  update_zoom (self, 1.0 / 1.5);
  z2 = self->zoom;

  if (z1 > z0 * 1.4 && z1 < z0 * 1.6 && z2 > z0 * 0.95 && z2 < z0 * 1.05) {
    g_snprintf (buf, sizeof (buf), _("zoom OK (%.0f%%/%.0f%%/%.0f%%)"),
                z0 * 100, z1 * 100, z2 * 100);
  } else {
    g_snprintf (buf, sizeof (buf), _("zoom FAIL (%.0f/%.0f/%.0f)"),
                z0 * 100, z1 * 100, z2 * 100);
  }
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
}


/* UI-test hook (DIA_UITEST): snap a point to the grid and confirm rounding. */
static void
on_uitest_snap (GtkButton *button, DiaShell *self)
{
  Point p = { 5.3, 5.2 };
  gboolean saved = self->snap_grid;
  char buf[96];

  self->snap_grid = TRUE;
  snap_to_grid (self, &p);          /* expect 5.5, 5.0 */
  self->snap_grid = saved;

  if (p.x == 5.5 && p.y == 5.0) {
    g_snprintf (buf, sizeof (buf), _("snap OK (%.2f, %.2f)"), p.x, p.y);
  } else {
    g_snprintf (buf, sizeof (buf), _("snap FAIL (%.2f, %.2f)"), p.x, p.y);
  }
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
}


static void on_layer_add (GtkButton *b, DiaShell *self);
static void on_layer_remove (GtkButton *b, DiaShell *self);

/* UI-test hook (DIA_UITEST): add a layer then remove it, confirming the layer
 * count and the list track (exercises the wired layer buttons' logic). */
static void
on_uitest_layers (GtkButton *button, DiaShell *self)
{
  int c0, c1, c2;
  char buf[96];

  c0 = data_layer_count (self->diagram);
  on_layer_add (NULL, self);
  c1 = data_layer_count (self->diagram);
  on_layer_remove (NULL, self);
  c2 = data_layer_count (self->diagram);

  if (c1 == c0 + 1 && c2 == c0) {
    g_snprintf (buf, sizeof (buf), _("layers OK (%d/%d/%d)"), c0, c1, c2);
  } else {
    g_snprintf (buf, sizeof (buf), _("layers FAIL (%d/%d/%d)"), c0, c1, c2);
  }
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
}


/* UI-test hook (DIA_UITEST): set a line width, create a box with it, then read
 * the property back to confirm the attribute round-trips through StdProp. */
static void
on_uitest_lineattr (GtkButton *button, DiaShell *self)
{
  static PropDescription d[] = { PROP_STD_LINE_WIDTH, PROP_DESC_END };
  double saved = self->line_width;
  DiaObject *obj;
  GPtrArray *props;
  double got = -1.0;
  char buf[96];

  self->line_width = 0.42;
  obj = diagram_create_object (self, "Standard - Box", (Point) { 4, 4 });
  if (obj) {
    apply_line_props (self, obj);
    push_op (self, OP_CREATE, obj, (Point) { 4, 4 }, (Point) { 4, 4 });
    props = prop_list_from_descs (d, pdtpp_true);
    dia_object_get_properties (obj, props);
    got = ((RealProperty *) g_ptr_array_index (props, 0))->real_data;
    prop_list_free (props);
  }
  self->line_width = saved;

  if (obj && fabs (got - 0.42) < 0.001) {
    g_snprintf (buf, sizeof (buf), _("lineattr OK (w=%.2f)"), got);
  } else {
    g_snprintf (buf, sizeof (buf), _("lineattr FAIL (w=%.2f)"), got);
  }
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
  gtk_widget_queue_draw (self->canvas);
}


/* UI-test hook (DIA_UITEST): export the diagram to a temp PNG and confirm a
 * non-trivial file was written (exercises the export backend). */
static void
on_uitest_export (GtkButton *button, DiaShell *self)
{
  char *path = g_build_filename (g_get_tmp_dir (), "dia-uitest-export.png", NULL);
  GStatBuf st;
  gboolean ok;
  char buf[128];

  g_remove (path);
  ok = diagram_export_file (self->diagram, path)
       && g_stat (path, &st) == 0 && st.st_size > 100;
  if (ok) {
    g_snprintf (buf, sizeof (buf), _("export OK (%ld bytes)"), (long) st.st_size);
  } else {
    g_snprintf (buf, sizeof (buf), _("export FAIL"));
  }
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
  g_remove (path);
  g_free (path);
}


/* UI-test hook (DIA_UITEST): create -> copy -> paste, verifying the count goes
 * N -> N+1 -> N+2 and a clipboard object is held. */
static void
on_uitest_clipboard (GtkButton *button, DiaShell *self)
{
  guint c0, c1, c2;
  DiaObject *obj;
  char buf[96];

  c0 = diagram_object_count (self);
  obj = diagram_create_object (self, "Standard - Box", (Point) { 9, 9 });
  if (obj) {
    push_op (self, OP_CREATE, obj, (Point) { 9, 9 }, (Point) { 9, 9 });
  }
  data_remove_all_selected (self->diagram);
  data_select (self->diagram, obj);
  self->selected = obj;
  c1 = diagram_object_count (self);

  action_copy (NULL, NULL, self);
  action_paste (NULL, NULL, self);
  c2 = diagram_object_count (self);

  if (c1 == c0 + 1 && c2 == c1 + 1 && self->clipboard != NULL) {
    g_snprintf (buf, sizeof (buf), _("clipboard OK (%u/%u/%u)"), c0, c1, c2);
  } else {
    g_snprintf (buf, sizeof (buf), _("clipboard FAIL (%u/%u/%u)"), c0, c1, c2);
  }
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
  gtk_widget_queue_draw (self->canvas);
}


/* UI-test hook (DIA_UITEST): pick a sheet shape as the tool and create it,
 * confirming a sheet object type instantiates from the drawer path. */
static void
on_uitest_sheet (GtkButton *button, DiaShell *self)
{
  guint c0 = diagram_object_count (self);
  DiaLayer *layer;
  DiaObject *obj;
  int n;
  char buf[96];

  g_strlcpy (self->tool, "Flowchart - Diamond", sizeof (self->tool));
  apply_tool_at (self, (Point) { 11, 11 });

  layer = dia_diagram_data_get_active_layer (self->diagram);
  n = dia_layer_object_count (layer);
  obj = n > 0 ? dia_layer_object_get_nth (layer, n - 1) : NULL;

  if (diagram_object_count (self) == c0 + 1 && obj && obj->type &&
      g_strcmp0 (obj->type->name, "Flowchart - Diamond") == 0) {
    g_snprintf (buf, sizeof (buf), _("sheet OK (%s)"), obj->type->name);
  } else {
    g_snprintf (buf, sizeof (buf), _("sheet FAIL"));
  }
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
  gtk_widget_queue_draw (self->canvas);
}


/* UI-test hook (DIA_UITEST): connect a line endpoint to a box's connection
 * point, move the box, and confirm the endpoint tracked the connection. */
static void
on_uitest_connect (GtkButton *button, DiaShell *self)
{
  DiaObject *box, *line;
  Handle *lh;
  ConnectionPoint *cp = NULL;
  DiaLayer *layer;
  DiaObjectChange *ch;
  Point old_cp;
  char buf[128];

  box = diagram_create_object (self, "Standard - Box", (Point) { 5, 5 });
  line = diagram_create_object (self, "Standard - Line", (Point) { 12, 10 });
  if (!box || !line || line->num_handles < 1) {
    gtk_label_set_text (GTK_LABEL (self->status_msg),
                        _("connect FAIL (no objects)"));
    return;
  }
  push_op (self, OP_CREATE, box, (Point) { 5, 5 }, (Point) { 5, 5 });
  push_op (self, OP_CREATE, line, (Point) { 12, 10 }, (Point) { 12, 10 });

  layer = dia_diagram_data_get_active_layer (self->diagram);
  lh = line->handles[0];
  dia_layer_find_closest_connectionpoint (layer, &cp, &lh->pos, line);
  if (!cp) {
    gtk_label_set_text (GTK_LABEL (self->status_msg),
                        _("connect FAIL (no connection point)"));
    return;
  }

  /* connect + snap the endpoint onto the connection point */
  object_connect (line, lh, cp);
  ch = dia_object_move_handle (line, lh, &cp->pos, cp, HANDLE_MOVE_CONNECTED, 0);
  g_clear_pointer (&ch, dia_object_change_unref);
  old_cp = cp->pos;

  /* move the object that owns the CP; the connected endpoint should follow it */
  {
    DiaObject *target = cp->object;
    Point np = { target->position.x + 3.0, target->position.y + 3.0 };

    ch = dia_object_move (target, &np);
    g_clear_pointer (&ch, dia_object_change_unref);
    update_connections_for (target);
  }
  (void) box;

  if (lh->connected_to == cp
      && (cp->pos.x != old_cp.x || cp->pos.y != old_cp.y)
      && fabs (lh->pos.x - cp->pos.x) < 0.01
      && fabs (lh->pos.y - cp->pos.y) < 0.01) {
    g_snprintf (buf, sizeof (buf), _("connect OK (endpoint at %.1f, %.1f)"),
                lh->pos.x, lh->pos.y);
  } else {
    g_snprintf (buf, sizeof (buf),
                _("connect FAIL conn=%d cpmoved=%d lh=(%.2f,%.2f) cp=(%.2f,%.2f)"),
                lh->connected_to == cp,
                (cp->pos.x != old_cp.x || cp->pos.y != old_cp.y),
                lh->pos.x, lh->pos.y, cp->pos.x, cp->pos.y);
  }
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
  gtk_widget_queue_draw (self->canvas);
}


/* UI-test hook (DIA_UITEST): set a text object's content and read it back,
 * exercising the same get/set the double-click editor uses. */
static void
on_uitest_text (GtkButton *button, DiaShell *self)
{
  DiaObject *obj = diagram_create_object (self, "Standard - Text", (Point) { 6, 6 });
  char *got;
  char buf[96];

  if (!obj) {
    gtk_label_set_text (GTK_LABEL (self->status_msg), _("text FAIL (no object)"));
    return;
  }
  push_op (self, OP_CREATE, obj, (Point) { 6, 6 }, (Point) { 6, 6 });
  set_object_text (obj, "Hello Dia");
  got = get_object_text (obj);
  if (got && g_strcmp0 (got, "Hello Dia") == 0) {
    g_snprintf (buf, sizeof (buf), _("text OK (%s)"), got);
  } else {
    g_snprintf (buf, sizeof (buf), _("text FAIL (%s)"), got ? got : "(null)");
  }
  g_free (got);
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
  gtk_widget_queue_draw (self->canvas);
}


/* UI-test hook (DIA_UITEST): rubber-band-select two of three boxes (placed
 * away from the seeded objects) and delete them, verifying multi-select and
 * multi-delete. */
static void
on_uitest_multiselect (GtkButton *button, DiaShell *self)
{
  guint c0 = diagram_object_count (self);
  DiaObject *objs[3];
  const Point pts[3] = { { 50, 50 }, { 53, 50 }, { 80, 80 } };
  guint nsel, after;
  char buf[96];

  for (int i = 0; i < 3; i++) {
    objs[i] = diagram_create_object (self, "Standard - Box", pts[i]);
    if (objs[i]) {
      push_op (self, OP_CREATE, objs[i], pts[i], pts[i]);
    }
  }
  /* a rect covering the first two boxes but not the third (nor the seeded set) */
  self->rubber_start = (Point) { 48, 48 };
  self->rubber_cur = (Point) { 56, 54 };
  rubber_band_select (self);
  nsel = g_list_length (self->diagram->selected);

  delete_selected (self);
  after = diagram_object_count (self);

  if (nsel == 2 && after == c0 + 1) {
    g_snprintf (buf, sizeof (buf), _("multiselect OK (sel=%u left=%u)"),
                nsel, after);
  } else {
    g_snprintf (buf, sizeof (buf), _("multiselect FAIL (sel=%u left=%u)"),
                nsel, after);
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
export_done (GObject *source, GAsyncResult *res, gpointer data)
{
  DiaShell *self = data;
  GFile *file = gtk_file_dialog_save_finish (GTK_FILE_DIALOG (source), res, NULL);

  if (file) {
    char *path = g_file_get_path (file);
    gboolean ok = path && diagram_export_file (self->diagram, path);

    gtk_label_set_text (GTK_LABEL (self->status_msg),
                        ok ? _("Exported diagram") : _("Export failed"));
    g_free (path);
    g_object_unref (file);
  }
}

static void
action_export (GSimpleAction *a, GVariant *p, gpointer data)
{
  DiaShell *self = data;
  GtkFileDialog *dialog = gtk_file_dialog_new ();
  GtkRoot *root = gtk_widget_get_root (self->canvas);
  GListStore *filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  const struct { const char *name, *pat; } kinds[] = {
    { N_("PNG image (*.png)"), "*.png" },
    { N_("PDF document (*.pdf)"), "*.pdf" },
    { N_("SVG image (*.svg)"), "*.svg" },
  };

  for (gsize i = 0; i < G_N_ELEMENTS (kinds); i++) {
    GtkFileFilter *f = gtk_file_filter_new ();
    gtk_file_filter_set_name (f, gettext (kinds[i].name));
    gtk_file_filter_add_pattern (f, kinds[i].pat);
    g_list_store_append (filters, f);
    g_object_unref (f);
  }
  gtk_file_dialog_set_title (dialog, _("Export Diagram"));
  gtk_file_dialog_set_modal (dialog, FALSE);
  gtk_file_dialog_set_initial_name (dialog, "diagram.png");
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
  gtk_file_dialog_save (dialog, GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL,
                        NULL, export_done, self);
  g_object_unref (filters);
  g_object_unref (dialog);
}

/* Render the whole diagram onto the print page (one page), scaled cm -> points
 * with the same DiaCairoRenderer the canvas/export use. */
static void
on_print_draw_page (GtkPrintOperation *op, GtkPrintContext *pctx, int page,
                    DiaShell *self)
{
  cairo_t *cr = gtk_print_context_get_cairo_context (pctx);
  DiaRenderer *renderer;
  DiaRectangle *ext;

  data_update_extents (self->diagram);
  ext = &self->diagram->extents;

  cairo_save (cr);
  cairo_scale (cr, 72.0 / 2.54, 72.0 / 2.54);   /* the print cr is in points */
  cairo_translate (cr, 0.5 - ext->left, 0.5 - ext->top);
  renderer = g_object_new (DIA_CAIRO_TYPE_RENDERER, NULL);
  DIA_CAIRO_RENDERER (renderer)->cr = cairo_reference (cr);
  DIA_CAIRO_RENDERER (renderer)->with_alpha = FALSE;
  data_render (self->diagram, renderer, NULL, NULL, NULL);
  g_object_unref (renderer);
  cairo_restore (cr);
}

static void
action_print (GSimpleAction *a, GVariant *p, gpointer data)
{
  DiaShell *self = data;
  GtkPrintOperation *op = gtk_print_operation_new ();
  GtkRoot *root = gtk_widget_get_root (self->canvas);

  gtk_print_operation_set_n_pages (op, 1);
  g_signal_connect (op, "draw-page", G_CALLBACK (on_print_draw_page), self);
  gtk_print_operation_run (op, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                           GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL, NULL);
  g_object_unref (op);
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
  { "export",     action_export,     NULL, NULL, NULL },
  { "print",      action_print,      NULL, NULL, NULL },
  { "undo",       action_undo,       NULL, NULL, NULL },
  { "redo",       action_redo,       NULL, NULL, NULL },
  { "cut",        action_cut,        NULL, NULL, NULL },
  { "copy",       action_copy,       NULL, NULL, NULL },
  { "paste",      action_paste,      NULL, NULL, NULL },
  { "select-all", action_select_all, NULL, NULL, NULL },
  { "delete",     action_delete,     NULL, NULL, NULL },
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
  GMenu *edit = g_menu_new ();
  GMenu *view = g_menu_new ();
  GMenu *app = g_menu_new ();

  g_menu_append (file, _("_New"), "dia.new");
  g_menu_append (file, _("_Open…"), "dia.open");
  g_menu_append (file, _("_Save…"), "dia.save");
  g_menu_append (file, _("_Export…"), "dia.export");
  g_menu_append (file, _("_Print…"), "dia.print");
  g_menu_append_section (menu, NULL, G_MENU_MODEL (file));

  g_menu_append (edit, _("_Undo"), "dia.undo");
  g_menu_append (edit, _("_Redo"), "dia.redo");
  g_menu_append (edit, _("Cu_t"), "dia.cut");
  g_menu_append (edit, _("_Copy"), "dia.copy");
  g_menu_append (edit, _("_Paste"), "dia.paste");
  g_menu_append (edit, _("Select _All"), "dia.select-all");
  g_menu_append (edit, _("_Delete"), "dia.delete");
  g_menu_append_section (menu, NULL, G_MENU_MODEL (edit));

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
  g_object_unref (edit);
  g_object_unref (view);
  g_object_unref (app);

  return button;
}


/* Page presets for the toolbar dropdown (cm). "Infinite" is an unbounded
 * scrollable workspace with no page sheet. */
typedef struct { const char *label; double w, h; gboolean infinite; } PageSize;
static const PageSize page_sizes[] = {
  { N_("A4 Portrait"),   21.0, 29.7, FALSE },
  { N_("A4 Landscape"),  29.7, 21.0, FALSE },
  { N_("A3 Portrait"),   29.7, 42.0, FALSE },
  { N_("A3 Landscape"),  42.0, 29.7, FALSE },
  { N_("A2 Portrait"),   42.0, 59.4, FALSE },
  { N_("A2 Landscape"),  59.4, 42.0, FALSE },
  { N_("A1 Portrait"),   59.4, 84.1, FALSE },
  { N_("A1 Landscape"),  84.1, 59.4, FALSE },
  { N_("Letter"),        21.59, 27.94, FALSE },
  { N_("Infinite"),      0.0, 0.0, TRUE },
};

static void
on_page_size_changed (GtkDropDown *dd, GParamSpec *ps, DiaShell *self)
{
  guint i = gtk_drop_down_get_selected (dd);

  if (i >= G_N_ELEMENTS (page_sizes)) {
    return;
  }
  self->page_infinite = page_sizes[i].infinite;
  if (!self->page_infinite) {
    self->page_w_cm = page_sizes[i].w;
    self->page_h_cm = page_sizes[i].h;
  }
  update_scrollbars (self);
  redraw_canvas_and_rulers (self);
}

static GtkWidget *
build_page_size_dropdown (DiaShell *self)
{
  const char *labels[G_N_ELEMENTS (page_sizes) + 1];
  GtkWidget *dd;

  for (gsize i = 0; i < G_N_ELEMENTS (page_sizes); i++) {
    labels[i] = gettext (page_sizes[i].label);
  }
  labels[G_N_ELEMENTS (page_sizes)] = NULL;

  dd = gtk_drop_down_new_from_strings (labels);
  gtk_drop_down_set_selected (GTK_DROP_DOWN (dd), 0);   /* A4 portrait */
  gtk_widget_set_tooltip_text (dd, _("Page size"));
  set_a11y_label (dd, "page-size");
  g_signal_connect (dd, "notify::selected",
                    G_CALLBACK (on_page_size_changed), self);
  return dd;
}

static void
on_snap_grid_toggled (GtkToggleButton *b, DiaShell *self)
{
  self->snap_grid = gtk_toggle_button_get_active (b);
}

static void
on_snap_object_toggled (GtkToggleButton *b, DiaShell *self)
{
  self->snap_object = gtk_toggle_button_get_active (b);
}

static void
on_snap_guide_toggled (GtkToggleButton *b, DiaShell *self)
{
  self->snap_guide = gtk_toggle_button_get_active (b);
}

/* A flat toggle button with an icon from the resource bundle. */
static GtkWidget *
make_snap_toggle (const char *icon_res, const char *tip, const char *a11y,
                  gboolean active, GCallback cb, DiaShell *self)
{
  GtkWidget *b = gtk_toggle_button_new ();
  GtkWidget *img = gtk_image_new_from_resource (icon_res);

  gtk_image_set_pixel_size (GTK_IMAGE (img), 18);
  gtk_button_set_child (GTK_BUTTON (b), img);
  gtk_button_set_has_frame (GTK_BUTTON (b), FALSE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b), active);
  gtk_widget_set_tooltip_text (b, tip);
  set_a11y_label (b, a11y);
  g_signal_connect (b, "toggled", cb, self);
  return b;
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
    { "document-send-symbolic",  N_("Export"),      "dia.export" },
    { NULL, NULL, NULL },
    { "edit-undo-symbolic",      N_("Undo"),        "dia.undo" },
    { "edit-redo-symbolic",      N_("Redo"),        "dia.redo" },
    { NULL, NULL, NULL },
    { "edit-cut-symbolic",       N_("Cut"),         "dia.cut" },
    { "edit-copy-symbolic",      N_("Copy"),        "dia.copy" },
    { "edit-paste-symbolic",     N_("Paste"),       "dia.paste" },
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

  gtk_box_append (GTK_BOX (bar), gtk_separator_new (GTK_ORIENTATION_VERTICAL));
  gtk_box_append (GTK_BOX (bar), build_page_size_dropdown (self));

  /* Snapping toggles: grid (functional), object & guide (state for now). */
  gtk_box_append (GTK_BOX (bar), gtk_separator_new (GTK_ORIENTATION_VERTICAL));
  gtk_box_append (GTK_BOX (bar),
                  make_snap_toggle ("/org/gnome/Dia/icons/dia-grid-on.png",
                                    _("Snap to grid"), "snap-grid",
                                    self->snap_grid,
                                    G_CALLBACK (on_snap_grid_toggled), self));
  gtk_box_append (GTK_BOX (bar),
                  make_snap_toggle ("/org/gnome/Dia/icons/dia-mainpoints-on.png",
                                    _("Snap to objects"), "snap-object",
                                    self->snap_object,
                                    G_CALLBACK (on_snap_object_toggled), self));
  gtk_box_append (GTK_BOX (bar),
                  make_snap_toggle ("/org/gnome/Dia/icons/dia-guides-snap-on.png",
                                    _("Snap to guides"), "snap-guide",
                                    self->snap_guide,
                                    G_CALLBACK (on_snap_guide_toggled), self));

  /* UI-test-only trigger (see on_uitest_apply_tool). Absent in normal use.
   * The label IS the AT-SPI name the test searches for. */
  if (g_getenv ("DIA_UITEST")) {
    GtkWidget *t = gtk_button_new_with_label ("uitest-apply-tool");
    GtkWidget *r = gtk_button_new_with_label ("uitest-roundtrip");
    GtkWidget *m = gtk_button_new_with_label ("uitest-select-move");
    GtkWidget *u = gtk_button_new_with_label ("uitest-undo-redo");
    GtkWidget *x = gtk_button_new_with_label ("uitest-extra-objects");
    GtkWidget *z = gtk_button_new_with_label ("uitest-resize");
    GtkWidget *d = gtk_button_new_with_label ("uitest-delete");
    GtkWidget *zm = gtk_button_new_with_label ("uitest-zoom");
    GtkWidget *sn = gtk_button_new_with_label ("uitest-snap");
    GtkWidget *ly = gtk_button_new_with_label ("uitest-layers");
    GtkWidget *la = gtk_button_new_with_label ("uitest-lineattr");
    GtkWidget *ex = gtk_button_new_with_label ("uitest-export");
    GtkWidget *cb = gtk_button_new_with_label ("uitest-clipboard");
    GtkWidget *sh = gtk_button_new_with_label ("uitest-sheet");
    GtkWidget *cn = gtk_button_new_with_label ("uitest-connect");
    GtkWidget *tx = gtk_button_new_with_label ("uitest-text");
    GtkWidget *ms = gtk_button_new_with_label ("uitest-multiselect");
    gtk_button_set_has_frame (GTK_BUTTON (t), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (r), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (m), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (u), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (x), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (z), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (d), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (zm), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (sn), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (ly), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (la), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (ex), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (cb), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (sh), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (cn), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (tx), FALSE);
    gtk_button_set_has_frame (GTK_BUTTON (ms), FALSE);
    g_signal_connect (t, "clicked", G_CALLBACK (on_uitest_apply_tool), self);
    g_signal_connect (r, "clicked", G_CALLBACK (on_uitest_roundtrip), self);
    g_signal_connect (m, "clicked", G_CALLBACK (on_uitest_select_move), self);
    g_signal_connect (u, "clicked", G_CALLBACK (on_uitest_undo_redo), self);
    g_signal_connect (x, "clicked", G_CALLBACK (on_uitest_extra_objects), self);
    g_signal_connect (z, "clicked", G_CALLBACK (on_uitest_resize), self);
    g_signal_connect (d, "clicked", G_CALLBACK (on_uitest_delete), self);
    g_signal_connect (zm, "clicked", G_CALLBACK (on_uitest_zoom), self);
    g_signal_connect (sn, "clicked", G_CALLBACK (on_uitest_snap), self);
    g_signal_connect (ly, "clicked", G_CALLBACK (on_uitest_layers), self);
    g_signal_connect (la, "clicked", G_CALLBACK (on_uitest_lineattr), self);
    g_signal_connect (ex, "clicked", G_CALLBACK (on_uitest_export), self);
    g_signal_connect (cb, "clicked", G_CALLBACK (on_uitest_clipboard), self);
    g_signal_connect (sh, "clicked", G_CALLBACK (on_uitest_sheet), self);
    g_signal_connect (cn, "clicked", G_CALLBACK (on_uitest_connect), self);
    g_signal_connect (tx, "clicked", G_CALLBACK (on_uitest_text), self);
    g_signal_connect (ms, "clicked", G_CALLBACK (on_uitest_multiselect), self);
    gtk_box_append (GTK_BOX (bar), t);
    gtk_box_append (GTK_BOX (bar), r);
    gtk_box_append (GTK_BOX (bar), m);
    gtk_box_append (GTK_BOX (bar), u);
    gtk_box_append (GTK_BOX (bar), x);
    gtk_box_append (GTK_BOX (bar), z);
    gtk_box_append (GTK_BOX (bar), d);
    gtk_box_append (GTK_BOX (bar), zm);
    gtk_box_append (GTK_BOX (bar), sn);
    gtk_box_append (GTK_BOX (bar), ly);
    gtk_box_append (GTK_BOX (bar), la);
    gtk_box_append (GTK_BOX (bar), ex);
    gtk_box_append (GTK_BOX (bar), cb);
    gtk_box_append (GTK_BOX (bar), sh);
    gtk_box_append (GTK_BOX (bar), cn);
    gtk_box_append (GTK_BOX (bar), tx);
    gtk_box_append (GTK_BOX (bar), ms);
  }

  return bar;
}


static void
on_lw_changed (GtkSpinButton *sb, DiaShell *self)
{
  self->line_width = gtk_spin_button_get_value (sb);
  on_line_attrs_changed (self);
}

static void
on_lstyle_changed (GtkDropDown *dd, GParamSpec *ps, DiaShell *self)
{
  self->line_style = (DiaLineStyle) gtk_drop_down_get_selected (dd);
  on_line_attrs_changed (self);
}

static void
on_start_arrow_changed (GtkDropDown *dd, GParamSpec *ps, DiaShell *self)
{
  guint i = gtk_drop_down_get_selected (dd);

  if (i < G_N_ELEMENTS (arrow_choices)) {
    self->start_arrow = arrow_choices[i];
  }
  on_line_attrs_changed (self);
}

static void
on_end_arrow_changed (GtkDropDown *dd, GParamSpec *ps, DiaShell *self)
{
  guint i = gtk_drop_down_get_selected (dd);

  if (i < G_N_ELEMENTS (arrow_choices)) {
    self->end_arrow = arrow_choices[i];
  }
  on_line_attrs_changed (self);
}

/* Preview drawer: a line in the given DiaLineStyle (item position == style). */
static void
draw_linestyle_item (GtkDrawingArea *area, cairo_t *cr, int w, int h,
                     gpointer data)
{
  int style = GPOINTER_TO_INT (data);
  double y = h / 2.0;
  static const struct { int n; double d[6]; } pats[] = {
    { 0, { 0 } },                      /* solid        */
    { 2, { 6, 4 } },                   /* dashed       */
    { 4, { 6, 3, 1.5, 3 } },           /* dash-dot     */
    { 6, { 6, 3, 1.5, 3, 1.5, 3 } },   /* dash-dot-dot */
    { 2, { 1.5, 3 } },                 /* dotted       */
  };

  cairo_set_source_rgb (cr, 0.1, 0.1, 0.1);
  cairo_set_line_width (cr, 1.5);
  if (style >= 0 && style < (int) G_N_ELEMENTS (pats)) {
    cairo_set_dash (cr, pats[style].d, pats[style].n, 0);
  }
  cairo_move_to (cr, 4, y);
  cairo_line_to (cr, w - 4, y);
  cairo_stroke (cr);
}

/* Preview drawer: a horizontal line with the chosen arrow at one end (item
 * position indexes arrow_choices). @at_start draws it on the left (the arrow
 * for a line's start handle, i.e. rotated 180°); otherwise on the right. */
static void
draw_arrow_preview (cairo_t *cr, int w, int h, int idx, gboolean at_start)
{
  ArrowType t = (idx >= 0 && idx < (int) G_N_ELEMENTS (arrow_choices))
                  ? arrow_choices[idx] : ARROW_NONE;
  double y = h / 2.0, hh = 4.0;
  double tip  = at_start ? 4.0     : w - 4.0;
  double base = at_start ? 12.0    : w - 12.0;

  cairo_set_source_rgb (cr, 0.1, 0.1, 0.1);
  cairo_set_line_width (cr, 1.5);
  cairo_move_to (cr, 4, y);
  cairo_line_to (cr, w - 4, y);
  cairo_stroke (cr);

  if (t == ARROW_LINES) {
    cairo_move_to (cr, tip, y);  cairo_line_to (cr, base, y - hh);
    cairo_move_to (cr, tip, y);  cairo_line_to (cr, base, y + hh);
    cairo_stroke (cr);
  } else if (t == ARROW_FILLED_TRIANGLE) {
    cairo_move_to (cr, tip, y);
    cairo_line_to (cr, base, y - hh);
    cairo_line_to (cr, base, y + hh);
    cairo_close_path (cr);
    cairo_fill (cr);
  }
}

static void
draw_start_arrow_item (GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer d)
{
  draw_arrow_preview (cr, w, h, GPOINTER_TO_INT (d), TRUE);
}

static void
draw_end_arrow_item (GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer d)
{
  draw_arrow_preview (cr, w, h, GPOINTER_TO_INT (d), FALSE);
}

static void
preview_setup (GtkSignalListItemFactory *f, GtkListItem *item, gpointer draw_func)
{
  GtkWidget *area = gtk_drawing_area_new ();

  gtk_widget_set_size_request (area, 28, 16);
  gtk_list_item_set_child (item, area);
}

static void
preview_bind (GtkSignalListItemFactory *f, GtkListItem *item, gpointer draw_func)
{
  GtkWidget *area = gtk_list_item_get_child (item);
  guint pos = gtk_list_item_get_position (item);

  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (area),
                                  (GtkDrawingAreaDrawFunc) draw_func,
                                  GUINT_TO_POINTER (pos), NULL);
  gtk_widget_queue_draw (area);
}

/* A GtkDropDown that shows a drawn preview per item (no text), single column.
 * @labels (NULL-terminated) populate the model for accessibility/size. */
static GtkWidget *
make_preview_dropdown (const char * const *labels, gpointer draw_func,
                       guint selected, const char *a11y,
                       GCallback notify_cb, DiaShell *self)
{
  GtkStringList *model = gtk_string_list_new (labels);
  GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
  GtkWidget *dd;

  g_signal_connect (factory, "setup", G_CALLBACK (preview_setup), draw_func);
  g_signal_connect (factory, "bind", G_CALLBACK (preview_bind), draw_func);

  dd = gtk_drop_down_new (G_LIST_MODEL (model), NULL);   /* takes model ref */
  gtk_drop_down_set_factory (GTK_DROP_DOWN (dd), factory);
  gtk_drop_down_set_selected (GTK_DROP_DOWN (dd), selected);
  g_object_unref (factory);

  set_a11y_label (dd, a11y);
  g_signal_connect (dd, "notify::selected", notify_cb, self);
  return dd;
}


/* --- shapes drawer (object sheets) -------------------------------------- */

/* The object sheets shown below the toolbox. The standard set is the palette;
 * these are the extra registered sets (flowchart/network/ER). */
static const struct {
  const char *sheet;
  const char *types[6];   /* NULL-terminated */
} sheets[] = {
  { N_("Flowchart"), { "Flowchart - Box", "Flowchart - Diamond",
                       "Flowchart - Ellipse", "Flowchart - Parallelogram", NULL } },
  { N_("Network"),   { "Network - Bus", "Network - Base Station",
                       "Network - Radio Cell", "Network - WAN Link", NULL } },
  { N_("ER"),        { "ER - Entity", "ER - Relationship",
                       "ER - Attribute", "ER - Participation", NULL } },
};

/* Clicking a shape makes it the active create tool (apply_tool_at falls back to
 * a registered type name when it isn't a palette tool). */
static void
on_sheet_shape_clicked (GtkButton *btn, DiaShell *self)
{
  const char *tn = g_object_get_data (G_OBJECT (btn), "type-name");
  char buf[96];

  if (!tn) {
    return;
  }
  g_strlcpy (self->tool, tn, sizeof (self->tool));
  g_snprintf (buf, sizeof (buf), _("Tool: %s"), tn);
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
}

/* Rebuild the shape buttons for the currently selected sheet. */
static void
rebuild_sheet_shapes (DiaShell *self)
{
  GtkFlowBox *fb = GTK_FLOW_BOX (self->sheet_box);
  const char * const *types;
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (fb))) != NULL) {
    gtk_flow_box_remove (fb, child);
  }
  if (self->sheet_index < 0 || self->sheet_index >= (int) G_N_ELEMENTS (sheets)) {
    return;
  }
  types = sheets[self->sheet_index].types;
  for (int i = 0; types[i]; i++) {
    DiaObjectType *t = object_get_type ((char *) types[i]);
    GtkWidget *btn = gtk_button_new ();
    GtkWidget *img;

    if (t && t->pixmap) {
      GdkPixbuf *pb = gdk_pixbuf_new_from_xpm_data (t->pixmap);
      img = gtk_image_new_from_pixbuf (pb);
      g_clear_object (&pb);
    } else {
      img = gtk_image_new_from_icon_name ("image-x-generic-symbolic");
    }
    gtk_image_set_pixel_size (GTK_IMAGE (img), 22);
    gtk_button_set_child (GTK_BUTTON (btn), img);
    gtk_button_set_has_frame (GTK_BUTTON (btn), FALSE);
    gtk_widget_set_tooltip_text (btn, types[i]);
    set_a11y_label (btn, types[i]);
    g_object_set_data_full (G_OBJECT (btn), "type-name",
                            g_strdup (types[i]), g_free);
    g_signal_connect (btn, "clicked",
                      G_CALLBACK (on_sheet_shape_clicked), self);
    gtk_flow_box_insert (fb, btn, -1);
  }
}

static void
on_sheet_changed (GtkDropDown *dd, GParamSpec *ps, DiaShell *self)
{
  self->sheet_index = gtk_drop_down_get_selected (dd);
  rebuild_sheet_shapes (self);
}

static GtkWidget *
build_sheets (DiaShell *self)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  const char *names[G_N_ELEMENTS (sheets) + 1];
  GtkWidget *dd;

  for (gsize i = 0; i < G_N_ELEMENTS (sheets); i++) {
    names[i] = gettext (sheets[i].sheet);
  }
  names[G_N_ELEMENTS (sheets)] = NULL;

  dd = gtk_drop_down_new_from_strings (names);
  gtk_drop_down_set_selected (GTK_DROP_DOWN (dd), 0);
  set_a11y_label (dd, "sheet");
  g_signal_connect (dd, "notify::selected", G_CALLBACK (on_sheet_changed), self);

  self->sheet_box = gtk_flow_box_new ();
  gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (self->sheet_box),
                                   GTK_SELECTION_NONE);
  gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (self->sheet_box), 4);
  self->sheet_index = 0;
  rebuild_sheet_shapes (self);

  gtk_box_append (GTK_BOX (box), dd);
  gtk_box_append (GTK_BOX (box), self->sheet_box);
  return box;
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
  /* Keep the toolbox slim: don't let the attribute widgets' hexpand propagate
   * up and make the sidebar steal half the window from the canvas. */
  gtk_widget_set_hexpand (box, FALSE);

  gtk_grid_set_row_homogeneous (GTK_GRID (grid), TRUE);
  gtk_grid_set_column_homogeneous (GTK_GRID (grid), TRUE);
  gtk_widget_set_halign (grid, GTK_ALIGN_CENTER);

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
    /* Four tools per row. */
    gtk_grid_attach (GTK_GRID (grid), btn, i % 4, i / 4, 1, 1);
  }
  gtk_box_append (GTK_BOX (box), grid);

  gtk_box_append (GTK_BOX (box),
                  gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));

  /* Colour area: a flat button (so AT-SPI exposes a click action and it is
   * keyboard-operable) wrapping a drawing area that paints the fg/bg swatches.
   * It shares a row with the line-width selector below. */
  colour = gtk_drawing_area_new ();
  self->colour_area = colour;
  gtk_widget_set_size_request (colour, 34, 34);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (colour),
                                  draw_color_area, self, NULL);

  colour_btn = gtk_button_new ();
  gtk_button_set_child (GTK_BUTTON (colour_btn), colour);
  gtk_button_set_has_frame (GTK_BUTTON (colour_btn), FALSE);
  set_a11y_label (colour_btn, "colour-area");
  gtk_widget_set_tooltip_text (colour_btn, _("Click to pick the foreground colour"));
  g_signal_connect (colour_btn, "clicked", G_CALLBACK (on_colour_clicked), self);

  /* Line attributes. Changing these restyles the current selection and becomes
   * the default for newly created objects. */
  gtk_box_append (GTK_BOX (box),
                  gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
  {
    /* Labels back the models for accessibility; the items render as drawn
     * previews (no text) via a custom factory. */
    static const char * const styles[] = { "Solid", "Dashed", "Dash-dot",
                                            "Dash-dot-dot", "Dotted", NULL };
    static const char * const arrows[] = { "None", "Lines", "Filled", NULL };
    GtkWidget *wrow = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *drow = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget *wl = gtk_label_new (_("Width"));
    GtkWidget *lw = gtk_spin_button_new_with_range (0.0, 5.0, 0.05);
    GtkWidget *ls = make_preview_dropdown (styles, draw_linestyle_item,
                                           self->line_style, "line-style",
                                           G_CALLBACK (on_lstyle_changed), self);
    GtkWidget *sa = make_preview_dropdown (arrows, draw_start_arrow_item, 0,
                                           "start-arrow",
                                           G_CALLBACK (on_start_arrow_changed),
                                           self);
    GtkWidget *ea = make_preview_dropdown (arrows, draw_end_arrow_item, 0,
                                           "end-arrow",
                                           G_CALLBACK (on_end_arrow_changed),
                                           self);

    gtk_spin_button_set_digits (GTK_SPIN_BUTTON (lw), 2);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (lw), self->line_width);
    gtk_editable_set_width_chars (GTK_EDITABLE (lw), 4);
    gtk_editable_set_max_width_chars (GTK_EDITABLE (lw), 5);
    set_a11y_label (lw, "line-width");
    g_signal_connect (lw, "value-changed", G_CALLBACK (on_lw_changed), self);

    /* Row 1: colour swatch + Width spin. */
    gtk_widget_set_halign (wl, GTK_ALIGN_START);
    gtk_widget_set_hexpand (lw, TRUE);
    gtk_box_append (GTK_BOX (wrow), colour_btn);
    gtk_box_append (GTK_BOX (wrow), wl);
    gtk_box_append (GTK_BOX (wrow), lw);
    gtk_box_append (GTK_BOX (box), wrow);

    /* Row 2: the three previews, equal width, spanning the tool-grid width. */
    gtk_box_set_homogeneous (GTK_BOX (drow), TRUE);
    gtk_box_append (GTK_BOX (drow), sa);
    gtk_box_append (GTK_BOX (drow), ls);
    gtk_box_append (GTK_BOX (drow), ea);
    gtk_box_append (GTK_BOX (box), drow);
  }

  /* Shapes drawer: pick a sheet (flowchart/network/ER) and click a shape to
   * make it the active create tool. */
  gtk_box_append (GTK_BOX (box),
                  gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
  gtk_box_append (GTK_BOX (box), build_sheets (self));

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
  GtkEventController *keys;
  GtkEventController *scroll;
  GtkGesture *click;
  GtkGesture *drag;

  self->canvas = canvas;
  self->hscroll = hscroll;
  self->vscroll = vscroll;

  gtk_button_set_has_frame (GTK_BUTTON (origin), FALSE);

  self->hruler = hruler;
  self->vruler = vruler;

  gtk_widget_set_size_request (hruler, -1, 26);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (hruler),
                                  draw_ruler, self, NULL);

  gtk_widget_set_size_request (vruler, 26, -1);
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

  keys = gtk_event_controller_key_new ();
  g_signal_connect (keys, "key-pressed", G_CALLBACK (on_canvas_key), self);
  gtk_widget_add_controller (canvas, keys);

  scroll = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect (scroll, "scroll", G_CALLBACK (on_canvas_scroll), self);
  gtk_widget_add_controller (canvas, scroll);

  /* Scrollbars drive the viewport origin; the canvas resize refreshes them.
   * (GTK4: GtkScrollbar is a GtkWidget, not a GtkRange.) */
  g_signal_connect (gtk_scrollbar_get_adjustment (GTK_SCROLLBAR (hscroll)),
                    "value-changed", G_CALLBACK (on_hadj_changed), self);
  g_signal_connect (gtk_scrollbar_get_adjustment (GTK_SCROLLBAR (vscroll)),
                    "value-changed", G_CALLBACK (on_vadj_changed), self);
  g_signal_connect (canvas, "resize", G_CALLBACK (on_canvas_resize), self);

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


/* Commit an inline layer rename when its GtkEditableLabel stops editing. */
static void
on_layer_label_editing (GObject *labelobj, GParamSpec *ps, DiaShell *self)
{
  GtkEditableLabel *el = GTK_EDITABLE_LABEL (labelobj);
  int idx;
  DiaLayer *l;

  if (gtk_editable_label_get_editing (el)) {
    return;   /* only act when editing finishes */
  }
  idx = GPOINTER_TO_INT (g_object_get_data (labelobj, "dia-layer-index"));
  l = data_layer_get_nth (self->diagram, idx);
  if (l) {
    g_object_set (l, "name", gtk_editable_get_text (GTK_EDITABLE (el)), NULL);
  }
}

/* Rebuild the layers list (topmost first, the way Dia shows them). The single
 * selection highlight tracks the active layer; rows are GtkEditableLabels so a
 * double-click renames them. */
static void
refresh_layers_list (DiaShell *self)
{
  GtkListBox *lb = GTK_LIST_BOX (self->layers_list);
  DiaLayer *active = dia_diagram_data_get_active_layer (self->diagram);
  GtkWidget *child;
  int n = data_layer_count (self->diagram);
  int active_idx = active ? data_layer_get_index (self->diagram, active) : -1;

  self->scroll_guard++;   /* programmatic select shouldn't recurse */
  while ((child = gtk_widget_get_first_child (GTK_WIDGET (lb))) != NULL) {
    gtk_list_box_remove (lb, child);
  }
  /* Index order matches upstream: layer index 0 at the top, and data_raise_layer
   * moves a layer toward index 0 (up the list), data_lower_layer toward n-1. */
  for (int i = 0; i < n; i++) {
    DiaLayer *l = data_layer_get_nth (self->diagram, i);
    GtkWidget *row = gtk_editable_label_new (dia_layer_get_name (l));

    gtk_widget_set_halign (row, GTK_ALIGN_START);
    g_object_set_data (G_OBJECT (row), "dia-layer-index", GINT_TO_POINTER (i));
    g_signal_connect (row, "notify::editing",
                      G_CALLBACK (on_layer_label_editing), self);
    gtk_list_box_insert (lb, row, -1);
  }
  if (active_idx >= 0) {
    GtkListBoxRow *r = gtk_list_box_get_row_at_index (lb, active_idx);
    if (r) {
      gtk_list_box_select_row (lb, r);
    }
  }
  self->scroll_guard--;
}

static void
on_layer_row_selected (GtkListBox *lb, GtkListBoxRow *row, DiaShell *self)
{
  GtkWidget *child;
  DiaLayer *l;
  int idx;

  if (!row || self->scroll_guard) {
    return;   /* ignore our own programmatic re-selection during refresh */
  }
  child = gtk_list_box_row_get_child (row);
  idx = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (child), "dia-layer-index"));
  l = data_layer_get_nth (self->diagram, idx);
  if (l) {
    data_set_active_layer (self->diagram, l);
  }
}

/* Double-click / Enter on a row starts an inline rename. */
static void
on_layer_row_activated (GtkListBox *lb, GtkListBoxRow *row, DiaShell *self)
{
  GtkWidget *child;

  if (!row) {
    return;
  }
  child = gtk_list_box_row_get_child (row);
  if (GTK_IS_EDITABLE_LABEL (child)) {
    gtk_editable_label_start_editing (GTK_EDITABLE_LABEL (child));
  }
}

static void
on_layer_add (GtkButton *b, DiaShell *self)
{
  char *name = g_strdup_printf (_("Layer %d"),
                                data_layer_count (self->diagram) + 1);
  DiaLayer *l = dia_layer_new (name, self->diagram);

  g_free (name);
  data_add_layer (self->diagram, l);          /* refs the layer itself */
  data_set_active_layer (self->diagram, l);
  g_object_unref (l);                          /* drop our construction ref */
  refresh_layers_list (self);
  gtk_label_set_text (GTK_LABEL (self->status_msg),
                      _("Added a layer (active)"));
  gtk_widget_queue_draw (self->canvas);
}

static void
on_layer_remove (GtkButton *b, DiaShell *self)
{
  DiaLayer *active = dia_diagram_data_get_active_layer (self->diagram);

  if (data_layer_count (self->diagram) <= 1) {
    gtk_label_set_text (GTK_LABEL (self->status_msg),
                        _("Cannot remove the last layer"));
    return;
  }
  self->selected = NULL;                       /* may have lived in this layer */
  data_remove_layer (self->diagram, active);
  data_set_active_layer (self->diagram,
                         data_layer_get_nth (self->diagram,
                                             data_layer_count (self->diagram) - 1));
  refresh_layers_list (self);
  gtk_label_set_text (GTK_LABEL (self->status_msg), _("Removed a layer"));
  gtk_widget_queue_draw (self->canvas);
}

static void
on_layer_up (GtkButton *b, DiaShell *self)
{
  data_raise_layer (self->diagram,
                    dia_diagram_data_get_active_layer (self->diagram));
  refresh_layers_list (self);
  gtk_widget_queue_draw (self->canvas);
}

static void
on_layer_down (GtkButton *b, DiaShell *self)
{
  data_lower_layer (self->diagram,
                    dia_diagram_data_get_active_layer (self->diagram));
  refresh_layers_list (self);
  gtk_widget_queue_draw (self->canvas);
}

static GtkWidget *
build_layers (DiaShell *self)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  GtkWidget *label = gtk_label_new (_("Layers"));
  GtkWidget *list = gtk_list_box_new ();
  GtkWidget *controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  struct { const char *icon; const char *tip; GCallback cb; } btns[] = {
    { "list-add-symbolic",    N_("Add layer"),    G_CALLBACK (on_layer_add) },
    { "list-remove-symbolic", N_("Remove layer"), G_CALLBACK (on_layer_remove) },
    { "go-up-symbolic",       N_("Raise layer"),  G_CALLBACK (on_layer_up) },
    { "go-down-symbolic",     N_("Lower layer"),  G_CALLBACK (on_layer_down) },
  };

  gtk_widget_set_size_request (box, 150, -1);
  gtk_widget_set_margin_start (box, 4);
  gtk_widget_set_margin_end (box, 4);
  gtk_widget_set_margin_top (box, 4);

  gtk_widget_add_css_class (label, "heading");
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (box), label);

  self->layers_list = list;
  gtk_widget_set_vexpand (list, TRUE);
  g_signal_connect (list, "row-selected",
                    G_CALLBACK (on_layer_row_selected), self);
  g_signal_connect (list, "row-activated",
                    G_CALLBACK (on_layer_row_activated), self);
  refresh_layers_list (self);
  gtk_box_append (GTK_BOX (box), list);

  gtk_widget_add_css_class (controls, "toolbar");
  for (gsize i = 0; i < G_N_ELEMENTS (btns); i++) {
    GtkWidget *b = gtk_button_new_from_icon_name (btns[i].icon);
    gtk_button_set_has_frame (GTK_BUTTON (b), FALSE);
    gtk_widget_set_tooltip_text (b, gettext (btns[i].tip));
    set_a11y_label (b, gettext (btns[i].tip));
    g_signal_connect (b, "clicked", btns[i].cb, self);
    gtk_box_append (GTK_BOX (controls), b);
  }
  gtk_box_append (GTK_BOX (box), controls);

  return box;
}


/* Editable zoom field: typing a percentage and pressing Enter sets the zoom. */
static void
on_zoom_entry_activate (GtkEntry *entry, DiaShell *self)
{
  const char *text = gtk_editable_get_text (GTK_EDITABLE (entry));
  double pct = g_strtod (text, NULL);   /* parses the number, ignores a '%' */

  if (pct >= 1.0 && pct <= 2000.0) {
    update_zoom (self, (pct / 100.0) / self->zoom);   /* set absolute zoom */
  } else {
    char buf[32];
    g_snprintf (buf, sizeof (buf), "%.0f%%", self->zoom * 100.0);
    gtk_editable_set_text (GTK_EDITABLE (entry), buf);
  }
}

static GtkWidget *
build_statusbar (DiaShell *self)
{
  GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);

  self->status_msg = gtk_label_new ("");
  self->pos_label = gtk_label_new ("0, 0");
  /* Editable zoom readout (type a percentage + Enter). */
  self->zoom_label = gtk_entry_new ();
  gtk_editable_set_text (GTK_EDITABLE (self->zoom_label), _("100%"));
  gtk_editable_set_width_chars (GTK_EDITABLE (self->zoom_label), 5);
  gtk_entry_set_alignment (GTK_ENTRY (self->zoom_label), 1.0);
  gtk_widget_set_tooltip_text (self->zoom_label, _("Zoom — type a percentage"));
  set_a11y_label (self->zoom_label, "zoom");
  g_signal_connect (self->zoom_label, "activate",
                    G_CALLBACK (on_zoom_entry_activate), self);

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
  g_clear_pointer (&self->drag_moves, g_array_unref);
  dia_clear_object (&self->clipboard);
  g_clear_object (&self->diagram);
  g_free (self);
}


/* Header toggle: show/hide the right-hand layers panel to free canvas space. */
static void
on_toggle_layers (GtkToggleButton *b, DiaShell *self)
{
  if (self->layers_panel) {
    gtk_widget_set_visible (self->layers_panel,
                            gtk_toggle_button_get_active (b));
  }
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
  /* Default page: A4 portrait (cm); a small margin puts it near the top-left of
   * the zoomed-out workspace. */
  self->page_w_cm = 21.0;
  self->page_h_cm = 29.7;
  self->page_infinite = FALSE;
  self->origin_x = -2.0;
  self->origin_y = -2.0;
  self->snap_grid = TRUE;     /* on by default, like upstream Dia */
  self->line_width = 0.10;
  self->line_style = DIA_LINE_STYLE_SOLID;
  self->start_arrow = ARROW_NONE;
  self->end_arrow = ARROW_NONE;
  update_transform (self);

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
  {
    GtkWidget *layers_toggle = gtk_toggle_button_new ();
    gtk_button_set_icon_name (GTK_BUTTON (layers_toggle), "view-list-symbolic");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (layers_toggle), TRUE);
    gtk_widget_set_tooltip_text (layers_toggle, _("Show or hide the layers panel"));
    set_a11y_label (layers_toggle, "toggle-layers");
    g_signal_connect (layers_toggle, "toggled",
                      G_CALLBACK (on_toggle_layers), self);
    adw_header_bar_pack_end (ADW_HEADER_BAR (header), layers_toggle);
  }

  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (view), header);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (view), build_action_toolbar (self));

  gtk_box_append (GTK_BOX (main_area), build_toolbox (self));
  gtk_box_append (GTK_BOX (main_area),
                  gtk_separator_new (GTK_ORIENTATION_VERTICAL));
  gtk_box_append (GTK_BOX (main_area), build_canvas_area (self));
  /* Layers panel + its divider in one box so the header toggle hides both. */
  {
    GtkWidget *layers_wrap = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append (GTK_BOX (layers_wrap),
                    gtk_separator_new (GTK_ORIENTATION_VERTICAL));
    gtk_box_append (GTK_BOX (layers_wrap), build_layers (self));
    self->layers_panel = layers_wrap;
    gtk_box_append (GTK_BOX (main_area), layers_wrap);
  }
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (view), main_area);

  adw_toolbar_view_add_bottom_bar (ADW_TOOLBAR_VIEW (view), statusbar);

  /* tie the shared state lifetime to the view */
  g_object_set_data_full (G_OBJECT (view), "dia-shell", self, dia_shell_free);

  return view;
}
