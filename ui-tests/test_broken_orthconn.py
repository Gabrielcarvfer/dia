#!/usr/bin/env python3
"""Regression test for GNOME/dia#319: loading a .dia whose orthogonal
connection (ZigZagLine here) is missing its ``orth_points`` attribute used to
crash with a huge allocation:

    GLib-ERROR: overflow allocating 18446744073709551615*8 bytes

because the point count went to zero/negative and drove the handle and
orientation allocations underflow-negative. Dia should not crash on broken
input -- it must substitute a placeholder, warn, and carry on.

We export the broken file headlessly and assert it succeeds without an abort.

https://gitlab.gnome.org/GNOME/dia/-/issues/319

Run via meson test. Env: DIA_BIN = the built dia binary.
"""
import os
import subprocess
import sys
import tempfile

# A ZigZagLine (an orthconn) with NO orth_points / orth_orient attributes.
DIA_XML = """<?xml version="1.0" encoding="UTF-8"?>
<dia:diagram xmlns:dia="http://www.lysator.liu.se/~alla/dia/">
  <dia:layer name="Background" visible="true" active="true">
    <dia:object type="Standard - ZigZagLine" version="1" id="O0">
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


def main():
    dia = os.environ.get("DIA_BIN")
    if not dia:
        print("DIA_BIN must point at the dia binary")
        return 2

    with tempfile.TemporaryDirectory() as d:
        dia_file = os.path.join(d, "broken-orthconn.dia")
        out = os.path.join(d, "out.png")
        with open(dia_file, "w") as fh:
            fh.write(DIA_XML)

        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
        res = subprocess.run([dia, "--export", out, dia_file],
                             capture_output=True, text=True, env=env)
        err = res.stderr or ""

        # Negative return code => killed by a signal (SIGABRT from the GLib
        # allocation failure is the #319 crash). A clean non-zero is also a
        # regression here since the file is recoverable.
        if res.returncode != 0:
            print("FAIL: export exited %d (crash on broken orthconn?)\n%s"
                  % (res.returncode, err))
            return 1
        if "overflow allocating" in err or "GLib-ERROR" in err:
            print("FAIL: allocation overflow on broken orthconn:\n%s" % err)
            return 1
        if not os.path.exists(out) or os.path.getsize(out) == 0:
            print("FAIL: no output produced")
            return 1

    print("broken-orthconn import OK (placeholder substituted, no crash)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
