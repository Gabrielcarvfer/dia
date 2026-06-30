# Dia GTK4 UI tests (dogtail)

Automated UI tests for the GTK4/libadwaita port, driving the running app over
AT-SPI with [dogtail](https://gitlab.com/dogtail/dogtail) — so behaviour is
verified without manual clicking.

## Requirements

```sh
sudo apt-get install python3-dogtail at-spi2-core gir1.2-atspi-2.0 \
                     python3-pyatspi
```

A session D-Bus and a display (X11 or Wayland) must be available. The runner
enables accessibility automatically (`toolkit-accessibility` gsetting).

## Run

```sh
meson compile -C build          # build the app first
bash ui-tests/run.sh            # launch under AT-SPI + run test_shell.py
UITEST_VERBOSE=1 bash ui-tests/run.sh   # + accessible-tree dump & state diagnostics
bash ui-tests/run.sh other.py   # run a different test script
```

Exit code 0 = all checks passed.

## What it checks (`test_shell.py`)

- main window, tool buttons, canvas and layer list are present;
- the tool palette is a radio group (selecting *Box* deselects *Modify*);
- the zoom buttons change the readout (100% → 150%);
- clicking the colour area opens the async colour dialog.

## Notes for writing more tests (GTK4 + AT-SPI gotchas)

- A bare `GtkDrawingArea` is **invisible** to AT-SPI. Give it an accessible
  role (`g_object_new (GTK_TYPE_DRAWING_AREA, "accessible-role",
  GTK_ACCESSIBLE_ROLE_IMG, NULL)`) and a label so tests/screen-readers find it.
- A GTK4 active **toggle button** exposes `ATSPI_STATE_PRESSED` (not
  `CHECKED`). Radios use `CHECKED`.
- Prefer real widgets with a `click` action over gestures on drawing areas —
  dogtail can invoke the action directly instead of synthesizing a mouse event.
- Set accessible labels with `gtk_accessible_update_property (...,
  GTK_ACCESSIBLE_PROPERTY_LABEL, "name", -1)`; they become the AT-SPI name.
