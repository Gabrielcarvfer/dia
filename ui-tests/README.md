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

Add `xvfb` for headless runs (recommended): `sudo apt-get install xvfb`.

## Run

```sh
meson test -C build dogtail-ui  # automated, headless (recommended)

# or directly:
meson compile -C build          # build the app first
bash ui-tests/run.sh            # headless via xvfb-run + dbus-run-session
DIA_UITEST_DISPLAY=1 bash ui-tests/run.sh   # use the current display (to watch)
UITEST_VERBOSE=1 bash ui-tests/run.sh       # + accessible-tree dump & diagnostics
bash ui-tests/run.sh other.py               # run a different test script
```

By default `run.sh` re-execs itself inside `xvfb-run` (a virtual X server) and a
private `dbus-run-session`, so it needs no real display, shows no windows, and
runs in CI / headless sandboxes. Exit code 0 = all checks passed.

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
