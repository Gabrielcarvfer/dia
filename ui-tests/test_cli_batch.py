#!/usr/bin/env python3
"""Headless CLI batch/scoping options: multiple input files, -O/-I directories,
and -L/--show-layers layer filtering. Needs no display.

Run via meson test. Env: DIA_BIN = the built dia binary.
"""
import os
import subprocess
import sys
import tempfile

# Two layers, each with one shape in a different spot, so a per-layer export has
# clearly smaller extents than the whole diagram.
TWO_LAYER = """<?xml version="1.0" encoding="UTF-8"?>
<dia:diagram xmlns:dia="http://www.lysator.liu.se/~alla/dia/">
  <dia:layer name="Lower" visible="true" active="true">
    <dia:object type="Standard - Box" version="0" id="O0">
      <dia:attribute name="elem_corner"><dia:point val="1,1"/></dia:attribute>
      <dia:attribute name="elem_width"><dia:real val="2"/></dia:attribute>
      <dia:attribute name="elem_height"><dia:real val="2"/></dia:attribute>
    </dia:object>
  </dia:layer>
  <dia:layer name="Upper" visible="true">
    <dia:object type="Standard - Ellipse" version="0" id="O1">
      <dia:attribute name="elem_corner"><dia:point val="10,10"/></dia:attribute>
      <dia:attribute name="elem_width"><dia:real val="2"/></dia:attribute>
      <dia:attribute name="elem_height"><dia:real val="2"/></dia:attribute>
    </dia:object>
  </dia:layer>
</dia:diagram>
"""


def run(dia, *args):
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    return subprocess.run([dia, *args], capture_output=True, text=True, env=env)


def size(path):
    try:
        from PIL import Image
        return Image.open(path).size
    except ImportError:
        return None


def main():
    dia = os.environ.get("DIA_BIN")
    if not dia:
        print("DIA_BIN must point at the dia binary")
        return 2

    failures = 0
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "two.dia")
        with open(src, "w") as fh:
            fh.write(TWO_LAYER)

        # -L: a single-layer export must be smaller than the whole diagram.
        full = os.path.join(d, "full.png")
        lower = os.path.join(d, "lower.png")
        idx1 = os.path.join(d, "idx1.png")
        run(dia, "-e", full, src)
        run(dia, "-e", lower, "-L", "Lower", src)      # by name
        run(dia, "-e", idx1, "-L", "1", src)           # by index
        sf, sl, si = size(full), size(lower), size(idx1)
        if not all(os.path.getsize(p) > 0 for p in (full, lower, idx1)):
            print("FAIL: a -L export produced an empty file")
            failures += 1
        elif sf and sl and si:
            if not (sl[0] < sf[0] and sl[1] < sf[1]):
                print("FAIL: -L Lower not smaller than full (%s vs %s)" % (sl, sf))
                failures += 1
            elif not (si[0] < sf[0] and si[1] < sf[1]):
                print("FAIL: -L 1 not smaller than full (%s vs %s)" % (si, sf))
                failures += 1
            else:
                print("PASS: -L filters layers by name and index (%s/%s < %s)"
                      % (sl, si, sf))
        else:
            print("PASS: -L exports (Pillow absent; size check skipped)")

        # multiple inputs + -O + -t: output names derived per input.
        a = os.path.join(d, "alpha.dia")
        b = os.path.join(d, "beta.dia")
        for p in (a, b):
            with open(p, "w") as fh:
                fh.write(TWO_LAYER)
        outdir = os.path.join(d, "out")
        os.mkdir(outdir)
        run(dia, "-t", "png", "-O", outdir, a, b)
        got = sorted(os.listdir(outdir))
        if got != ["alpha.png", "beta.png"]:
            print("FAIL: multi-file -O -t produced %s" % got)
            failures += 1
        else:
            print("PASS: multi-file export with -O/-t derives names")

        # -I resolves the input against a directory.
        out = os.path.join(d, "viaI.png")
        res = run(dia, "-I", d, "-e", out, "two.dia")
        if res.returncode != 0 or not os.path.exists(out):
            print("FAIL: -I input-directory (rc=%d)\n%s" % (res.returncode,
                                                            res.stderr))
            failures += 1
        else:
            print("PASS: -I resolves the input directory")

    if failures:
        print("CLI batch FAILED")
        return 1
    print("CLI batch OK (-L, multi-file, -O, -I)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
