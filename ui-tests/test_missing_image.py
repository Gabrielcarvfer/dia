#!/usr/bin/env python3
"""A .dia that references a MISSING image must still export — falling back to
the built-in broken-image placeholder — without spewing 'Missing resource' or
GdkPixbuf CRITICAL assertions. That requires the libdia gresource
(broken-image.png) to be bundled into the port.

(A genuinely-missing image legitimately logs "image ... was not found"; the bug
is the broken-image RESOURCE itself being missing, which corrupts the render.)

Run via meson test. Env: DIA_BIN = the built dia binary.
"""
import os
import subprocess
import sys
import tempfile

# Image file points at an absolute path that does not exist.
DIA_XML = """<?xml version="1.0" encoding="UTF-8"?>
<dia:diagram xmlns:dia="http://www.lysator.liu.se/~alla/dia/">
  <dia:layer name="Background" visible="true">
    <dia:object type="Standard - Image" version="0" id="O0">
      <dia:attribute name="obj_pos"><dia:point val="1,1"/></dia:attribute>
      <dia:attribute name="elem_corner"><dia:point val="1,1"/></dia:attribute>
      <dia:attribute name="elem_width"><dia:real val="3"/></dia:attribute>
      <dia:attribute name="elem_height"><dia:real val="3"/></dia:attribute>
      <dia:attribute name="file">
        <dia:string>#/does/not/exist/nope.png#</dia:string>
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
        dia_file = os.path.join(d, "img.dia")
        out = os.path.join(d, "out.png")
        with open(dia_file, "w") as fh:
            fh.write(DIA_XML)

        res = subprocess.run([dia, "--export", out, dia_file],
                             capture_output=True, text=True)
        err = res.stderr or ""

        if res.returncode != 0:
            print("FAIL: export exited %d\n%s" % (res.returncode, err))
            return 1
        if "Missing resource" in err:
            print("FAIL: broken-image resource not bundled:\n%s" % err)
            return 1
        if "CRITICAL" in err or "assertion" in err:
            print("FAIL: pixbuf criticals on missing image:\n%s" % err)
            return 1
        if not os.path.exists(out) or os.path.getsize(out) == 0:
            print("FAIL: no output produced")
            return 1

    print("missing-image export OK (broken-image fallback, no criticals)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
