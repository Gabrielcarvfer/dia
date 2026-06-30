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

/* Shared shell state so the event handlers can reach the widgets they update.
 * Owns nothing but itself (widget pointers are borrowed); freed with the
 * window via g_object_set_data_full(). */
typedef struct {
  GtkWidget *canvas;
  GtkWidget *colour_area;
  GtkWidget *status_msg;
  GtkWidget *pos_label;
  GtkWidget *zoom_label;
  double     zoom;        /* 1.0 == 100% */
  GdkRGBA    fg;
  GdkRGBA    bg;
  char       tool[64];
} DiaShell;


/* The standard tool palette (mirrors app/toolbox.c tool_data). */
typedef struct {
  const char *name;
  const char *tooltip;
} ToolEntry;

static const ToolEntry tool_entries[] = {
  { N_("Modify"),     N_("Modify object(s)") },
  { N_("Text edit"),  N_("Edit text (F2)") },
  { N_("Magnify"),    N_("Magnify (M)") },
  { N_("Scroll"),     N_("Scroll around the diagram (S)") },
  { N_("Text"),       N_("Create a text object (T)") },
  { N_("Box"),        N_("Create a box (R)") },
  { N_("Ellipse"),    N_("Create an ellipse (E)") },
  { N_("Polygon"),    N_("Create a polygon (P)") },
  { N_("Beziergon"),  N_("Create a beziergon (B)") },
  { N_("Line"),       N_("Create a line (L)") },
  { N_("Arc"),        N_("Create an arc (A)") },
  { N_("Zigzag"),     N_("Create a zigzag line (Z)") },
  { N_("Polyline"),   N_("Create a polyline") },
  { N_("Bezier"),     N_("Create a bezier line (C)") },
  { N_("Image"),      N_("Insert an image (I)") },
  { N_("Outline"),    N_("Create an outline") },
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

/* Draw a small sample diagram onto the page using the REAL Dia renderer, so
 * the canvas exercises the ported libdia rendering pipeline (not placeholder
 * cairo). The page maps a 20cm-wide virtual diagram to the drawn page area. */
static void
draw_sample_diagram (cairo_t *cr, double scale)
{
  DiaRenderer *renderer;
  DiaCairoRenderer *cairo_renderer;
  DiaFont *font;
  Color black = { 0.0, 0.0, 0.0, 1.0 };
  Color box_a = { 0.80, 0.89, 1.0, 1.0 };
  Color box_b = { 0.85, 1.0, 0.85, 1.0 };
  Point a_ul = { 2.0, 2.0 },  a_lr = { 7.0, 5.0 };
  Point b_ul = { 12.0, 8.0 }, b_lr = { 17.0, 11.0 };
  Point l_from = { 7.0, 3.5 }, l_to = { 12.0, 9.5 };
  Point label = { 2.0, 14.0 };

  renderer = g_object_new (DIA_CAIRO_TYPE_RENDERER, NULL);
  cairo_renderer = DIA_CAIRO_RENDERER (renderer);
  cairo_renderer->cr = cairo_reference (cr);
  cairo_renderer->with_alpha = TRUE;

  dia_renderer_begin_render (renderer, NULL);
  dia_renderer_set_linewidth (renderer, 0.05);

  dia_renderer_draw_rect (renderer, &a_ul, &a_lr, &box_a, &black);
  dia_renderer_draw_rect (renderer, &b_ul, &b_lr, &box_b, &black);
  dia_renderer_draw_line (renderer, &l_from, &l_to, &black);

  font = dia_font_new_from_style (DIA_FONT_SANS, 0.8);
  dia_renderer_set_font (renderer, font, 0.8);
  dia_renderer_draw_string (renderer, "Dia \xc2\xb7 GTK4 port",
                            &label, DIA_ALIGN_LEFT, &black);
  g_clear_object (&font);

  dia_renderer_end_render (renderer);
  g_object_unref (renderer);
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

  /* Render the sample diagram in page coordinates (cm), clipped to the page. */
  cairo_save (cr);
  cairo_rectangle (cr, px, py, page_w, page_h);
  cairo_clip (cr);
  cairo_translate (cr, px, py);
  cairo_scale (cr, page_w / 20.0, page_w / 20.0);
  draw_sample_diagram (cr, page_w / 20.0);
  cairo_restore (cr);
}


static void
draw_ruler (GtkDrawingArea *area,
            cairo_t        *cr,
            int             width,
            int             height,
            gpointer        user_data)
{
  gboolean horizontal = GPOINTER_TO_INT (user_data);
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

  name = gtk_button_get_label (GTK_BUTTON (button));
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

  /* report "diagram" coordinates: pixels divided by the zoom factor */
  g_snprintf (buf, sizeof (buf), "%.0f, %.0f", x / self->zoom, y / self->zoom);
  gtk_label_set_text (GTK_LABEL (self->pos_label), buf);
}


static void
on_canvas_pressed (GtkGestureClick *gesture,
                   int              n_press,
                   double           x,
                   double           y,
                   DiaShell        *self)
{
  char buf[128];

  g_snprintf (buf, sizeof (buf), _("%s at (%.0f, %.0f)"),
              self->tool[0] ? self->tool : _("Click"),
              x / self->zoom, y / self->zoom);
  gtk_label_set_text (GTK_LABEL (self->status_msg), buf);
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

static void on_zoom_in  (GtkButton *b, DiaShell *s) { update_zoom (s, 1.5); }
static void on_zoom_out (GtkButton *b, DiaShell *s) { update_zoom (s, 1.0 / 1.5); }

static void
on_zoom_reset (GtkButton *b, DiaShell *self)
{
  self->zoom = 1.0;
  update_zoom (self, 1.0);
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

  g_menu_append (menu, _("_About Dia"), "app.about");
  g_menu_append (menu, _("_Quit"), "app.quit");

  gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (button), "open-menu-symbolic");
  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (button), G_MENU_MODEL (menu));
  g_object_unref (menu);

  return button;
}


static GtkWidget *
build_action_toolbar (DiaShell *self)
{
  GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  struct { const char *icon; const char *tip; GCallback cb; } items[] = {
    { "document-new-symbolic",   N_("New diagram"), NULL },
    { "document-open-symbolic",  N_("Open"),        NULL },
    { "document-save-symbolic",  N_("Save"),        NULL },
    { NULL, NULL, NULL },
    { "edit-undo-symbolic",      N_("Undo"),        NULL },
    { "edit-redo-symbolic",      N_("Redo"),        NULL },
    { NULL, NULL, NULL },
    { "zoom-in-symbolic",        N_("Zoom in"),     G_CALLBACK (on_zoom_in) },
    { "zoom-out-symbolic",       N_("Zoom out"),    G_CALLBACK (on_zoom_out) },
    { "zoom-original-symbolic",  N_("Zoom 1:1"),    G_CALLBACK (on_zoom_reset) },
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
    if (items[i].cb)
      g_signal_connect (b, "clicked", items[i].cb, self);
    gtk_box_append (GTK_BOX (bar), b);
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
    GtkWidget *btn = gtk_toggle_button_new_with_label (gettext (tool_entries[i].name));

    gtk_widget_set_tooltip_text (btn, gettext (tool_entries[i].tooltip));
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

  self->canvas = canvas;

  gtk_button_set_has_frame (GTK_BUTTON (origin), FALSE);

  gtk_widget_set_size_request (hruler, -1, 20);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (hruler),
                                  draw_ruler, GINT_TO_POINTER (TRUE), NULL);

  gtk_widget_set_size_request (vruler, 20, -1);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (vruler),
                                  draw_ruler, GINT_TO_POINTER (FALSE), NULL);

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
  g_object_set_data_full (G_OBJECT (view), "dia-shell", self, g_free);

  return view;
}
