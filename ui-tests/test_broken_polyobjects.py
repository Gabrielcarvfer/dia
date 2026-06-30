#!/usr/bin/env python3
"""Regression test for GNOME/dia#319 / bug 302781 ("Dia should not crash on
highly broken files"): loading a .dia whose poly/bezier object is missing its
point-count attribute used to crash.

The point count went to zero, and the loaders then drove the handle/connection
allocations underflow-negative (e.g. ``3*count-2`` == -2), producing:

    GLib-ERROR: overflow allocating 18446744073709551614*8 bytes

or dereferenced a NULL points array. Dia should not crash on broken input -- it
must substitute a placeholder, warn, and carry on.

This mirrors test_broken_orthconn.py over the four sibling loaders:

    Standard - PolyLine   -> polyconn_load   (lib/poly_conn.c)
    Standard - BezierLine -> bezierconn_load (lib/bezier_conn.c)
    Standard - Beziergon  -> beziershape_load(lib/beziershape.c)
    Standard - Polygon    -> polyshape_load  (lib/polyshape.c)

We export each broken file headlessly and assert it succeeds without an abort.

https://gitlab.gnome.org/GNOME/dia/-/issues/319

Run via meson test. Env: DIA_BIN = the built dia binary.
"""
import os
import subprocess
import sys
import tempfile

# Each object omits the points attribute (poly_points / bez_points) that the
# loader needs to size its handles -- the exact #319 / 302781 trigger.
DIA_XML = """<?xml version="1.0" encoding="UTF-8"?>
<dia:diagram xmlns:dia="http://www.lysator.liu.se/~alla/dia/">
  <dia:layer name="Background" visible="true" active="true">
    <dia:object type="%s" version="1" id="O0">
      <dia:attribute name="obj_pos">
        <dia:point val="1,1"/>
      </dia:attribute>
      <dia:attribute name="obj_bb">
        <dia:rectangle val="1,1;2,2"/>
      </dia:attribute>
    </dia:object>
  </dia:layer>
</dia:diagram>
"""

OBJECT_TYPES = [
    "Standard - PolyLine",
    "Standard - BezierLine",
    "Standard - Beziergon",
    "Standard - Polygon",
]


def check_one(dia, obj_type, workdir):
    safe = obj_type.replace(" ", "_").replace("-", "")
    dia_file = os.path.join(workdir, "broken-%s.dia" % safe)
    out = os.path.join(workdir, "out-%s.png" % safe)
    with open(dia_file, "w") as fh:
        fh.write(DIA_XML % obj_type)

    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    res = subprocess.run([dia, "--export", out, dia_file],
                         capture_output=True, text=True, env=env)
    err = res.stderr or ""

    # Negative return code => killed by a signal (SIGABRT from the GLib
    # allocation failure is the crash). A non-zero exit (e.g. UBSan abort on a
    # NULL deref) is also a regression since the file is recoverable.
    if res.returncode != 0:
        return "FAIL %s: export exited %d (crash on broken object?)\n%s" % (
            obj_type, res.returncode, err)
    if "overflow allocating" in err or "GLib-ERROR" in err:
        return "FAIL %s: allocation overflow:\n%s" % (obj_type, err)
    if "runtime error" in err or "Sanitizer" in err:
        return "FAIL %s: sanitizer error:\n%s" % (obj_type, err)
    if not os.path.exists(out) or os.path.getsize(out) == 0:
        return "FAIL %s: no output produced" % obj_type
    return None


def main():
    dia = os.environ.get("DIA_BIN")
    if not dia:
        print("DIA_BIN must point at the dia binary")
        return 2

    failures = []
    with tempfile.TemporaryDirectory() as d:
        for obj_type in OBJECT_TYPES:
            err = check_one(dia, obj_type, d)
            if err:
                failures.append(err)
            else:
                print("%s: placeholder substituted, no crash OK" % obj_type)

    if failures:
        print("\n".join(failures))
        return 1

    print("all broken poly/bezier objects imported OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
