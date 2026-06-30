#!/usr/bin/env bash
#
# Run the dogtail UI tests against the GTK4 Dia skeleton.
#
# By default this runs HEADLESS: it re-execs itself inside `xvfb-run` (a virtual
# X server) and a private `dbus-run-session` (so the AT-SPI bus activates
# cleanly), then launches the app and drives it with dogtail. No windows appear,
# it needs no real display, and it works in CI and in sandboxes where the
# desktop's own display rejects new GUI clients.
#
# Set DIA_UITEST_DISPLAY=1 to use the current display instead (e.g. to watch it).
#
# Requires: python3-dogtail at-spi2-core gir1.2-atspi-2.0 python3-pyatspi xvfb.

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
# DIA_BIN lets `meson test` point at the just-built binary; default to build/.
DIA="${DIA_BIN:-$ROOT/build/skeleton/dia}"
SCRIPT="${1:-test_shell.py}"

if [ ! -x "$DIA" ]; then
  echo "Build first: meson compile -C $ROOT/build" >&2
  exit 2
fi

# Re-exec headless unless asked to use the current display or already inside.
if [ -z "${DIA_UITEST_INNER:-}" ] && [ "${DIA_UITEST_DISPLAY:-}" != "1" ]; then
  if command -v xvfb-run >/dev/null && command -v dbus-run-session >/dev/null; then
    exec xvfb-run -a dbus-run-session -- \
      env DIA_UITEST_INNER=1 bash "$0" "$SCRIPT"
  fi
  echo "NOTE: xvfb-run/dbus-run-session not found; using the current display." >&2
fi

APP_LOG="$(mktemp /tmp/dia-uitest-app.XXXXXX.log)"

# dogtail refuses to run unless toolkit accessibility is enabled.
gsettings set org.gnome.desktop.interface toolkit-accessibility true 2>/dev/null || true

export GTK_A11Y=atspi       # force the AT-SPI accessibility backend
export GDK_BACKEND=x11      # the (X)vfb server is X11
export GTK_USE_PORTAL=0     # native dialogs, no portal
export ADW_DISABLE_PORTAL=1
export DIA_UITEST=1         # expose the AT-SPI test triggers (see dia-shell.c)

# Builds carry -fsanitize=address,undefined. Keep ASan's memory-error and
# UBSan's UB detection (these abort on a real bug), but turn OFF LeakSanitizer:
# GTK/GLib keep many caches alive until exit, which would otherwise drown real
# findings in noise. detect_leaks can be re-enabled with suppressions later.
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1:halt_on_error=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
# ASan must be the first loaded library; let the app pull it in normally.

echo "Launching $DIA (headless: ${DIA_UITEST_INNER:+yes}${DIA_UITEST_INNER:-no}) ..."
"$DIA" >"$APP_LOG" 2>&1 &
APP_PID=$!

cleanup() { kill "$APP_PID" 2>/dev/null; wait "$APP_PID" 2>/dev/null; }
trap cleanup EXIT

python3 "$HERE/$SCRIPT"
RC=$?

echo "--- app log (filtered) ---"
grep -ivE "libEGL|MESA|dzn|Zink|Vulkan|DRI3|driver name|device information|dri2 screen|Xlib|extension" "$APP_LOG" | tail -n 5

exit $RC
