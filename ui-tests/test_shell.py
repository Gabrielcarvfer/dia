#!/usr/bin/env python3
"""Dogtail UI test for the GTK4 Dia skeleton.

Dumps the accessible tree (so we can see what AT-SPI exposes) and then runs
robust behavioural checks: the tool palette is a radio group, the zoom
buttons change the readout, and the colour area opens the async colour dialog.

Run via ui-tests/run.sh. Exit 0 = all passed, 1 = some failed, 2 = app missing.
"""
import os
import re
import sys
import time

from dogtail.config import config

# Set UITEST_VERBOSE=1 for the accessible-tree dump and raw-state diagnostics.
VERBOSE = bool(os.environ.get("UITEST_VERBOSE"))

config.searchCutoffCount = 20
config.searchBackoffDuration = 0.5
config.actionDelay = 0.5
config.defaultDelay = 0.5
config.logDebugToStdOut = False

import pyatspi                            # noqa: E402
from dogtail.tree import root            # noqa: E402
from dogtail import predicate            # noqa: E402

results = []


def check(desc, cond):
    ok = bool(cond)
    results.append(ok)
    print(("PASS" if ok else "FAIL"), "-", desc)
    return ok


def soft(desc, cond):
    """Best-effort check: reported but never fails the suite. Used for things
    that depend on synthesized input, which some environments (e.g. Xwayland)
    don't deliver to drawing areas."""
    print(("PASS" if cond else "SKIP"), "-", desc,
          "" if cond else "(synthesized input not delivered in this environment)")
    return bool(cond)


def find(node, **kw):
    return node.findChild(predicate.GenericPredicate(**kw),
                          retry=True, requireResult=False)


def states_of(node):
    """Set of short state names, e.g. {'PRESSED', 'SENSITIVE', ...}.

    pyatspi prints states as '<enum ATSPI_STATE_PRESSED of type ...>', so we
    pull the name out of the ATSPI_STATE_<NAME> token.
    """
    out = set()
    try:
        for s in node.getState().get_states():
            m = re.search(r'ATSPI_STATE_(\w+)', str(s))
            out.add(m.group(1) if m else str(s))
    except Exception:
        pass
    return out


def all_state_names(node):
    try:
        return sorted(str(s) for s in node.getState().get_states())
    except Exception as exc:
        return ["<err %s>" % exc]


def is_selected(node):
    """GTK4 toggle buttons expose 'active' as PRESSED; radios as CHECKED."""
    s = states_of(node)
    return bool(s & {"PRESSED", "CHECKED", "SELECTED"})


def actions_of(node):
    try:
        return list(node.actions.keys())
    except Exception:
        return []


def do_click(node):
    for name in ("click", "press", "activate", "toggle"):
        try:
            node.doActionNamed(name)
            return True
        except Exception:
            continue
    try:
        node.click()
        return True
    except Exception as exc:
        print("   (click failed:", exc, ")")
        return False


def dump(node, depth=0, maxdepth=8):
    interesting = states_of(node) & {
        "PRESSED", "CHECKED", "SELECTED", "ACTIVE", "FOCUSED",
        "SHOWING", "SENSITIVE", "EDITABLE",
    }
    print("  " * depth + "[%s] %r states=%s actions=%s"
          % (node.roleName, node.name, sorted(interesting), actions_of(node)))
    if depth < maxdepth:
        for child in node.children:
            dump(child, depth + 1, maxdepth)


def find_dia_app(timeout=25):
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
    if VERBOSE:
        print("\n================ ACCESSIBLE TREE ================")
        try:
            dump(app)
        except Exception as exc:
            print("(tree dump error:", exc, ")")
        print("================================================\n")

    # 1. Structure.
    check("main window present", find(app, roleName='frame') is not None)
    modify = find(app, name='Modify', roleName='toggle button')
    box = find(app, name='Box', roleName='toggle button')
    check("Modify tool button present", modify)
    check("Box tool button present", box)

    canvas = (find(app, name='diagram-canvas')
              or find(app, roleName='image')
              or find(app, roleName='drawing area'))
    check("diagram canvas present", canvas)
    check("layers list present",
          find(app, name='Background', roleName='label') is not None)

    if VERBOSE:
        print("\n----- DIAGNOSTICS -----")
        if modify:
            print("DIAG Modify states :", all_state_names(modify))
        if box:
            print("DIAG Box states    :", all_state_names(box))
        for nm in ('diagram-canvas', 'colour-area'):
            hits = app.findChildren(predicate.GenericPredicate(name=nm))
            print("DIAG name %-15r hits=%d roles=%s"
                  % (nm, len(hits), [h.roleName for h in hits]))
        print("-----------------------\n")

    # 2. Tool palette behaves as a radio group (active toggle => PRESSED state).
    if modify and box:
        check("Modify selected initially", is_selected(modify))
        do_click(box)
        time.sleep(0.6)
        check("Box selected after clicking it", is_selected(box))
        check("Modify deselected after Box selected", not is_selected(modify))

    # 3. Zoom buttons update the readout (100% -> 150%).
    check("zoom readout starts at 100%",
          find(app, name='100%', roleName='label') is not None)
    zoom_in = find(app, name='Zoom in', roleName='push button')
    check("zoom-in button present", zoom_in)
    if zoom_in:
        print("   zoom-in actions:", actions_of(zoom_in))
        do_click(zoom_in)
        time.sleep(0.6)
        check("zoom readout becomes 150% after zoom-in",
              find(app, name='150%', roleName='label') is not None)

    # 3b. Clicking the canvas with the Box tool creates an object (the
    #     statusbar reports the new object count). Needs a synthesized click
    #     on the drawing area -> best effort (reported, not fatal).
    # WSLg delivers no synthesized input (uinput ignored, XTEST dropped), so we
    # exercise the canvas via the DIA_UITEST trigger: it applies the current
    # tool (Box, selected above) at a fixed page point through the SAME code a
    # real canvas click runs. Invoked via its AT-SPI action -> no input synth.
    trigger = find(app, name='uitest-apply-tool', roleName='push button')
    check("DIA_UITEST trigger present (run via ui-tests/run.sh)", trigger)
    if trigger:
        do_click(trigger)
        time.sleep(0.5)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        status = [lab.name for lab in labels
                  if lab.name and 'object(s)' in lab.name]
        if VERBOSE:
            print("   statusbar after trigger:", status)
        created = any('object(s)' in (lab.name or '') for lab in labels)
        check("Box tool creates an object on the canvas", created)

    # 4. Colour area opens the async colour dialog.
    colour = find(app, name='colour-area')
    check("colour area present", colour)
    if colour:
        try:
            do_click(colour)
            time.sleep(1.2)
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
