#!/usr/bin/env python3
"""Dogtail UI test for the GTK4 Dia skeleton.

Drives the running app over AT-SPI and asserts the wired behaviour works:
the tool palette is a radio group, the zoom buttons change the zoom readout,
and clicking the colour area opens the async colour dialog.

Run via ui-tests/run.sh (which launches the app under AT-SPI first).
Exit code 0 = all checks passed, 1 = some failed, 2 = app not found.
"""
import sys
import time

from dogtail.config import config

config.searchCutoffCount = 20
config.searchBackoffDuration = 0.5
config.actionDelay = 0.5
config.defaultDelay = 0.5
config.logDebugToStdOut = False

from dogtail.tree import root            # noqa: E402
from dogtail import predicate            # noqa: E402

results = []


def check(desc, cond):
    ok = bool(cond)
    results.append(ok)
    print(("PASS" if ok else "FAIL"), "-", desc)
    return ok


def find(node, **kw):
    """Find a descendant, returning None instead of raising."""
    return node.findChild(predicate.GenericPredicate(**kw),
                          retry=True, requireResult=False)


def find_dia_app(timeout=25):
    """Pick the application that owns our tool palette (name may vary)."""
    end = time.time() + timeout
    while time.time() < end:
        for app in root.applications():
            try:
                if app.findChild(predicate.GenericPredicate(name='Modify'),
                                 retry=False, requireResult=False):
                    return app
            except Exception:
                pass
        time.sleep(0.5)
    return None


def main():
    app = find_dia_app()
    if not app:
        print("FATAL: Dia application not found on the a11y bus")
        return 2

    print("Found application:", app.name)

    # 1. Main window + core widgets present.
    check("main window present",
          find(app, roleName='frame') is not None)
    modify = find(app, name='Modify', roleName='toggle button')
    box = find(app, name='Box', roleName='toggle button')
    check("Modify tool button present", modify)
    check("Box tool button present", box)
    check("diagram canvas present",
          find(app, name='diagram-canvas') is not None)
    check("layers list present",
          find(app, name='Background', roleName='label') is not None)

    # 2. Tool palette behaves as a radio group.
    if modify and box:
        check("Modify is active initially", modify.checked)
        box.click()
        time.sleep(0.5)
        check("Box active after clicking it", box.checked)
        check("Modify inactive after Box selected", not modify.checked)

    # 3. Zoom buttons update the zoom readout (100% -> 150%).
    check("zoom readout starts at 100%",
          find(app, name='100%', roleName='label') is not None)
    zoom_in = find(app, name='Zoom in', roleName='push button')
    check("zoom-in button present", zoom_in)
    if zoom_in:
        zoom_in.click()
        time.sleep(0.5)
        check("zoom readout becomes 150% after zoom-in",
              find(app, name='150%', roleName='label') is not None)

    # 4. Colour area opens the async colour dialog (needs synthetic click on a
    #    drawing area -> best effort; reported but not fatal on input quirks).
    colour = find(app, name='colour-area')
    check("colour area present", colour)
    if colour:
        try:
            colour.click()
            time.sleep(1.0)
            dlg = (find(app, roleName='dialog')
                   or find(root, name='Foreground Colour'))
            check("colour dialog opened on click", dlg)
        except Exception as exc:
            check("colour dialog opened on click (%s)" % exc, False)

    passed = sum(results)
    total = len(results)
    print("\n%d/%d checks passed" % (passed, total))
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
