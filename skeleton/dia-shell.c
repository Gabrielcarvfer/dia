/* Dia -- GTK4 + libadwaita port
 *
 * dia-shell: the integrated-UI layout rebuilt with GTK4 widgets.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <glib/gi18n.h>

#include "dia-shell.h"

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


/* --- drawing callbacks (placeholder visuals) ----------------------------- */

static void
draw_canvas (GtkDrawingArea *area,
             cairo_t        *cr,
             int             width,
             int             height,
             gpointer        user_data)
{
  double page_w, page_h, px, py;

  /* desk background */
  cairo_set_source_rgb (cr, 0.6, 0.6, 0.62);
  cairo_paint (cr);

  /* a centred "page" */
  page_w = width * 0.7;
  page_h = height * 0.82;
  px = (width - page_w) / 2.0;
  py = (height - page_h) / 2.0;

  cairo_rectangle (cr, px + 3, py + 3, page_w, page_h);
  cairo_set_source_rgba (cr, 0, 0, 0, 0.25);   /* drop shadow */
  cairo_fill (cr);

  cairo_rectangle (cr, px, py, page_w, page_h);
  cairo_set_source_rgb (cr, 1, 1, 1);          /* paper */
  cairo_fill_preserve (cr);
  cairo_set_source_rgb (cr, 0.4, 0.4, 0.4);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);
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
  double s = MIN (width, height) * 0.62;

  /* background colour swatch (bottom-right) */
  cairo_rectangle (cr, width - s, height - s, s, s);
  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_fill_preserve (cr);
  cairo_set_source_rgb (cr, 0.3, 0.3, 0.3);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);

  /* foreground colour swatch (top-left) */
  cairo_rectangle (cr, 0, 0, s, s);
  cairo_set_source_rgb (cr, 0, 0, 0);
  cairo_fill_preserve (cr);
  cairo_set_source_rgb (cr, 0.3, 0.3, 0.3);
  cairo_stroke (cr);
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


/* The action toolbar (New/Open/Save/Undo/Redo/Zoom). */
static GtkWidget *
build_action_toolbar (void)
{
  GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  struct { const char *icon; const char *tip; } items[] = {
    { "document-new-symbolic",   N_("New diagram") },
    { "document-open-symbolic",  N_("Open") },
    { "document-save-symbolic",  N_("Save") },
    { NULL, NULL },
    { "edit-undo-symbolic",      N_("Undo") },
    { "edit-redo-symbolic",      N_("Redo") },
    { NULL, NULL },
    { "zoom-in-symbolic",        N_("Zoom in") },
    { "zoom-out-symbolic",       N_("Zoom out") },
    { "zoom-original-symbolic",  N_("Zoom 1:1") },
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
    gtk_box_append (GTK_BOX (bar), b);
  }

  return bar;
}


/* The left toolbox: tool grid + colour area. */
static GtkWidget *
build_toolbox (void)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  GtkWidget *grid = gtk_grid_new ();
  GtkWidget *colour;
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
    } else {
      gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (btn), first);
    }
    gtk_grid_attach (GTK_GRID (grid), btn, i % 2, i / 2, 1, 1);
  }
  gtk_box_append (GTK_BOX (box), grid);

  gtk_box_append (GTK_BOX (box),
                  gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));

  /* foreground/background colour area */
  colour = gtk_drawing_area_new ();
  gtk_widget_set_size_request (colour, 56, 56);
  gtk_widget_set_halign (colour, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text (colour, _("Foreground & background colour"));
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (colour),
                                  draw_color_area, NULL, NULL);
  gtk_box_append (GTK_BOX (box), colour);

  return box;
}


/* The diagram display: rulers, canvas, scrollbars, in a notebook tab. */
static GtkWidget *
build_canvas_area (void)
{
  GtkWidget *notebook = gtk_notebook_new ();
  GtkWidget *grid = gtk_grid_new ();
  GtkWidget *origin = gtk_button_new ();
  GtkWidget *hruler = gtk_drawing_area_new ();
  GtkWidget *vruler = gtk_drawing_area_new ();
  GtkWidget *canvas = gtk_drawing_area_new ();
  GtkWidget *vscroll = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, NULL);
  GtkWidget *hscroll = gtk_scrollbar_new (GTK_ORIENTATION_HORIZONTAL, NULL);

  gtk_button_set_has_frame (GTK_BUTTON (origin), FALSE);

  gtk_widget_set_size_request (hruler, -1, 20);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (hruler),
                                  draw_ruler, GINT_TO_POINTER (TRUE), NULL);

  gtk_widget_set_size_request (vruler, 20, -1);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (vruler),
                                  draw_ruler, GINT_TO_POINTER (FALSE), NULL);

  gtk_widget_set_hexpand (canvas, TRUE);
  gtk_widget_set_vexpand (canvas, TRUE);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (canvas),
                                  draw_canvas, NULL, NULL);

  /*   (0,0) origin   (1,0) hruler
   *   (0,1) vruler   (1,1) canvas    (2,1) vscroll
   *                  (1,2) hscroll                    */
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


/* The right-hand layer list. */
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


/* The bottom statusbar: message + zoom + position. */
static GtkWidget *
build_statusbar (void)
{
  GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *msg = gtk_label_new ("");
  GtkWidget *zoom = gtk_label_new (_("100%"));
  GtkWidget *pos = gtk_label_new ("0, 0");

  gtk_widget_add_css_class (bar, "toolbar");
  gtk_widget_set_margin_start (bar, 6);
  gtk_widget_set_margin_end (bar, 6);

  gtk_widget_set_hexpand (msg, TRUE);
  gtk_widget_set_halign (msg, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (bar), msg);
  gtk_box_append (GTK_BOX (bar), pos);
  gtk_box_append (GTK_BOX (bar), gtk_separator_new (GTK_ORIENTATION_VERTICAL));
  gtk_box_append (GTK_BOX (bar), zoom);

  return bar;
}


GtkWidget *
dia_shell_new (void)
{
  GtkWidget *view = adw_toolbar_view_new ();
  GtkWidget *header = adw_header_bar_new ();
  GtkWidget *main_area = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *title = adw_window_title_new ("Dia", _("Diagram1"));

  adw_header_bar_set_title_widget (ADW_HEADER_BAR (header), title);
  adw_header_bar_pack_end (ADW_HEADER_BAR (header), build_primary_menu_button ());

  /* top bars: header + action toolbar */
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (view), header);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (view), build_action_toolbar ());

  /* main area: [toolbox | canvas | layers] */
  gtk_box_append (GTK_BOX (main_area), build_toolbox ());
  gtk_box_append (GTK_BOX (main_area),
                  gtk_separator_new (GTK_ORIENTATION_VERTICAL));
  gtk_box_append (GTK_BOX (main_area), build_canvas_area ());
  gtk_box_append (GTK_BOX (main_area),
                  gtk_separator_new (GTK_ORIENTATION_VERTICAL));
  gtk_box_append (GTK_BOX (main_area), build_layers ());
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (view), main_area);

  /* bottom bar: statusbar */
  adw_toolbar_view_add_bottom_bar (ADW_TOOLBAR_VIEW (view), build_statusbar ());

  return view;
}
