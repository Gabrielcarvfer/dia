#!/usr/bin/env python3
"""Headless CLI export test: `dia --export` renders a sample .dia to PNG and
PDF and produces valid, non-empty files. Needs no display.

Run via meson test. Args: argv[1] = path to a sample .dia.
Env: DIA_BIN = the built dia binary.
"""
import os
import subprocess
import sys
import tempfile


def main():
    dia = os.environ.get("DIA_BIN")
    if not dia:
        print("DIA_BIN must point at the dia binary")
        return 2
    if len(sys.argv) < 2:
        print("usage: test_cli_export.py SAMPLE.dia")
        return 2
    sample = sys.argv[1]

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for ext, magic in (("png", b"PNG"), ("pdf", b"%PDF")):
            out = os.path.join(tmp, "out." + ext)
            res = subprocess.run([dia, "--export", out, sample],
                                 capture_output=True, text=True)
            if res.returncode != 0:
                print("FAIL: dia --export %s exited %d\n%s"
                      % (ext, res.returncode, res.stderr))
                failures += 1
                continue
            if not os.path.exists(out) or os.path.getsize(out) == 0:
                print("FAIL: no %s file produced" % ext)
                failures += 1
                continue
            with open(out, "rb") as fh:
                head = fh.read(8)
            if magic not in head:
                print("FAIL: %s missing %r signature (got %r)"
                      % (ext, magic, head))
                failures += 1
                continue
            print("PASS: %s export (%d bytes)" % (ext, os.path.getsize(out)))

        # --list-filters lists the cairo formats
        res = subprocess.run([dia, "--list-filters"],
                             capture_output=True, text=True)
        if res.returncode != 0 or not all(f in res.stdout
                                          for f in ("png", "svg", "pdf")):
            print("FAIL: --list-filters output: %r" % res.stdout)
            failures += 1
        else:
            print("PASS: --list-filters")

        # -t chooses the format regardless of extension; -s bounds the size
        out = os.path.join(tmp, "sized.out")
        res = subprocess.run([dia, "-e", out, "-t", "png", "-s", "200x150",
                              sample], capture_output=True, text=True)
        ok = res.returncode == 0 and os.path.exists(out)
        if ok:
            with open(out, "rb") as fh:
                ok = b"PNG" in fh.read(8)
        if ok:
            try:
                from PIL import Image
                w, h = Image.open(out).size
                ok = w <= 200 and h <= 150   # fit within the requested box
            except ImportError:
                pass   # Pillow optional; signature check already passed
        if not ok:
            print("FAIL: -t png -s 200x150 (rc=%d)\n%s"
                  % (res.returncode, res.stderr))
            failures += 1
        else:
            print("PASS: -t png -s 200x150 (fit within bounds)")

    if failures:
        print("CLI export FAILED")
        return 1
    print("CLI export OK (PNG + PDF + filters + size)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
