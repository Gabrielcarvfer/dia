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

    if failures:
        print("CLI export FAILED")
        return 1
    print("CLI export OK (PNG + PDF)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
