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

**99 of 100 `lib/*.c` files compile against GTK4** — the entire model + widget
layer. The only holdout is `dia-io.c`, blocked by libxml < 2.14 (not a GTK
issue). The whole object/model layer needed *no code changes*: GTK4 still ships
`gtk.h`, `GtkWidget`,
`GdkPixbuf`, `GdkRGBA`, `GtkTreeView`/`GtkCellRenderer`, etc., so headers that
merely declare such types port as-is. The rest were hand-ported widgets. Real
migrations done so far:

- `font.c` — `gtk_get_default_language()` → `pango_language_get_default()`;
  `gdk_pango_context_get()` → a PangoCairo measure context.
- Cell renderers (`dia-{line,arrow,colour}-cell-renderer.c`) — GTK3 `render`
  (cairo) / `get_size` vfuncs → `snapshot` (via `gtk_snapshot_append_cairo`)
  and `get_preferred_width`/`_height`; dropped the state-flags arg from
  `gtk_style_context_get_color`; `get_background_color` is gone (approximated).
- `dia-unit-spinner.c` — `GtkSpinButton` is final in GTK4, so `DiaUnitSpinner`
  is re-based as a `GtkWidget` composing a `GtkSpinButton` (public API intact).
- Preview widgets (`dia-line-preview.c`, `dia-arrow-preview.c`) — `GtkWidget`
  `draw` vfunc → `snapshot`; dropped `get_allocation`/`set_has_window`/
  `is_drawable`.
- `prop_inttypes.c` — `gtk_entry_get/set_text` → `GtkEditable`.
- `prop_matrix.c` — `gtk_box_pack_start` → `gtk_box_append` (+ `set_hexpand`);
  `gtk_container_get_children` → `gtk_widget_get_first_child`/`get_next_sibling`.
- `prop_dict.c` — `gtk_scrolled_window_new()` arg drop / `set_child`; removed
  `set_shadow_type` and `gtk_widget_show_all`.
- `dia-font-selector.c` — `gtk_box_pack_start` → `gtk_box_append`.
- Container/box sweep (`widgets.c`, `prop_widgets.c`, `prop_sdarray_widget.c`,
  `propdialogs.c`, the selectors) — `gtk_box_pack_start`/`gtk_container_add` →
  `gtk_box_append`/`set_child`; `gtk_button_set_relief` → `set_has_frame`;
  `gtk_misc_*` dropped; `*_new_from_icon_name` lose the size arg;
  `gtk_misc_set_alignment` → `gtk_label_set_x/yalign`.
- `message.c` — dialog content-area `gtk_container_add` → `gtk_box_append`.
- `persistence.c` — GTK4 drops window positioning; persist size only
  (`gtk_window_get/set_default_size`); `GdkScreen` monitor enumeration →
  `GdkDisplay`/`GdkMonitor`/`GListModel`; opaque `GdkEvent`.
- The menu choosers (`dia-arrow-chooser.c`, `dia-line-chooser.c`,
  `diapatternselector.c`) — `GtkMenu`/`GtkMenuItem`/`GtkArrow` (all removed) →
  `GtkPopover`/`GtkMenuButton` holding flat preview buttons; the
  `button-press-event` vfunc → `GtkButton::clicked`.

`lib/` is essentially done. `dia-io.c` is the only file left, blocked by
libxml < 2.14 (re-add once the system libxml satisfies the >= 2.14 pin).

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
   **Status: DONE** — 99/100 `lib/*.c` compile via `lib-port/` (only the
   libxml-blocked `dia-io.c` remains). Next phase is the `app/` UI layer.
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

### `lib/` porting patterns (reference for the `app/` phase)

The recurring GTK3→GTK4 migrations established while porting `lib/` — reuse
these in `app/`:
- Custom-widget drawing: `GtkWidget`/`GtkCellRenderer` `draw`/`render` (cairo)
  vfunc → `snapshot` (draw via `gtk_snapshot_append_cairo`); `get_size` →
  `get_preferred_width`/`_height`.
- Final parent classes (`GtkSpinButton`): can't subclass → compose inside a
  `GtkWidget` (`GtkBinLayout`).
- Containers: `gtk_container_add`/`gtk_box_pack_start` → `gtk_box_append` /
  `gtk_*_set_child`; drop `gtk_widget_show_all` / `set_border_width` /
  `gtk_misc_*`; `gtk_button_set_relief` → `set_has_frame`.
- Text entries: `gtk_entry_get/set_text` → `GtkEditable`.
- Events: `button-press-event` vfunc / `GdkEventButton` → `GtkGestureClick` or
  `GtkButton::clicked`; key events → `GtkEventControllerKey`; opaque `GdkEvent`
  (`event->type` → `gdk_event_get_event_type`).
- Menus: `GtkMenu`/`GtkMenuItem`/`GtkArrow` (removed) → `GtkPopover` /
  `GtkMenuButton` holding ordinary widgets.
- Windows: positioning is gone — size-only via `get/set_default_size`;
  `GdkScreen` monitor APIs → `GdkDisplay`/`GdkMonitor`/`GListModel`.
- Dialogs: `gtk_dialog_run` (removed) → async `AdwDialog`/`AdwMessageDialog` /
  `GtkFileDialog` with response callbacks.
- Misc: `*_new_from_icon_name` lose the `GtkIconSize` arg;
  `gtk_misc_set_alignment` → `gtk_label_set_x/yalign`; scrolled-window
  `new()`/`set_child`, no shadow type.

`dia-io.c` stays out until the system libxml satisfies the `>= 2.14` pin
(`XML_SAVE_EMPTY`).

### App-layer hard spots (from an API survey of `app/`)

- `gtk_box_pack_start` — 35 files
- `gtk_container_add` — 32 files
- `gtk_widget_destroy` — 27 files
- `GdkEventButton` direct field access — 21 files
- `gtk_widget_show_all` — 13 files
- `gtk_dialog_run` — 5 files
- 42 custom `GtkWidget` subclasses to revisit (drawing/input model)
