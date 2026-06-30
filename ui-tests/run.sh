#!/usr/bin/env bash
#
# Launch the GTK4 Dia skeleton under AT-SPI and run the dogtail UI tests.
#
# Requires: python3-dogtail, at-spi2-core, gir1.2-atspi-2.0, python3-pyatspi.
# Uses the X11 backend so synthetic input (clicks) works through Xwayland.

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
DIA="$ROOT/build/skeleton/dia"
APP_LOG="$(mktemp /tmp/dia-uitest-app.XXXXXX.log)"

if [ ! -x "$DIA" ]; then
  echo "Build first: meson compile -C $ROOT/build" >&2
  exit 2
fi

# dogtail refuses to run unless toolkit accessibility is enabled.
gsettings set org.gnome.desktop.interface toolkit-accessibility true 2>/dev/null || true

export GTK_A11Y=atspi      # force the AT-SPI accessibility backend
export GDK_BACKEND=x11     # Xwayland: lets AT-SPI report screen coordinates
export ADW_DISABLE_PORTAL=1

# The canvas-click test injects real pointer input with ydotool (uinput),
# which needs /dev/uinput writable. ydotool delivers through the compositor,
# unlike xdotool's XTEST which WSLg's Xwayland drops.
if [ ! -w /dev/uinput ]; then
  echo "NOTE: /dev/uinput is not writable; the canvas-click check will fail." >&2
  echo "  Quick (this session):  sudo chmod 0666 /dev/uinput" >&2
  echo "  Persistent: sudo cp ui-tests/99-uinput.rules /etc/udev/rules.d/ &&" >&2
  echo "              sudo udevadm control --reload && sudo udevadm trigger" >&2
fi

echo "Launching $DIA ..."
"$DIA" >"$APP_LOG" 2>&1 &
APP_PID=$!

cleanup() { kill "$APP_PID" 2>/dev/null; wait "$APP_PID" 2>/dev/null; }
trap cleanup EXIT

# Give it a moment to register on the a11y bus.
sleep 2

python3 "$HERE/${1:-test_shell.py}"
RC=$?

echo "--- app log (filtered) ---"
grep -ivE "libEGL|MESA|dzn|Zink|Vulkan|DRI3|driver name|device information|dri2 screen" "$APP_LOG" | tail -n 5

exit $RC
