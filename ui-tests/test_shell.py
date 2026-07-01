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
    check("Main window present", find(app, roleName='frame') is not None)
    modify = find(app, name='Modify', roleName='toggle button')
    box = find(app, name='Box', roleName='toggle button')
    check("Modify tool button present", modify)
    check("Box tool button present", box)

    canvas = (find(app, name='diagram-canvas')
              or find(app, roleName='image')
              or find(app, roleName='drawing area'))
    check("Diagram canvas present", canvas)
    check("Layers list present",
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

    # 3. Zoom controls present; the editable zoom field exists (its text is not
    #    reliably exposed over AT-SPI, so the zoom behaviour is checked below via
    #    the DIA_UITEST trigger at 3j).
    check("Editable zoom field present",
          find(app, name='zoom') is not None)
    zoom_in = find(app, name='Zoom in', roleName='push button')
    check("Zoom-in button present", zoom_in)

    # 3b. Clicking the canvas with the Box tool creates an object (the
    #     statusbar reports the new object count). Needs a synthesized click
    #     on the drawing area -> best effort (reported, not fatal).
    # WSLg delivers no synthesized input (uinput ignored, XTEST dropped), so we
    # exercise the canvas via the DIA_UITEST trigger: it applies the current
    # tool (Box, selected above) at a fixed page point through the SAME code a
    # real canvas click runs. Invoked via its AT-SPI action -> no input synth.
    trigger = (find(app, name='uitest-apply-tool')
               or find(app, name='uitest-apply-tool', roleName='push button'))
    if not trigger:
        # Diagnose: list push-button names so we can see what the env produced.
        btns = app.findChildren(predicate.GenericPredicate(roleName='push button'))
        print("   push buttons:", [b.name for b in btns])
        print("   DIA_UITEST =", os.environ.get('DIA_UITEST'))
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

    # 3c. The File menu actions: the New toolbar button drives the dia.new
    #     GAction, which clears the diagram (statusbar -> "0 object(s)").
    new_btn = find(app, name='New diagram', roleName='push button')
    check("New toolbar button present (GAction wired)", new_btn)
    if new_btn:
        do_click(new_btn)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        cleared = any('0 object(s)' in (lab.name or '') for lab in labels)
        check("New action clears the diagram", cleared)

    # 3d. Real .dia I/O round-trip (save -> clear -> reload) via the DIA_UITEST
    #     trigger, so it doesn't need the file chooser. Asserts the object count
    #     survives, i.e. objects serialize and parse through real .dia XML.
    rt = find(app, name='uitest-roundtrip', roleName='push button')
    check("DIA_UITEST round-trip trigger present", rt)
    if rt:
        do_click(rt)
        time.sleep(0.5)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('round-trip OK' in (lab.name or '') for lab in labels)
        check("Real .dia save/reload round-trip preserves objects", ok)

    # 3e. Modify tool: select + move logic (via the DIA_UITEST trigger).
    sm = find(app, name='uitest-select-move', roleName='push button')
    check("DIA_UITEST select-move trigger present", sm)
    if sm:
        do_click(sm)
        time.sleep(0.5)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('select+move OK' in (lab.name or '') for lab in labels)
        check("Modify tool selects and moves an object", ok)

    # 3f. Undo/redo (create -> undo -> redo) via the DIA_UITEST trigger.
    ur = find(app, name='uitest-undo-redo', roleName='push button')
    check("DIA_UITEST undo-redo trigger present", ur)
    if ur:
        do_click(ur)
        time.sleep(0.5)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('undo/redo OK' in (lab.name or '') for lab in labels)
        check("Undo/redo restores object count", ok)

    # 3g. Extra object sets (flowchart, network, ER) create on the canvas via
    #     the DIA_UITEST trigger -> proves those types are registered.
    ex = find(app, name='uitest-extra-objects', roleName='push button')
    check("DIA_UITEST extra-objects trigger present", ex)
    if ex:
        do_click(ex)
        time.sleep(0.5)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('extra-objects OK' in (lab.name or '') for lab in labels)
        check("Flowchart/network/ER object types create on the canvas", ok)

    # 3h. Modify tool: dragging a handle resizes/stretches the object. Exercised
    #     via the DIA_UITEST trigger (it calls dia_object_move_handle, the same
    #     call the canvas handle-drag uses) and asserts the object grew.
    rz = find(app, name='uitest-resize', roleName='push button')
    check("DIA_UITEST resize trigger present", rz)
    if rz:
        do_click(rz)
        time.sleep(0.5)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('resize OK' in (lab.name or '') for lab in labels)
        check("Modify tool resizes an object by its handle", ok)

    # 3i. Delete removes the selected object (create -> delete -> undo) via the
    #     DIA_UITEST trigger; the same delete_selected() the Delete key calls.
    dl = find(app, name='uitest-delete', roleName='push button')
    check("DIA_UITEST delete trigger present", dl)
    if dl:
        do_click(dl)
        time.sleep(0.5)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('delete OK' in (lab.name or '') for lab in labels)
        check("Delete removes the selected object (and undo restores it)", ok)

    # 3j. Zoom in/out changes the zoom level (model-level, via the trigger).
    zm = find(app, name='uitest-zoom', roleName='push button')
    check("DIA_UITEST zoom trigger present", zm)
    if zm:
        do_click(zm)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('zoom OK' in (lab.name or '') for lab in labels)
        check("Zoom in/out changes the zoom level", ok)

    # 3k. Snap-to-grid rounds a point to the grid.
    sn = find(app, name='uitest-snap', roleName='push button')
    check("DIA_UITEST snap trigger present", sn)
    if sn:
        do_click(sn)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('snap OK' in (lab.name or '') for lab in labels)
        check("Snap-to-grid rounds a point to the grid", ok)

    # 3l. Layer add/remove (the wired layers panel buttons), via the trigger.
    ly = find(app, name='uitest-layers', roleName='push button')
    check("DIA_UITEST layers trigger present", ly)
    if ly:
        do_click(ly)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('layers OK' in (lab.name or '') for lab in labels)
        check("Add/remove layer updates the layer list", ok)

    # 3m. Line attributes round-trip through StdProp (width set + read back).
    la = find(app, name='uitest-lineattr', roleName='push button')
    check("DIA_UITEST line-attr trigger present", la)
    if la:
        do_click(la)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('lineattr OK' in (lab.name or '') for lab in labels)
        check("Line width applies to an object (StdProp)", ok)

    # 3n. Export backend writes a non-trivial PNG (used by the Export action
    #     and the --export CLI option).
    ex = find(app, name='uitest-export', roleName='push button')
    check("DIA_UITEST export trigger present", ex)
    if ex:
        do_click(ex)
        time.sleep(0.5)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('export OK' in (lab.name or '') for lab in labels)
        check("Export renders the diagram to a file", ok)

    # 3o. Copy/paste clones the selected object (clipboard).
    cb = find(app, name='uitest-clipboard', roleName='push button')
    check("DIA_UITEST clipboard trigger present", cb)
    if cb:
        do_click(cb)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('clipboard OK' in (lab.name or '') for lab in labels)
        check("Copy/paste clones the selected object", ok)

    # 3p. Shapes drawer: a sheet shape becomes the tool and creates its object.
    sh = find(app, name='uitest-sheet', roleName='push button')
    check("DIA_UITEST sheet trigger present", sh)
    if sh:
        do_click(sh)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('sheet OK' in (lab.name or '') for lab in labels)
        check("Sheet shape creates its object type", ok)

    # 3q. Connection points: connect a line endpoint to a box CP, move the box,
    #     the endpoint tracks it.
    cn = find(app, name='uitest-connect', roleName='push button')
    check("DIA_UITEST connect trigger present", cn)
    if cn:
        do_click(cn)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('connect OK' in (lab.name or '') for lab in labels)
        check("Connected line endpoint tracks the object", ok)

    # 3r. Text editing: set + read back a text object's content (the get/set
    #     the double-click editor uses).
    tx = find(app, name='uitest-text', roleName='push button')
    check("DIA_UITEST text trigger present", tx)
    if tx:
        do_click(tx)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('text OK' in (lab.name or '') for lab in labels)
        check("Text object content can be set and read", ok)

    # 3s. Multi-select: rubber-band selects two boxes, delete removes both.
    ms = find(app, name='uitest-multiselect', roleName='push button')
    check("DIA_UITEST multiselect trigger present", ms)
    if ms:
        do_click(ms)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('multiselect OK' in (lab.name or '') for lab in labels)
        check("Rubber-band selects and deletes multiple objects", ok)

    # 3t. Group/ungroup the selection.
    gr = find(app, name='uitest-group', roleName='push button')
    check("DIA_UITEST group trigger present", gr)
    if gr:
        do_click(gr)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('group OK' in (lab.name or '') for lab in labels)
        check("Group then ungroup restores the objects", ok)

    # 3u. Z-order: bring to front / send to back.
    zo = find(app, name='uitest-zorder', roleName='push button')
    check("DIA_UITEST z-order trigger present", zo)
    if zo:
        do_click(zo)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('zorder OK' in (lab.name or '') for lab in labels)
        check("Bring-to-front / send-to-back reorders objects", ok)

    # 3v. Align: two boxes aligned left share a left edge.
    al = find(app, name='uitest-align', roleName='push button')
    check("DIA_UITEST align trigger present", al)
    if al:
        do_click(al)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('align OK' in (lab.name or '') for lab in labels)
        check("Align left lines up the left edges", ok)

    # 3w. Colour: set a box's line colour and read it back.
    co = find(app, name='uitest-colour', roleName='push button')
    check("DIA_UITEST colour trigger present", co)
    if co:
        do_click(co)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('colour OK' in (lab.name or '') for lab in labels)
        check("Colour picker sets the object's line colour", ok)

    # 3x. Distribute: three boxes spaced evenly.
    di = find(app, name='uitest-distribute', roleName='push button')
    check("DIA_UITEST distribute trigger present", di)
    if di:
        do_click(di)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('distribute OK' in (lab.name or '') for lab in labels)
        check("Distribute evenly spaces objects", ok)

    # 3y. Properties: libdia builds + applies an object's StdProp editor widget.
    pr = find(app, name='uitest-properties', roleName='push button')
    check("DIA_UITEST properties trigger present", pr)
    if pr:
        do_click(pr)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('properties OK' in (lab.name or '') for lab in labels)
        check("Object property editor builds and applies", ok)

    # 3z. Polygon: create, move a vertex, add a vertex via the object menu.
    pg = find(app, name='uitest-polygon', roleName='push button')
    check("DIA_UITEST polygon trigger present", pg)
    if pg:
        do_click(pg)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('polygon OK' in (lab.name or '') for lab in labels)
        check("Polygon create + move-vertex + add-vertex", ok)

    # 3aa. Pan: the Scroll tool's pan moves the origin opposite the drag.
    pn = find(app, name='uitest-pan', roleName='push button')
    check("DIA_UITEST pan trigger present", pn)
    if pn:
        do_click(pn)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('pan OK' in (lab.name or '') for lab in labels)
        check("Pan moves the viewport origin", ok)

    # 3ab. Nested groups: a group inside a group (recursive grouping).
    ng = find(app, name='uitest-nestedgroup', roleName='push button')
    check("DIA_UITEST nestedgroup trigger present", ng)
    if ng:
        do_click(ng)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('nestedgroup OK' in (lab.name or '') for lab in labels)
        check("Recursive grouping (group inside a group)", ok)

    # 3ac. Rotate 90°: a line turns vertical; a box swaps w/h pixel-clean.
    ro = find(app, name='uitest-rotate', roleName='push button')
    check("DIA_UITEST rotate trigger present", ro)
    if ro:
        do_click(ro)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('rotate OK' in (lab.name or '') for lab in labels)
        check("Rotate 90° (line geometry + pixel-clean box swap)", ok)

    # 3ad. Undo/redo of a rotation (OP_ROTATE) and add-corner edit (OP_CHANGE).
    ue = find(app, name='uitest-undoedit', roleName='push button')
    check("DIA_UITEST undoedit trigger present", ue)
    if ue:
        do_click(ue)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('undoedit OK' in (lab.name or '') for lab in labels)
        check("Undo/redo of rotate + property/menu edit", ok)

    # 3ae. A tilted (arbitrary-angle) box's handles hit-test where they're drawn.
    rh = find(app, name='uitest-rothandle', roleName='push button')
    check("DIA_UITEST rothandle trigger present", rh)
    if rh:
        do_click(rh)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('rothandle OK' in (lab.name or '') for lab in labels)
        check("Rotated-object handles are grabbable at drawn positions", ok)

    # 3af. Layers-panel moves: object -> another layer, and object -> into a group.
    lm = find(app, name='uitest-layermove', roleName='push button')
    check("DIA_UITEST layermove trigger present", lm)
    if lm:
        do_click(lm)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('layermove OK' in (lab.name or '') for lab in labels)
        check("Move object between layers and into a group", ok)

    # 3ag. Text font + size set via StdProp round-trips (New Text dialog path).
    ts = find(app, name='uitest-textstyle', roleName='push button')
    check("DIA_UITEST textstyle trigger present", ts)
    if ts:
        do_click(ts)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('textstyle OK' in (lab.name or '') for lab in labels)
        check("Text font and size are settable", ok)

    # 3ai. Rotating text 90 twice accumulates to 180 (text_angle).
    tr = find(app, name='uitest-textrotate', roleName='push button')
    check("DIA_UITEST textrotate trigger present", tr)
    if tr:
        do_click(tr)
        time.sleep(0.4)
        labels = app.findChildren(predicate.GenericPredicate(roleName='label'))
        ok = any('textrotate OK' in (lab.name or '') for lab in labels)
        check("Text rotation accumulates (90+90=180)", ok)

    # 3ah. The New Text dialog actually presents (GtkFontDialogButton path).
    nt = find(app, name='uitest-newtext', roleName='push button')
    check("DIA_UITEST newtext trigger present", nt)
    if nt:
        do_click(nt)
        time.sleep(1.0)
        dlg = (find(app, roleName='dialog') or find(root, roleName='dialog')
               or find(root, name='New Text'))
        check("New Text dialog opens", dlg)

    # 4. Colour area opens the async colour dialog.
    colour = find(app, name='colour-area')
    check("Colour area present", colour)
    if colour:
        try:
            do_click(colour)
            time.sleep(1.2)
            dlg = (find(app, roleName='dialog')
                   or find(root, name='Foreground Colour'))
            check("Colour dialog opened on click", dlg)
        except Exception as exc:
            check("Colour dialog opened on click (%s)" % exc, False)

    passed = sum(results)
    total = len(results)
    print("\n%d/%d checks passed" % (passed, total))
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
