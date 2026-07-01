#!/usr/bin/env python3
"""Export the per-object-type sample diagrams (Arcs, Boxes, Lines, Texts, … —
the corpus from upstream tests/exports/, used by upstream's test-export.c)
through our CLI to PNG/SVG/PDF and assert each output is *sane*:

  * PNG  — a valid image with non-trivial dimensions,
  * SVG  — valid XML that actually contains drawing elements (path/rect/ellipse/
           line/polyline/polygon/text), i.e. the objects rendered rather than a
           blank canvas,
  * PDF  — a real %PDF document of non-trivial size.

This is the port's stand-in for upstream test-export.c (which byte-diffs against
per-plugin reference files and covers many non-cairo formats we don't ship). It
guards the thing that matters: every standard object type still produces output.

Run via meson test. Env: DIA_BIN = the built dia binary. argv[1] = exports dir.
"""
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

SAMPLES = [
    "Arcs", "Beziergons", "Bezierlines", "Boxes", "Ellipses",
    "Lines", "Polygons", "Polylines", "Texts", "Zigzaglines",
]

# Any real drawing renders to at least one of these SVG elements.
DRAW_TAGS = {"path", "rect", "ellipse", "circle", "line",
             "polyline", "polygon", "text", "image"}


def run(dia, *args):
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    return subprocess.run([dia, *args], capture_output=True, text=True, env=env)


def check_png(path):
    try:
        from PIL import Image
        im = Image.open(path)
        im.load()
        return im.width >= 4 and im.height >= 4
    except ImportError:
        return os.path.getsize(path) > 100      # can't decode; size sanity only
    except Exception:
        return False


def check_svg(path):
    try:
        root = ET.parse(path).getroot()
    except Exception:
        return False
    tags = {el.tag.split("}")[-1] for el in root.iter()}
    return bool(tags & DRAW_TAGS)               # something actually drawn


def check_pdf(path):
    try:
        with open(path, "rb") as fh:
            head = fh.read(5)
    except OSError:
        return False
    return head == b"%PDF-" and os.path.getsize(path) > 200


CHECKS = {"png": check_png, "svg": check_svg, "pdf": check_pdf}


def main():
    dia = os.environ.get("DIA_BIN")
    if not dia:
        print("DIA_BIN must point at the dia binary")
        return 2
    exports_dir = sys.argv[1] if len(sys.argv) > 1 else "tests/exports"

    failures = 0
    tested = 0
    with tempfile.TemporaryDirectory() as d:
        for name in SAMPLES:
            src = os.path.join(exports_dir, name + ".dia")
            if not os.path.exists(src):
                print("SKIP", name, "(sample missing)")
                continue
            for fmt, checker in CHECKS.items():
                out = os.path.join(d, "%s.%s" % (name, fmt))
                res = run(dia, "-t", fmt, "-e", out, src)
                tested += 1
                if res.returncode != 0 or not os.path.exists(out):
                    print("FAIL %s.%s: export failed (rc=%d)\n%s"
                          % (name, fmt, res.returncode, res.stderr.strip()))
                    failures += 1
                elif not checker(out):
                    print("FAIL %s.%s: output is not sane (empty/invalid)"
                          % (name, fmt))
                    failures += 1
                else:
                    print("PASS %s.%s" % (name, fmt))

    if tested == 0:
        print("no samples found under %r" % exports_dir)
        return 2
    if failures:
        print("\n%d export(s) produced no / insane output" % failures)
        return 1
    print("\nAll per-object-type exports produce sane PNG/SVG/PDF output (%d)"
          % tested)
    return 0


if __name__ == "__main__":
    sys.exit(main())
