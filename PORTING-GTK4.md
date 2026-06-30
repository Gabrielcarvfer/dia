# Porting Dia to GTK4 + libadwaita

This branch starts the migration of Dia from **GTK3** to **GTK4 + libadwaita**,
as a hard switch (no GTK3/GTK4 `#ifdef` compatibility shims).

## Current state

GTK4 ports are all-or-nothing for compilation — the headers change, so the
whole tree must migrate before anything links. To avoid leaving the tree
unbuildable for the duration, the build has **two modes**:

| Mode | meson option | What builds | Toolkit |
|------|--------------|-------------|---------|
| **Port skeleton** (default) | *(none)* | `skeleton/` | GTK4 + libadwaita |
| Legacy reference | `-Dlegacy_gtk3=true` | `lib/ objects/ app/ plug-ins/ …` | GTK3 |

The default build produces a small, self-contained `AdwApplication`
(`skeleton/main.c`): an `AdwApplicationWindow` with an `AdwToolbarView` +
`AdwHeaderBar`, a primary menu (About/Quit wired through `GAction`), and an
`AdwStatusPage` placeholder. It compiles and launches today, and is the shell
the ported widgets get wired into.

The original GTK3 code is untouched and kept in-tree as the porting reference;
build it with `-Dlegacy_gtk3=true` (needs GTK3 + `libxml >= 2.14`).

### Progress so far

A growing GTK4 core library, **`lib-port/`** (builds `libdia-core`), recompiles
`lib/` against GTK4 file by file. The source stays in `lib/` (so the legacy
build is unaffected); `lib-port/meson.build` lists the files verified to
compile under GTK4. Grow that list as porting continues.

**71 of 100 `lib/*.c` files compile against GTK4** — the entire object/model
layer. The vast majority needed *no code changes*: GTK4 still ships `gtk.h`,
`GtkWidget`, `GdkPixbuf`, `GdkRGBA`, `GtkTreeView`/`GtkCellRenderer`, etc., so
headers that merely declare such types port as-is. Only files that *use removed
GTK3 APIs* need real work. `font.c` is the first such hand-port done
(`gtk_get_default_language()` → `pango_language_get_default()`,
`gdk_pango_context_get()` → a PangoCairo measure context).

The ~29 remaining `lib/` files are the genuinely GTK-coupled widgets/dialogs
(see the ranked list at the bottom).

## Building

```sh
# Toolchain (Debian/Ubuntu):
sudo apt-get install meson ninja-build pkgconf libgtk-4-dev libadwaita-1-dev \
                     libglib2.0-dev

# Default = GTK4/libadwaita skeleton:
meson setup build
meson compile -C build
./build/skeleton/dia

# Legacy GTK3 tree (reference):
meson setup build-gtk3 -Dlegacy_gtk3=true
```

## Migration roadmap

Rough order; each step should keep the **default** (skeleton) build green and,
where possible, move ported code from the legacy subdirs into the skeleton's
link set.

1. **Core model (`lib/`) — non-GTK first.** Object model, properties, geometry,
   and the `DiaRenderer` hierarchy are mostly GLib/cairo and port cheaply.
   Detach them from `gtk.h` where they don't truly need it.
   **Status: largely done** — 71/100 `lib/*.c` compile via `lib-port/`.
2. **Canvas drawing model.** `app/dia-canvas.c` is a `GtkDrawingArea`. GTK4:
   - `configure-event` / `GdkEventConfigure` → `GtkDrawingArea::resize` and
     `gtk_widget_get_width/height()`.
   - draw signal → `gtk_drawing_area_set_draw_func()` (cairo) or `snapshot`.
   - input: `GdkEventButton`/`GdkEventKey` field access → `GtkEventController`
     (`GtkGestureClick`, `GtkEventControllerScroll`, `GtkEventControllerKey`,
     `GtkEventControllerMotion`).
3. **Containers & layout.** `gtk_container_add` → `gtk_box_append` /
   widget-specific setters; `gtk_box_pack_start` → `gtk_box_append`;
   `gtk_widget_show_all` → drop (widgets visible by default).
4. **Menus & toolbars.** `GtkMenu`/`GtkMenuItem`/`GtkToolbar` → `GMenu` +
   `GtkPopoverMenu` / `AdwHeaderBar` + `GtkActionBar`; migrate accelerators to
   `gtk_application_set_accels_for_action`.
5. **Dialogs.** `gtk_dialog_run` (removed) → async `AdwDialog` /
   `AdwMessageDialog` / `GtkFileDialog` with response callbacks.
6. **Misc removed APIs.** `gtk_misc_*`, `gtk_widget_set_events`,
   `gtk_clipboard` → `GdkClipboard`, stock items → icon names.
7. **App entry.** Fold `app/main.c` + `app/app_procs.c` (`gtk_main`) into the
   `AdwApplication` in `skeleton/main.c`.
8. **Plug-ins / objects** that touch GTK (property dialogs, etc.) last.

### Remaining `lib/` files, ranked by removed-API count

These are the GTK-coupled files not yet in `lib-port`. The number is how many
*removed-in-GTK4* API references each contains (a rough difficulty proxy):

```
 0  object.c*                (* done — was only blocked by xpm-pixbuf subproject)
 1  dia-colour-selector.c, dia-line-cell-renderer.c, dia_image.c,
    diainteractiverenderer.c (GdkWindow -> GdkSurface)
 2  dia-arrow-preview.c, dia-line-preview.c, dia-unit-spinner.c, prop_dict.c
 3  dia-arrow-cell-renderer.c, prop_inttypes.c
 4  dia-font-selector.c, prop_pixbuf.c
 5  prop_text.c
 6  prop_matrix.c
 8  dia-arrow-selector.c, dia-line-style-selector.c
11  diapatternselector.c
12  dia-arrow-chooser.c, dia-line-chooser.c, widgets.c
13  dia-size-selector.c, prop_widgets.c
14  prop_sdarray_widget.c, propdialogs.c
18  persistence.c
19  message.c
   (dia-io.c is libxml-version-blocked, not GTK: needs XML_SAVE_EMPTY / libxml 2.14)
```

Recurring porting patterns for these:
- `GtkCellRenderer::render` (cairo) → `GtkCellRenderer::snapshot` (the colour/
  line/arrow cell renderers).
- Subclassing `GtkSpinButton`/etc. → those `*Class` structs are private in
  GTK4; `dia-unit-spinner` must be re-based or wrapped (unblocks
  `prop_geomtypes.c`).
- `GdkWindow` → `GdkSurface` (`diainteractiverenderer.c`).
- `gtk_dialog_run`/`gtk_container_add`/`gtk_box_pack_start`/`gtk_widget_show_all`
  → async dialogs, `gtk_box_append`, visible-by-default (`message.c`,
  `propdialogs.c`, the selectors/choosers).
- Window geometry (`gtk_window_get_size`/`get_position`/`resize`) and
  `gtk_entry_get_text`/`set_text` → `GtkEditable` + surface APIs
  (`persistence.c`).

### App-layer hard spots (from an API survey of `app/`)

- `gtk_box_pack_start` — 35 files
- `gtk_container_add` — 32 files
- `gtk_widget_destroy` — 27 files
- `GdkEventButton` direct field access — 21 files
- `gtk_widget_show_all` — 13 files
- `gtk_dialog_run` — 5 files
- 42 custom `GtkWidget` subclasses to revisit (drawing/input model)
