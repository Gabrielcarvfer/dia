#!/usr/bin/env python3
"""CLI export parity canary: our `dia --export` should render the same content
as upstream dia. For a curated set of samples we export with both binaries,
trim surrounding whitespace, normalise to a common size, and assert the pixel
difference stays below a (generous) threshold. This catches rendering
regressions and object sets silently dropping out of the export — the
difference jumps well above the threshold when content goes missing.

It is a CANARY, not a strict pixel match: anti-aliasing, font rendering and our
2x default DPI make a few percent of difference normal (measured 0.7-7.3% on
supported samples), so the threshold is loose.

Skips cleanly (exit 77) when upstream `dia`, `xvfb-run`, or Pillow is
unavailable — upstream dia needs an X display, ours does not.

Run via meson test. Args: argv[1] = samples directory. Env: DIA_BIN = our dia.
"""
import os
import shutil
import subprocess
import sys
import tempfile

SKIP = 77

# Curated, well-supported samples (standard / custom-shape / arc-bezier / UML /
# ER). Kept small so the test stays quick.
SAMPLES = [
    "Circuit.dia", "arrows.dia", "arcs.dia",
    "all_objects.dia", "UML-demo.dia", "ER-demo.dia",
]
THRESHOLD = 0.15   # max fraction of clearly-different pixels (canary)


def trim(im, Image, ImageChops):
    im = im.convert("RGB")
    bg = Image.new("RGB", im.size, (255, 255, 255))
    bbox = ImageChops.difference(im, bg).getbbox()
    return im.crop(bbox) if bbox else im


def pixel_diff(a, b, Image, ImageChops):
    a, b = trim(a, Image, ImageChops), trim(b, Image, ImageChops)
    W = H = 320

    def fit(im):
        im = im.copy()
        im.thumbnail((W, H))
        c = Image.new("RGB", (W, H), (255, 255, 255))
        c.paste(im, ((W - im.width) // 2, (H - im.height) // 2))
        return c

    diff = ImageChops.difference(fit(a), fit(b)).getdata()
    n = len(diff)
    return sum(1 for p in diff if max(p) > 40) / n


def main():
    our = os.environ.get("DIA_BIN")
    if not our:
        print("DIA_BIN must point at our dia binary")
        return 2
    if len(sys.argv) < 2:
        print("usage: test_cli_parity.py SAMPLES_DIR")
        return 2
    samples_dir = sys.argv[1]

    up = shutil.which("dia")
    xvfb = shutil.which("xvfb-run")
    if not up or not xvfb:
        print("SKIP: upstream dia (%s) or xvfb-run (%s) not found"
              % (bool(up), bool(xvfb)))
        return SKIP
    try:
        from PIL import Image, ImageChops
    except ImportError:
        print("SKIP: Pillow not available")
        return SKIP

    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    failures, tested = 0, 0
    with tempfile.TemporaryDirectory() as tmp:
        for name in SAMPLES:
            src = os.path.join(samples_dir, name)
            if not os.path.exists(src):
                print("SKIP %s (missing)" % name)
                continue
            up_png = os.path.join(tmp, "up.png")
            our_png = os.path.join(tmp, "our.png")
            subprocess.run([xvfb, "-a", up, "-e", up_png, src],
                           capture_output=True)
            subprocess.run([our, "-e", our_png, src],
                           capture_output=True, env=env)
            if not (os.path.exists(up_png) and os.path.exists(our_png)):
                print("FAIL %s: an export produced no file "
                      "(up=%s our=%s)" % (name, os.path.exists(up_png),
                                          os.path.exists(our_png)))
                failures += 1
                continue
            d = pixel_diff(Image.open(up_png), Image.open(our_png),
                           Image, ImageChops)
            tested += 1
            tag = "OK" if d <= THRESHOLD else "FAIL"
            print("%s %-18s pixel diff %.1f%% (<= %.0f%%)"
                  % (tag, name, d * 100, THRESHOLD * 100))
            if d > THRESHOLD:
                failures += 1

    if tested == 0:
        print("SKIP: no samples available to compare")
        return SKIP
    if failures:
        print("CLI parity FAILED (%d)" % failures)
        return 1
    print("CLI parity OK (%d samples within %.0f%%)" % (tested, THRESHOLD * 100))
    return 0


if __name__ == "__main__":
    sys.exit(main())
